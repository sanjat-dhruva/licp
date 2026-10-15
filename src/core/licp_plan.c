/**
 * @file licp_plan.c
 * @brief Upload plan parsing and building.
 * @ingroup core
 */
#include "licp/core/licp_plan.h"

#include <string.h>

#include "licp/common/licp_bytes.h"
#include "licp/common/licp_crc.h"

#define PLAN_HDR_LEN 11u     /**< Header: magic 4, version 1, sequence 4, count 2. */
#define DEVICE_VALUE_LEN 31u /**< Length of a DEVICE_UPSERT value. */

/** @brief Check the length rule of one entry type (unknown types are accepted). */
static licp_status_t check_entry(uint8_t type, uint16_t len)
{
    switch (type) {
    case LICP_PLAN_DEVICE_UPSERT: return (len == DEVICE_VALUE_LEN) ? LICP_OK : LICP_ERR_FORMAT;
    case LICP_PLAN_DEVICE_REMOVE: return (len == 4u) ? LICP_OK : LICP_ERR_FORMAT;
    case LICP_PLAN_ORBIT:         return (len == LICP_ORBIT_RECORD_LEN) ? LICP_OK : LICP_ERR_FORMAT;
    case LICP_PLAN_BROADCAST:     return (len >= 2u && len <= 256u) ? LICP_OK : LICP_ERR_FORMAT;
    case LICP_PLAN_UNICAST:       return (len >= 7u && len <= 261u) ? LICP_OK : LICP_ERR_FORMAT;
    case LICP_PLAN_GROUP_KEY:     return (len == 16u) ? LICP_OK : LICP_ERR_FORMAT;
    case LICP_PLAN_PARAM:         return (len == 5u) ? LICP_OK : LICP_ERR_FORMAT;
    default:                      return LICP_OK; /* unknown types: length is self-describing */
    }
}

licp_status_t licp_plan_validate(const uint8_t *buf, size_t len, uint32_t *plan_seq)
{
    if (buf == NULL) {
        return LICP_ERR_PARAM;
    }
    if (len < PLAN_HDR_LEN + 4u) {
        return LICP_ERR_LENGTH;
    }
    if (memcmp(buf, "LPLN", 4) != 0 || buf[4] != LICP_PLAN_VERSION) {
        return LICP_ERR_FORMAT;
    }
    if (licp_crc32(0, buf, len - 4u) != licp_get_u32(&buf[len - 4u])) {
        return LICP_ERR_CRC;
    }
    uint16_t count = licp_get_u16(&buf[9]);
    licp_reader_t r;
    licp_reader_init(&r, buf + PLAN_HDR_LEN, len - PLAN_HDR_LEN - 4u);
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t type = licp_reader_u8(&r);
        uint16_t elen = licp_reader_u16(&r);
        if (!r.ok || licp_reader_take(&r, elen) == NULL) {
            return LICP_ERR_LENGTH;
        }
        licp_status_t st = check_entry(type, elen);
        if (st != LICP_OK) {
            return st;
        }
    }
    if (licp_reader_left(&r) != 0u) {
        return LICP_ERR_LENGTH;
    }
    if (plan_seq != NULL) {
        *plan_seq = licp_get_u32(&buf[5]);
    }
    return LICP_OK;
}

/** @brief Decode a DEVICE_UPSERT value into a device record. */
static void decode_device(const uint8_t *v, licp_device_t *d)
{
    memset(d, 0, sizeof(*d));
    d->dev_addr = licp_get_u32(v);
    memcpy(d->nwk_s_key, v + 4, 16);
    d->home_channel = v[20];
    d->home_slot = v[21];
    d->priority = v[22];
    d->short_id = licp_get_u16(v + 23);
    d->fcnt_up_last = licp_get_u32(v + 25);
    d->fcnt_valid = (v[29] != 0u);
    d->flags = v[30];
}

