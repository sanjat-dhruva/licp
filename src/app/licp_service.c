/**
 * @file licp_service.c
 * @brief On-board service orchestration.
 * @ingroup app
 */
#include "licp/app/licp_service.h"

#include <string.h>

#include "licp/common/licp_bytes.h"
#include "licp/common/licp_log.h"

/** Module tag used in log messages. */
static const char *TAG = "service";

/* ------------------------------------------------------------------------- */
/* Pipeline hook                                                              */
/* ------------------------------------------------------------------------- */

/**
 * @brief Pipeline hook of the service: on every accepted frame, mark called-in
 *        one-to-one messages ready, note the time and flag the device table for saving.
 */
static void on_frame(licp_rx_outcome_t outcome, const licp_frame_t *f, const licp_device_t *d,
                     const licp_rx_frame_t *rx, void *user)
{
    licp_service_t *s = (licp_service_t *)user;
    (void)d;
    if (outcome == LICP_RX_ACCEPTED && f != NULL) {
        licp_dlq_on_uplink(&s->dlq, f->dev_addr);
        s->last_accept_ms = rx->gnss_ms;
        s->devices_dirty = 1;
    }
}

/* ------------------------------------------------------------------------- */
/* Initialisation                                                             */
/* ------------------------------------------------------------------------- */

licp_status_t licp_service_init(licp_service_t *s, const licp_config_t *cfg, const licp_clock_t *clock)
{
    if (s == NULL || cfg == NULL || clock == NULL || clock->now_ms == NULL) {
        return LICP_ERR_PARAM;
    }
    licp_status_t st = licp_config_validate(cfg);
    if (st != LICP_OK) {
        LICP_LOGE(TAG, "invalid configuration");
        return st;
    }
    memset(s, 0, sizeof(*s));
    s->cfg = *cfg;
    s->clock = *clock;
    licp_log_set_level((licp_log_level_t)cfg->log_level);

    st = licp_device_table_init(&s->devices, cfg->device_capacity);
    if (st != LICP_OK) {
        return st;
    }
    st = licp_device_table_load(&s->devices, cfg->device_table_path);
    if (st == LICP_OK) {
        LICP_LOGI(TAG, "loaded %zu devices", licp_device_table_count(&s->devices));
    } else if (st != LICP_ERR_IO) {
        LICP_LOGW(TAG, "device table file unusable (%s); starting empty", licp_status_str(st));
    }

    licp_store_config_t sc = {cfg->store_path, cfg->store_max_bytes, cfg->store_fsync_each};
    st = licp_store_open(&s->store, &sc);
    if (st != LICP_OK) {
        LICP_LOGE(TAG, "cannot open store %s: %s", cfg->store_path, licp_status_str(st));
        licp_device_table_free(&s->devices);
        return st;
    }
    (void)licp_load_init(&s->load, &cfg->load);
    (void)licp_bitmap_reset(&s->bitmap, cfg->bitmap_size, 0);
    st = licp_pipeline_init(&s->pipeline, &cfg->pipeline, &s->devices, s->store, &s->load, &s->bitmap,
                            &s->cfg.region, &s->cfg.sf);
    if (st == LICP_OK) {
        st = licp_pipeline_add_hook(&s->pipeline, on_frame, s);
    }
    if (st != LICP_OK) {
        licp_store_close(s->store);
        licp_device_table_free(&s->devices);
        return st;
    }
    licp_dlq_init(&s->dlq, &cfg->downlink);
    s->last_planned_sf = -1;
    s->start_ms = licp_clock_now(&s->clock);
    return LICP_OK;
}

licp_status_t licp_service_add_rx(licp_service_t *s, const licp_radio_t *radio)
{
    if (s == NULL || radio == NULL || radio->ops == NULL) {
        return LICP_ERR_PARAM;
    }
    if (s->rx_count >= LICP_SVC_MAX_RX) {
        return LICP_ERR_FULL;
    }
    s->rx[s->rx_count++] = *radio;
    return LICP_OK;
}

