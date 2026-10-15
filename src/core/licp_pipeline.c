/**
 * @file licp_pipeline.c
 * @brief Per-frame processing implementation.
 * @ingroup core
 */
#include "licp/core/licp_pipeline.h"

#include <string.h>

#include "licp/common/licp_log.h"

/** Module tag used in log messages. */
static const char *TAG = "pipeline";

void licp_pipeline_default_config(licp_pipeline_config_t *cfg)
{
    cfg->max_fcnt_gap = 16384u;
    cfg->freq_tolerance_hz = 30000u;
}

licp_status_t licp_pipeline_init(licp_pipeline_t *p, const licp_pipeline_config_t *cfg, licp_device_table_t *devices,
                                 licp_store_t *store, licp_load_t *load, licp_bitmap_t *bitmap,
                                 const licp_region_t *region, const licp_sf_config_t *sf)
{
    if (p == NULL || cfg == NULL || devices == NULL || store == NULL || load == NULL || bitmap == NULL ||
        region == NULL || sf == NULL) {
        return LICP_ERR_PARAM;
    }
    memset(p, 0, sizeof(*p));
    p->cfg = *cfg;
    p->devices = devices;
    p->store = store;
    p->load = load;
    p->bitmap = bitmap;
    p->region = region;
    p->sf = sf;
    return LICP_OK;
}

licp_status_t licp_pipeline_add_hook(licp_pipeline_t *p, licp_pipeline_hook_fn fn, void *user)
{
    if (p == NULL || fn == NULL) {
        return LICP_ERR_PARAM;
    }
    if (p->hook_count >= LICP_PIPELINE_MAX_HOOKS) {
        return LICP_ERR_FULL;
    }
    p->hooks[p->hook_count] = fn;
    p->hook_user[p->hook_count] = user;
    p->hook_count++;
    return LICP_OK;
}

const char *licp_rx_outcome_str(licp_rx_outcome_t o)
{
    static const char *const names[LICP_RX_OUTCOME_COUNT] = {
        "accepted", "collision", "bad-format", "unknown-device", "blocked",
        "bad-mic",  "duplicate", "bad-counter", "store-full",   "store-error"};
    return ((unsigned)o < LICP_RX_OUTCOME_COUNT) ? names[o] : "invalid";
}

/** @brief Queue an emergency acknowledgement once per device. */
static void queue_sos(licp_pipeline_t *p, uint16_t short_id)
{
    for (uint8_t i = 0; i < p->sos_count; ++i) {
        if (p->sos_queue[i] == short_id) {
            return; /* already queued */
        }
    }
    if (p->sos_count < LICP_PIPELINE_SOS_QUEUE) {
        p->sos_queue[p->sos_count++] = short_id;
    } else {
        LICP_LOGW(TAG, "emergency acknowledgement queue full, dropping id %u", (unsigned)short_id);
    }
}

/** @brief Count the outcome, call every hook and return the outcome. */
static licp_rx_outcome_t finish(licp_pipeline_t *p, licp_rx_outcome_t o, const licp_frame_t *f,
                                const licp_device_t *d, const licp_rx_frame_t *rx)
{
    p->outcomes[o]++;
    for (uint8_t i = 0; i < p->hook_count; ++i) {
        p->hooks[i](o, f, d, rx, p->hook_user[i]);
    }
    if (o != LICP_RX_ACCEPTED && o != LICP_RX_COLLISION) {
        LICP_LOGD(TAG, "frame dropped: %s", licp_rx_outcome_str(o));
    }
    return o;
}

