/**
 * @file licp_downlink.c
 * @brief Downlink queue and beacon-window planner implementation.
 * @ingroup core
 */
#include "licp/core/licp_downlink.h"

#include <string.h>

#include "licp/common/licp_log.h"

/** Module tag used in log messages. */
static const char *TAG = "downlink";

void licp_dl_default_config(licp_dl_config_t *cfg)
{
    cfg->lead_ms = 50u;
    cfg->gap_ms = 30u;
    cfg->max_call_ins = 3u;
}

void licp_dlq_init(licp_dl_queue_t *q, const licp_dl_config_t *cfg)
{
    memset(q, 0, sizeof(*q));
    q->cfg = *cfg;
}

/** @brief Remove queue item @p i, keeping the order of the rest. */
static void remove_at(licp_dl_queue_t *q, size_t i)
{
    memmove(&q->items[i], &q->items[i + 1u], (q->count - i - 1u) * sizeof(licp_dl_item_t));
    q->count--;
}

licp_status_t licp_dlq_add(licp_dl_queue_t *q, licp_dl_kind_t kind, uint32_t dev_addr, uint16_t short_id,
                           uint8_t group, const uint8_t *data, size_t len)
{
    if (q == NULL || data == NULL || len == 0u || len > LICP_FRAME_MAX_LEN ||
        (kind != LICP_DL_UNICAST && kind != LICP_DL_BROADCAST && kind != LICP_DL_ACKMAP)) {
        return LICP_ERR_PARAM;
    }
    if (kind == LICP_DL_ACKMAP) {
        for (size_t i = 0; i < q->count; ++i) {
            if (q->items[i].kind == LICP_DL_ACKMAP) {
                remove_at(q, i);
                break;
            }
        }
    }
    if (q->count >= LICP_DLQ_CAPACITY) {
        return LICP_ERR_FULL;
    }
    licp_dl_item_t *it = &q->items[q->count++];
    memset(it, 0, sizeof(*it));
    it->kind = kind;
    it->state = LICP_DL_WAIT_CALLIN;
    it->dev_addr = dev_addr;
    it->short_id = short_id;
    it->group = group;
    memcpy(it->data, data, len);
    it->len = len;
    return LICP_OK;
}

uint8_t licp_dlq_take_call_ins(licp_dl_queue_t *q, uint16_t *ids, uint8_t max)
{
    uint8_t n = 0;
    if (q == NULL || ids == NULL) {
        return 0;
    }
    size_t i = 0;
    while (i < q->count) {
        licp_dl_item_t *it = &q->items[i];
        if (it->kind == LICP_DL_UNICAST && it->state != LICP_DL_READY) {
            if (it->call_ins >= q->cfg.max_call_ins) {
                LICP_LOGW(TAG, "dropping unanswered message for %08X", (unsigned)it->dev_addr);
                q->dropped++;
                remove_at(q, i);
                continue;
            }
            if (n < max) {
                ids[n++] = it->short_id;
                it->state = LICP_DL_CALLED;
                it->call_ins++;
            }
        }
        ++i;
    }
    return n;
}

uint8_t licp_dlq_take_broadcasts(licp_dl_queue_t *q, uint8_t *groups, uint8_t max)
{
    uint8_t n = 0;
    if (q == NULL || groups == NULL) {
        return 0;
    }
    for (size_t i = 0; i < q->count && n < max; ++i) {
        licp_dl_item_t *it = &q->items[i];
        if (it->kind == LICP_DL_BROADCAST && !it->announced) {
            it->announced = true;
            groups[n++] = it->group;
        }
    }
    return n;
}

void licp_dlq_on_uplink(licp_dl_queue_t *q, uint32_t dev_addr)
{
    if (q == NULL) {
        return;
    }
    for (size_t i = 0; i < q->count; ++i) {
        licp_dl_item_t *it = &q->items[i];
        if (it->kind == LICP_DL_UNICAST && it->state == LICP_DL_CALLED && it->dev_addr == dev_addr) {
            it->state = LICP_DL_READY;
        }
    }
}

/** Try to place a transmission at @p *t; advances @p *t on success. */
static bool place(const licp_region_t *region, uint64_t *t, uint64_t end, uint32_t gap_ms, const uint8_t *data,
                  size_t len, licp_tx_request_t *req)
{
    uint32_t air_us = licp_lora_airtime_us(&region->downlink, (uint32_t)len);
    if (air_us == 0u || len > LICP_FRAME_MAX_LEN || len > region->downlink_max_payload) {
        return false;
    }
    uint64_t air_ms = ((uint64_t)air_us + 999u) / 1000u;
    if (*t + air_ms > end) {
        return false;
    }
    memset(req, 0, sizeof(*req));
    req->time_ms = *t;
    req->freq_hz = region->downlink_hz;
    req->mod = region->downlink;
    req->power_dbm = region->downlink_power_dbm;
    memcpy(req->payload, data, len);
    req->len = len;
    req->airtime_us = air_us;
    *t += air_ms + gap_ms;
    return true;
}

size_t licp_dl_plan_window(licp_dl_queue_t *q, const licp_region_t *region, uint64_t window_start,
                           uint32_t window_ms, const uint8_t *beacon, size_t beacon_len, licp_tx_request_t *out,
                           size_t max)
{
    if (q == NULL || region == NULL || beacon == NULL || out == NULL || max == 0u) {
        return 0;
    }
    uint64_t t = window_start + q->cfg.lead_ms;
    uint64_t end = window_start + window_ms;
    size_t n = 0;
    if (!place(region, &t, end, q->cfg.gap_ms, beacon, beacon_len, &out[n])) {
        LICP_LOGE(TAG, "beacon does not fit in the window");
        return 0;
    }
    ++n;

    /* Order: ready one-to-one, announced broadcasts, acknowledgement bitmap. */
    static const licp_dl_kind_t order[3] = {LICP_DL_UNICAST, LICP_DL_BROADCAST, LICP_DL_ACKMAP};
    for (unsigned k = 0; k < 3u; ++k) {
        size_t i = 0;
        while (i < q->count && n < max) {
            licp_dl_item_t *it = &q->items[i];
            bool eligible = (it->kind == order[k]) &&
                            ((it->kind == LICP_DL_UNICAST && it->state == LICP_DL_READY) ||
                             (it->kind == LICP_DL_BROADCAST && it->announced) || it->kind == LICP_DL_ACKMAP);
            if (eligible && place(region, &t, end, q->cfg.gap_ms, it->data, it->len, &out[n])) {
                ++n;
                remove_at(q, i);
                continue;
            }
            if (eligible && it->kind == LICP_DL_BROADCAST) {
                it->announced = false; /* did not fit: announce again next time */
            }
            ++i;
        }
    }
    return n;
}