void licp_service_set_tx(licp_service_t *s, const licp_radio_t *radio)
{
    if (s != NULL && radio != NULL) {
        s->tx = *radio;
    }
}

licp_status_t licp_service_start(licp_service_t *s)
{
    for (size_t i = 0; i < s->rx_count; ++i) {
        licp_status_t st = s->rx[i].ops->start(s->rx[i].ctx);
        if (st != LICP_OK) {
            LICP_LOGE(TAG, "receiver %s failed to start", s->rx[i].ops->name);
            return st;
        }
    }
    if (s->tx.ops != NULL) {
        /* The same back-end may serve receive and transmit: start it only once. */
        bool already = false;
        for (size_t i = 0; i < s->rx_count; ++i) {
            if (s->rx[i].ctx == s->tx.ctx) {
                already = true;
            }
        }
        if (!already) {
            licp_status_t st = s->tx.ops->start(s->tx.ctx);
            if (st != LICP_OK) {
                LICP_LOGE(TAG, "transmitter %s failed to start", s->tx.ops->name);
                return st;
            }
        }
    }
    return LICP_OK;
}

licp_pipeline_t *licp_service_pipeline(licp_service_t *s)
{
    return (s == NULL) ? NULL : &s->pipeline;
}

licp_status_t licp_service_set_beacon_extra(licp_service_t *s, const licp_beacon_extra_t *extra, uint8_t count)
{
    if (s == NULL || count > LICP_BEACON_MAX_EXTRA || (count > 0u && extra == NULL)) {
        return LICP_ERR_PARAM;
    }
    if (count > 0u) {
        memcpy(s->beacon_extra, extra, (size_t)count * sizeof(*extra));
    }
    s->beacon_extra_count = count;
    return LICP_OK;
}

void licp_service_set_plan_extension(licp_service_t *s,
                                     licp_status_t (*fn)(void *user, uint8_t type, const uint8_t *value, size_t len),
                                     void *user)
{
    if (s != NULL) {
        s->plan_ext = fn;
        s->plan_ext_user = user;
    }
}

/* ------------------------------------------------------------------------- */
/* Beacon window                                                              */
/* ------------------------------------------------------------------------- */

/** Highest set bit + 1, or 0 if the bitmap is empty. */
static uint16_t bitmap_extent(const licp_bitmap_t *bm)
{
    for (uint16_t i = bm->size; i > 0u; --i) {
        if (licp_bitmap_test(bm, (uint16_t)(i - 1u))) {
            return i;
        }
    }
    return 0;
}

/** @brief Queue the acknowledgement bitmap if bits were set since it was last queued. */
static void queue_ackmap(licp_service_t *s)
{
    uint16_t set = licp_bitmap_count(&s->bitmap);
    if (set == 0u || set == s->bitmap_sent_count) {
        return;
    }
    uint16_t extent = bitmap_extent(&s->bitmap);
    /* Bits that fit in one downlink: payload minus header(3) + fields(7) + tag(4). */
    uint32_t max_bits = ((uint32_t)s->cfg.region.downlink_max_payload - 14u) * 8u;
    uint16_t count = (extent > max_bits) ? (uint16_t)max_bits : extent;
    if (extent > max_bits) {
        LICP_LOGW(TAG, "bitmap extent %u exceeds one frame; sending first %u bits", (unsigned)extent,
                  (unsigned)count);
    }
    uint8_t frame[LICP_FRAME_MAX_LEN];
    size_t len = 0;
    if (licp_ackmap_encode(s->cfg.sat_id, &s->bitmap, 0, count, s->cfg.group_key, frame, sizeof(frame), &len) ==
            LICP_OK &&
        licp_dlq_add(&s->dlq, LICP_DL_ACKMAP, 0, 0, 0, frame, len) == LICP_OK) {
        s->bitmap_sent_count = set;
    }
}