licp_status_t licp_plan_apply(const uint8_t *buf, size_t len, const licp_plan_handlers_t *h, uint32_t *plan_seq)
{
    if (h == NULL) {
        return LICP_ERR_PARAM;
    }
    licp_status_t st = licp_plan_validate(buf, len, plan_seq);
    if (st != LICP_OK) {
        return st;
    }
    uint16_t count = licp_get_u16(&buf[9]);
    licp_reader_t r;
    licp_reader_init(&r, buf + PLAN_HDR_LEN, len - PLAN_HDR_LEN - 4u);
    for (uint16_t i = 0; i < count && st == LICP_OK; ++i) {
        uint8_t type = licp_reader_u8(&r);
        uint16_t elen = licp_reader_u16(&r);
        const uint8_t *v = licp_reader_take(&r, elen);
        switch (type) {
        case LICP_PLAN_DEVICE_UPSERT:
            if (h->on_device_upsert != NULL) {
                licp_device_t d;
                decode_device(v, &d);
                st = h->on_device_upsert(h->user, &d);
                memset(d.nwk_s_key, 0, sizeof(d.nwk_s_key));
            }
            break;
        case LICP_PLAN_DEVICE_REMOVE:
            if (h->on_device_remove != NULL) {
                st = h->on_device_remove(h->user, licp_get_u32(v));
            }
            break;
        case LICP_PLAN_ORBIT:
            if (h->on_orbit != NULL) {
                licp_orbit_t o;
                st = licp_orbit_unpack(v, &o);
                if (st == LICP_OK) {
                    st = h->on_orbit(h->user, &o);
                }
            }
            break;
        case LICP_PLAN_BROADCAST:
            if (h->on_broadcast != NULL) {
                st = h->on_broadcast(h->user, v[0], v + 1, (size_t)elen - 1u);
            }
            break;
        case LICP_PLAN_UNICAST:
            if (h->on_unicast != NULL) {
                st = h->on_unicast(h->user, licp_get_u32(v), licp_get_u16(v + 4), v + 6, (size_t)elen - 6u);
            }
            break;
        case LICP_PLAN_GROUP_KEY:
            if (h->on_group_key != NULL) {
                st = h->on_group_key(h->user, v);
            }
            break;
        case LICP_PLAN_PARAM:
            if (h->on_param != NULL) {
                st = h->on_param(h->user, v[0], licp_get_u32(v + 1));
            }
            break;
        default:
            if (h->on_unknown != NULL) {
                st = h->on_unknown(h->user, type, v, elen);
            }
            break;
        }
    }
    return st;
}

void licp_plan_begin(licp_plan_builder_t *b, uint8_t *buf, size_t cap, uint32_t plan_seq)
{
    b->buf = buf;
    b->cap = cap;
    b->len = 0;
    b->entries = 0;
    b->ok = (buf != NULL && cap >= PLAN_HDR_LEN + 4u);
    if (b->ok) {
        memcpy(buf, "LPLN", 4);
        buf[4] = LICP_PLAN_VERSION;
        licp_put_u32(&buf[5], plan_seq);
        licp_put_u16(&buf[9], 0);
        b->len = PLAN_HDR_LEN;
    }
}

void licp_plan_add(licp_plan_builder_t *b, uint8_t type, const uint8_t *value, uint16_t len)
{
    if (!b->ok || b->len + 3u + len + 4u > b->cap || (len > 0u && value == NULL)) {
        b->ok = 0;
        return;
    }
    b->buf[b->len] = type;
    licp_put_u16(&b->buf[b->len + 1u], len);
    if (len > 0u) {
        memcpy(&b->buf[b->len + 3u], value, len);
    }
    b->len += 3u + len;
    b->entries++;
}

void licp_plan_add_device(licp_plan_builder_t *b, const licp_device_t *d)
{
    uint8_t v[DEVICE_VALUE_LEN];
    licp_put_u32(v, d->dev_addr);
    memcpy(v + 4, d->nwk_s_key, 16);
    v[20] = d->home_channel;
    v[21] = d->home_slot;
    v[22] = d->priority;
    licp_put_u16(v + 23, d->short_id);
    licp_put_u32(v + 25, d->fcnt_up_last);
    v[29] = d->fcnt_valid ? 1u : 0u;
    v[30] = d->flags;
    licp_plan_add(b, LICP_PLAN_DEVICE_UPSERT, v, DEVICE_VALUE_LEN);
}

size_t licp_plan_end(licp_plan_builder_t *b)
{
    if (!b->ok || b->len + 4u > b->cap) {
        return 0;
    }
    licp_put_u16(&b->buf[9], b->entries);
    licp_put_u32(&b->buf[b->len], licp_crc32(0, b->buf, b->len));
    b->len += 4u;
    return b->len;
}