licp_rx_outcome_t licp_pipeline_process(licp_pipeline_t *p, const licp_rx_frame_t *rx)
{
    if (p == NULL || rx == NULL) {
        return LICP_RX_BAD_FORMAT;
    }
    /* 1. Collision signal. */
    if ((rx->flags & LICP_RX_FLAG_HEADER_ONLY) != 0u) {
        licp_load_on_collision(p->load);
        return finish(p, LICP_RX_COLLISION, NULL, NULL, rx);
    }

    /* 2. Parse and look up. */
    licp_frame_t f;
    uint8_t channel = 0;
    if (licp_frame_parse(rx->phy, rx->len, &f) != LICP_OK ||
        (f.mtype != LICP_MTYPE_UNCONF_DATA_UP && f.mtype != LICP_MTYPE_CONF_DATA_UP) ||
        licp_region_channel_of(p->region, rx->freq_hz, p->cfg.freq_tolerance_hz, &channel) != LICP_OK) {
        return finish(p, LICP_RX_BAD_FORMAT, NULL, NULL, rx);
    }
    licp_device_t *d = licp_device_table_find(p->devices, f.dev_addr);
    if (d == NULL) {
        return finish(p, LICP_RX_UNKNOWN_DEVICE, &f, NULL, rx);
    }
    if ((d->flags & LICP_DEV_FLAG_BLOCKED) != 0u) {
        return finish(p, LICP_RX_BLOCKED, &f, d, rx);
    }

    /* 3. Counter and integrity. */
    uint32_t fcnt32 = d->fcnt_valid ? licp_frame_rebuild_fcnt(d->fcnt_up_last, f.fcnt16) : f.fcnt16;
    if (licp_frame_verify_mic(&f, d->nwk_s_key, fcnt32) != LICP_OK) {
        return finish(p, LICP_RX_BAD_MIC, &f, d, rx);
    }

    /* 4. Duplicate and counter-gap checks. */
    if (d->fcnt_valid) {
        if (fcnt32 == d->fcnt_up_last) {
            return finish(p, LICP_RX_DUPLICATE, &f, d, rx);
        }
        if (fcnt32 - d->fcnt_up_last > p->cfg.max_fcnt_gap) {
            return finish(p, LICP_RX_BAD_COUNTER, &f, d, rx);
        }
    }

    /* 5. Store by priority. */
    bool sos = licp_region_is_sos_channel(p->region, channel);
    uint8_t prio = sos ? 0u : d->priority;
    if (prio == 0u && !sos) {
        prio = 1u; /* class 0 is reserved for the emergency channels */
    }
    if (prio >= LICP_PRIORITY_CLASSES) {
        prio = LICP_PRIORITY_CLASSES - 1u;
    }
    bool shared = false;
    licp_sf_position_t pos;
    if (!sos && licp_sf_locate(p->sf, rx->gnss_ms, &pos) == LICP_OK) {
        shared = pos.in_beacon || pos.slot != d->home_slot || channel != d->home_channel;
    }
    licp_store_meta_t m = {rx->gnss_ms, rx->freq_hz, channel, rx->datarate, rx->rssi_x10, rx->snr_x10,
                           f.dev_addr,  fcnt32,      prio,    (uint8_t)(shared ? 1u : 0u)};
    licp_status_t st = licp_store_append(p->store, &m, rx->phy, rx->len, NULL);
    if (st == LICP_ERR_FULL) {
        return finish(p, LICP_RX_STORE_FULL, &f, d, rx);
    }
    if (st != LICP_OK) {
        LICP_LOGE(TAG, "store append failed: %s", licp_status_str(st));
        return finish(p, LICP_RX_STORE_ERROR, &f, d, rx);
    }

    /* 6. Commit state. */
    d->fcnt_up_last = fcnt32;
    d->fcnt_valid = true;
    (void)licp_bitmap_set(p->bitmap, d->short_id);
    if (sos) {
        queue_sos(p, d->short_id);
    }
    licp_load_on_decoded(p->load, prio, shared ? 1 : 0);

    /* 7. Hooks. */
    return finish(p, LICP_RX_ACCEPTED, &f, d, rx);
}

uint8_t licp_pipeline_take_sos_acks(licp_pipeline_t *p, uint16_t *out, uint8_t max)
{
    if (p == NULL || out == NULL) {
        return 0;
    }
    uint8_t n = (p->sos_count < max) ? p->sos_count : max;
    memcpy(out, p->sos_queue, (size_t)n * sizeof(uint16_t));
    memmove(p->sos_queue, &p->sos_queue[n], (size_t)(p->sos_count - n) * sizeof(uint16_t));
    p->sos_count = (uint8_t)(p->sos_count - n);
    return n;
}

void licp_pipeline_return_sos_acks(licp_pipeline_t *p, const uint16_t *ids, uint8_t count)
{
    if (p == NULL || ids == NULL || count == 0u) {
        return;
    }
    uint8_t room = (uint8_t)(LICP_PIPELINE_SOS_QUEUE - p->sos_count);
    uint8_t n = (count < room) ? count : room;
    memmove(&p->sos_queue[n], p->sos_queue, (size_t)p->sos_count * sizeof(uint16_t));
    memcpy(p->sos_queue, ids, (size_t)n * sizeof(uint16_t));
    p->sos_count = (uint8_t)(p->sos_count + n);
}