/**
 * @brief Close the load period, build the beacon for superframe @p sf and hand the
 *        planned beacon-window transmissions to the transmit back-end.
 */
static void plan_window(licp_service_t *s, uint32_t sf)
{
    uint64_t window_start = licp_sf_start_ms(&s->cfg.sf, sf);
    licp_load_end_superframe(&s->load);
    queue_ackmap(s);

    licp_beacon_t b;
    memset(&b, 0, sizeof(b));
    b.sat_id = s->cfg.sat_id;
    b.superframe = sf;
    uint64_t tx_ms = window_start + s->cfg.downlink.lead_ms;
    b.gnss_s = (uint32_t)(tx_ms / 1000u);
    b.gnss_ms = (uint16_t)(tx_ms % 1000u);
    licp_load_get_wire(&s->load, b.access_p);
    b.sos_ack_count = licp_pipeline_take_sos_acks(&s->pipeline, b.sos_acks, LICP_BEACON_MAX_ACKS);
    b.call_in_count = licp_dlq_take_call_ins(&s->dlq, b.call_ins, LICP_BEACON_MAX_CALLINS);
    b.broadcast_count = licp_dlq_take_broadcasts(&s->dlq, b.broadcast_groups, LICP_BEACON_MAX_GROUPS);
    if (s->orbit_count > 0u) {
        b.has_orbit = true;
        b.orbit = s->orbits[s->orbit_next % s->orbit_count];
    }
    memcpy(b.extra, s->beacon_extra, sizeof(b.extra));
    b.extra_count = s->beacon_extra_count;

    uint8_t frame[LICP_FRAME_MAX_LEN];
    size_t len = 0;
    licp_beacon_fit_t fit;
    licp_status_t st = licp_beacon_encode(&b, s->cfg.group_key, s->cfg.region.downlink_max_payload, frame,
                                          sizeof(frame), &len, &fit);
    if (st != LICP_OK) {
        LICP_LOGE(TAG, "beacon encode failed: %s", licp_status_str(st));
        return;
    }
    if (fit.sos_acks_sent < b.sos_ack_count) {
        licp_pipeline_return_sos_acks(&s->pipeline, &b.sos_acks[fit.sos_acks_sent],
                                      (uint8_t)(b.sos_ack_count - fit.sos_acks_sent));
    }
    if (fit.orbit_sent) {
        s->orbit_next = (uint8_t)((s->orbit_next + 1u) % s->orbit_count);
    }

    licp_tx_request_t tx[LICP_DL_MAX_TX];
    size_t n = licp_dl_plan_window(&s->dlq, &s->cfg.region, window_start, s->cfg.sf.beacon_window_ms, frame, len,
                                   tx, LICP_DL_MAX_TX);
    for (size_t i = 0; i < n; ++i) {
        if (s->tx.ops == NULL || s->tx.ops->transmit(s->tx.ctx, &tx[i]) != LICP_OK) {
            LICP_LOGW(TAG, "transmission %zu of superframe %u refused", i, (unsigned)sf);
        }
    }
    LICP_LOGD(TAG, "superframe %u: %zu transmissions planned", (unsigned)sf, n);
}

void licp_service_poll(licp_service_t *s)
{
    if (s == NULL) {
        return;
    }
    /* 1. Receive. */
    licp_rx_frame_t batch[LICP_SVC_RX_BATCH];
    for (size_t r = 0; r < s->rx_count; ++r) {
        size_t n = s->rx[r].ops->receive(s->rx[r].ctx, batch, LICP_SVC_RX_BATCH);
        for (size_t i = 0; i < n; ++i) {
            (void)licp_pipeline_process(&s->pipeline, &batch[i]);
        }
    }

    uint64_t now = licp_clock_now(&s->clock);

    /* 2. New pass after a quiet gap. */
    if (s->last_accept_ms > 0u && now > s->last_accept_ms && now - s->last_accept_ms > s->cfg.pass_gap_ms &&
        licp_bitmap_count(&s->bitmap) > 0u) {
        s->pass_id++;
        (void)licp_bitmap_reset(&s->bitmap, s->cfg.bitmap_size, s->pass_id);
        s->bitmap_sent_count = 0;
        (void)licp_service_flush(s);
        LICP_LOGI(TAG, "pass %u started", (unsigned)s->pass_id);
    }

    /* 3. Plan the next beacon window ahead of time. */
    licp_sf_position_t pos;
    if (licp_sf_locate(&s->cfg.sf, now, &pos) != LICP_OK) {
        return;
    }
    uint32_t next = pos.superframe + 1u;
    uint64_t next_start = licp_sf_start_ms(&s->cfg.sf, next);
    if (now + s->cfg.plan_ahead_ms >= next_start && s->last_planned_sf < (int64_t)next) {
        plan_window(s, next);
        s->last_planned_sf = (int64_t)next;
    }
}

/* ------------------------------------------------------------------------- */
/* Plan handlers                                                              */
/* ------------------------------------------------------------------------- */

/** @brief Plan handler: add or update a device. */
static licp_status_t h_upsert(void *user, const licp_device_t *d)
{
    licp_service_t *s = (licp_service_t *)user;
    s->devices_dirty = 1;
    return licp_device_table_upsert(&s->devices, d);
}

/** @brief Plan handler: remove a device (absent devices are not an error). */
static licp_status_t h_remove(void *user, uint32_t dev_addr)
{
    licp_service_t *s = (licp_service_t *)user;
    s->devices_dirty = 1;
    licp_status_t st = licp_device_table_remove(&s->devices, dev_addr);
    return (st == LICP_ERR_NOT_FOUND) ? LICP_OK : st;
}

/** @brief Plan handler: replace the orbit record of the same satellite or add it to the rotation. */
static licp_status_t h_orbit(void *user, const licp_orbit_t *o)
{
    licp_service_t *s = (licp_service_t *)user;
    for (uint8_t i = 0; i < s->orbit_count; ++i) {
        if (s->orbits[i].sat_id == o->sat_id) {
            s->orbits[i] = *o;
            return LICP_OK;
        }
    }
    if (s->orbit_count < LICP_SVC_MAX_ORBITS) {
        s->orbits[s->orbit_count++] = *o;
        return LICP_OK;
    }
    return LICP_ERR_FULL;
}

/** @brief Plan handler: queue a pre-built broadcast. */
static licp_status_t h_broadcast(void *user, uint8_t group, const uint8_t *frame, size_t len)
{
    licp_service_t *s = (licp_service_t *)user;
    return licp_dlq_add(&s->dlq, LICP_DL_BROADCAST, 0, 0, group, frame, len);
}

/** @brief Plan handler: queue a pre-built one-to-one message (sent after a call-in). */
static licp_status_t h_unicast(void *user, uint32_t dev_addr, uint16_t short_id, const uint8_t *frame, size_t len)
{
    licp_service_t *s = (licp_service_t *)user;
    return licp_dlq_add(&s->dlq, LICP_DL_UNICAST, dev_addr, short_id, 0, frame, len);
}

/** @brief Plan handler: replace the beacon and bitmap signing key. */
static licp_status_t h_group_key(void *user, const uint8_t key[16])
{
    licp_service_t *s = (licp_service_t *)user;
    memcpy(s->cfg.group_key, key, 16);
    return LICP_OK;
}

/** @brief Plan handler: apply a numeric parameter (::licp_param_id_t). */
static licp_status_t h_param(void *user, uint8_t id, uint32_t value)
{
    licp_service_t *s = (licp_service_t *)user;
    switch (id) {
    case LICP_PARAM_BITMAP_SIZE:
        if (value == 0u || value > LICP_BITMAP_MAX_BITS) {
            return LICP_ERR_PARAM;
        }
        s->cfg.bitmap_size = (uint16_t)value;
        s->bitmap_sent_count = 0;
        return licp_bitmap_reset(&s->bitmap, s->cfg.bitmap_size, s->pass_id);
    case LICP_PARAM_MAX_FCNT_GAP:
        s->pipeline.cfg.max_fcnt_gap = value;
        return LICP_OK;
    case LICP_PARAM_LOG_LEVEL:
        if (value > 3u) {
            return LICP_ERR_PARAM;
        }
        licp_log_set_level((licp_log_level_t)value);
        return LICP_OK;
    default:
        LICP_LOGW(TAG, "unknown parameter %u ignored", (unsigned)id);
        return LICP_OK;
    }
}

/** @brief Plan handler: pass unknown entry types to the registered extension, or skip them. */
static licp_status_t h_unknown(void *user, uint8_t type, const uint8_t *value, size_t len)
{
    licp_service_t *s = (licp_service_t *)user;
    if (s->plan_ext != NULL) {
        return s->plan_ext(s->plan_ext_user, type, value, len);
    }
    LICP_LOGW(TAG, "plan entry type 0x%02X skipped", (unsigned)type);
    return LICP_OK;
}

/* ------------------------------------------------------------------------- */
/* On-board computer messages                                                 */
/* ------------------------------------------------------------------------- */

/** @brief Encode a reply message with the next outgoing sequence number. */
static size_t reply_msg(licp_service_t *s, uint8_t type, const uint8_t *payload, uint16_t len, uint8_t *out,
                        size_t cap)
{
    return licp_obc_encode(type, s->obc_seq++, payload, len, out, cap);
}

/** @brief Append one telemetry pair: identifier (1 byte) and value (4 bytes). */
static void tm_put(licp_writer_t *w, uint8_t id, uint32_t value)
{
    licp_writer_u8(w, id);
    licp_writer_u32(w, value);
}

/** @brief Build the STATUS telemetry message. @return Encoded length. */
static size_t build_status(licp_service_t *s, uint8_t *out, size_t cap)
{
    uint8_t p[160];
    licp_writer_t w;
    licp_writer_init(&w, p, sizeof(p));
    licp_store_stats_t ss;
    licp_store_get_stats(s->store, &ss);
    licp_radio_stats_t rs;
    memset(&rs, 0, sizeof(rs));
    if (s->tx.ops != NULL) {
        s->tx.ops->stats(s->tx.ctx, &rs);
    }
    uint32_t rejected = 0;
    for (unsigned i = 0; i < LICP_RX_OUTCOME_COUNT; ++i) {
        if (i != LICP_RX_ACCEPTED && i != LICP_RX_COLLISION) {
            rejected += s->pipeline.outcomes[i];
        }
    }
    uint64_t now = licp_clock_now(&s->clock);
    uint8_t pw[LICP_PRIORITY_CLASSES];
    licp_load_get_wire(&s->load, pw);
    tm_put(&w, LICP_TM_UPTIME_S, (uint32_t)((now - s->start_ms) / 1000u));
    tm_put(&w, LICP_TM_DEVICES, (uint32_t)licp_device_table_count(&s->devices));
    tm_put(&w, LICP_TM_PENDING, ss.pending);
    tm_put(&w, LICP_TM_PENDING_SOS, ss.pending_by_class[0]);
    tm_put(&w, LICP_TM_IN_FLIGHT, ss.in_flight);
    tm_put(&w, LICP_TM_EVICTED, ss.evicted);
    tm_put(&w, LICP_TM_REJECTED_FULL, ss.rejected_full);
    tm_put(&w, LICP_TM_RX_ACCEPTED, s->pipeline.outcomes[LICP_RX_ACCEPTED]);
    tm_put(&w, LICP_TM_RX_COLLISIONS, s->pipeline.outcomes[LICP_RX_COLLISION]);
    tm_put(&w, LICP_TM_RX_REJECTED, rejected);
    tm_put(&w, LICP_TM_P_CLASS1, pw[1]);
    tm_put(&w, LICP_TM_P_CLASS2, pw[2]);
    tm_put(&w, LICP_TM_P_CLASS3, pw[3]);
    tm_put(&w, LICP_TM_COLLISION_PERMIL, (uint32_t)(s->load.last_ratio * 1000.0 + 0.5));
    tm_put(&w, LICP_TM_TX_OK, rs.tx_ok);
    tm_put(&w, LICP_TM_TX_REJECTED, rs.tx_rejected);
    tm_put(&w, LICP_TM_PLAN_SEQ, s->plan_seq);
    tm_put(&w, LICP_TM_DL_DROPPED, s->dlq.dropped);
    tm_put(&w, LICP_TM_PASS_ID, s->pass_id);
    if (!w.ok) {
        return 0;
    }
    return reply_msg(s, LICP_OBC_STATUS, p, (uint16_t)w.len, out, cap);
}

/** @brief Apply an uploaded plan and answer with PLAN_RESULT. */
static size_t handle_plan(licp_service_t *s, const licp_obc_msg_t *msg, uint8_t *out, size_t cap)
{
    licp_plan_handlers_t h = {h_upsert, h_remove, h_orbit, h_broadcast, h_unicast, h_group_key, h_param, h_unknown,
                              s};
    uint32_t seq = 0;
    licp_status_t st = licp_plan_apply(msg->payload, msg->len, &h, &seq);
    if (st == LICP_OK) {
        s->plan_seq = seq;
        LICP_LOGI(TAG, "plan %u applied, %zu devices", (unsigned)seq, licp_device_table_count(&s->devices));
        (void)licp_service_flush(s);
    } else {
        LICP_LOGE(TAG, "plan rejected: %s", licp_status_str(st));
    }
    uint8_t p[5];
    licp_put_u32(p, seq);
    p[4] = (uint8_t)(int8_t)st;
    return reply_msg(s, LICP_OBC_PLAN_RESULT, p, sizeof(p), out, cap);
}

/** @brief Hand up to the requested number of stored frames to the ground (FRAMES reply). */
static size_t handle_frames_request(licp_service_t *s, const licp_obc_msg_t *msg, uint8_t *out, size_t cap)
{
    uint16_t want = (msg->len >= 2u) ? licp_get_u16(msg->payload) : (uint16_t)LICP_SVC_MAX_FRAMES_PER_MSG;
    if (want > LICP_SVC_MAX_FRAMES_PER_MSG) {
        want = LICP_SVC_MAX_FRAMES_PER_MSG;
    }
    uint32_t seqs[LICP_SVC_MAX_FRAMES_PER_MSG];
    size_t n = licp_store_select(s->store, seqs, want);

    static uint8_t p[LICP_OBC_MAX_PAYLOAD];
    licp_writer_t w;
    licp_writer_init(&w, p, sizeof(p));
    uint8_t *count_at = licp_writer_reserve(&w, 2);
    uint16_t count = 0;
    for (size_t i = 0; i < n; ++i) {
        licp_store_meta_t m;
        uint8_t phy[LICP_FRAME_MAX_LEN];
        size_t len = 0;
        if (licp_store_read(s->store, seqs[i], &m, phy, sizeof(phy), &len) != LICP_OK) {
            continue;
        }
        licp_writer_u32(&w, seqs[i]);
        uint8_t *mp = licp_writer_reserve(&w, 28);
        if (mp != NULL) {
            licp_put_u64(mp, m.gnss_ms);
            licp_put_u32(mp + 8, m.freq_hz);
            mp[12] = m.channel;
            mp[13] = m.datarate;
            licp_put_u16(mp + 14, (uint16_t)m.rssi_x10);
            licp_put_u16(mp + 16, (uint16_t)m.snr_x10);
            licp_put_u32(mp + 18, m.dev_addr);
            licp_put_u32(mp + 22, m.fcnt32);
            mp[26] = m.priority;
            mp[27] = m.flags;
        }
        licp_writer_u8(&w, (uint8_t)len);
        licp_writer_bytes(&w, phy, len);
        ++count;
    }
    if (!w.ok || count_at == NULL) {
        licp_store_requeue_in_flight(s->store);
        return 0;
    }
    licp_put_u16(count_at, count);
    return reply_msg(s, LICP_OBC_FRAMES, p, (uint16_t)w.len, out, cap);
}

/** @brief Release frames confirmed by the ground and answer with STATUS. */
static size_t handle_release(licp_service_t *s, const licp_obc_msg_t *msg, uint8_t *out, size_t cap)
{
    if (msg->len >= 2u) {
        uint16_t count = licp_get_u16(msg->payload);
        if ((size_t)count * 4u + 2u == msg->len) {
            uint32_t seqs[256];
            size_t done = 0;
            while (done < count) {
                size_t chunk = count - done;
                if (chunk > 256u) {
                    chunk = 256u;
                }
                for (size_t i = 0; i < chunk; ++i) {
                    seqs[i] = licp_get_u32(msg->payload + 2u + (done + i) * 4u);
                }
                if (licp_store_release(s->store, seqs, chunk) != LICP_OK) {
                    LICP_LOGE(TAG, "release failed");
                    break;
                }
                done += chunk;
            }
        } else {
            LICP_LOGW(TAG, "malformed release message");
        }
    }
    return build_status(s, out, cap);
}

size_t licp_service_handle_obc(licp_service_t *s, const licp_obc_msg_t *msg, uint8_t *reply, size_t cap)
{
    if (s == NULL || msg == NULL || reply == NULL) {
        return 0;
    }
    switch (msg->type) {
    case LICP_OBC_PLAN_UPLOAD:
        return handle_plan(s, msg, reply, cap);
    case LICP_OBC_FRAMES_REQUEST:
        return handle_frames_request(s, msg, reply, cap);
    case LICP_OBC_RELEASE:
        return handle_release(s, msg, reply, cap);
    case LICP_OBC_STATUS_REQUEST:
        return build_status(s, reply, cap);
    case LICP_OBC_CONTACT_END:
        licp_store_requeue_in_flight(s->store);
        return build_status(s, reply, cap);
    default:
        LICP_LOGW(TAG, "unknown message type 0x%02X", (unsigned)msg->type);
        return 0;
    }
}

/* ------------------------------------------------------------------------- */
/* Persistence and shutdown                                                   */
/* ------------------------------------------------------------------------- */

licp_status_t licp_service_flush(licp_service_t *s)
{
    if (s == NULL || !s->devices_dirty) {
        return LICP_OK;
    }
    licp_status_t st = licp_device_table_save(&s->devices, s->cfg.device_table_path);
    if (st == LICP_OK) {
        s->devices_dirty = 0;
    } else {
        LICP_LOGE(TAG, "saving device table failed");
    }
    return st;
}

void licp_service_shutdown(licp_service_t *s)
{
    if (s == NULL) {
        return;
    }
    for (size_t i = 0; i < s->rx_count; ++i) {
        s->rx[i].ops->stop(s->rx[i].ctx);
    }
    if (s->tx.ops != NULL) {
        bool shared = false;
        for (size_t i = 0; i < s->rx_count; ++i) {
            if (s->rx[i].ctx == s->tx.ctx) {
                shared = true;
            }
        }
        if (!shared) {
            s->tx.ops->stop(s->tx.ctx);
        }
    }
    (void)licp_service_flush(s);
    licp_store_close(s->store);
    s->store = NULL;
    licp_device_table_free(&s->devices);
    memset(s->cfg.group_key, 0, sizeof(s->cfg.group_key));
}
