/**
 * @file licp_beacon.c
 * @brief Beacon and acknowledgement-bitmap frame encoding and decoding.
 * @ingroup proto
 */
#include "licp/proto/licp_beacon.h"

#include <math.h>
#include <string.h>

#include "licp/common/licp_bytes.h"
#include "licp/crypto/licp_cmac.h"

#define PFRAME_HDR_LEN   3u  /**< MHDR, kind and version. */
#define BEACON_FIXED_LEN 15u /**< Satellite id, superframe, time (6), probabilities (4). */
#define TAG_LEN          4u  /**< Truncated CMAC tag length. */
#define SECTION_HDR_LEN  2u  /**< Section type and length bytes. */

uint8_t licp_prob_to_u8(double p)
{
    if (!(p > 0.0)) {
        return 0u;
    }
    if (p >= 1.0) {
        return 255u;
    }
    return (uint8_t)lround(p * 255.0);
}

double licp_prob_from_u8(uint8_t v)
{
    return (double)v / 255.0;
}

/** @brief Compute the 4-byte CMAC tag over @p len bytes and append it. */
static void append_tag(const uint8_t key[16], uint8_t *buf, size_t len)
{
    uint8_t tag[16];
    licp_cmac(key, buf, len, tag);
    memcpy(&buf[len], tag, TAG_LEN);
}

/** @brief Verify the trailing 4-byte tag (constant-time comparison). */
static bool check_tag(const uint8_t key[16], const uint8_t *buf, size_t len)
{
    uint8_t tag[16];
    licp_cmac(key, buf, len - TAG_LEN, tag);
    uint8_t diff = 0;
    for (size_t i = 0; i < TAG_LEN; ++i) {
        diff = (uint8_t)(diff | (tag[i] ^ buf[len - TAG_LEN + i]));
    }
    return diff == 0u;
}

licp_status_t licp_beacon_encode(const licp_beacon_t *b, const uint8_t key[16], size_t max_len, uint8_t *out,
                                 size_t cap, size_t *out_len, licp_beacon_fit_t *fit)
{
    if (b == NULL || key == NULL || out == NULL || out_len == NULL || b->sos_ack_count > LICP_BEACON_MAX_ACKS ||
        b->call_in_count > LICP_BEACON_MAX_CALLINS || b->broadcast_count > LICP_BEACON_MAX_GROUPS ||
        b->extra_count > LICP_BEACON_MAX_EXTRA) {
        return LICP_ERR_PARAM;
    }
    size_t limit = (max_len < cap) ? max_len : cap;
    if (limit < PFRAME_HDR_LEN + BEACON_FIXED_LEN + TAG_LEN) {
        return LICP_ERR_LENGTH;
    }
    licp_beacon_fit_t f;
    memset(&f, 0, sizeof(f));

    licp_writer_t w;
    licp_writer_init(&w, out, limit - TAG_LEN);
    licp_writer_u8(&w, LICP_PFRAME_MHDR);
    licp_writer_u8(&w, LICP_PFRAME_BEACON);
    licp_writer_u8(&w, LICP_PFRAME_VERSION);
    licp_writer_u8(&w, b->sat_id);
    licp_writer_u32(&w, b->superframe);
    licp_writer_u32(&w, b->gnss_s);
    licp_writer_u16(&w, b->gnss_ms);
    for (unsigned i = 0; i < LICP_PRIORITY_CLASSES; ++i) {
        licp_writer_u8(&w, b->access_p[i]);
    }

    /* 1. Emergency acknowledgements, truncated to fit. */
    if (b->sos_ack_count > 0u) {
        size_t room = w.cap - w.len;
        if (room > SECTION_HDR_LEN) {
            size_t n = (room - SECTION_HDR_LEN) / 2u;
            if (n > b->sos_ack_count) {
                n = b->sos_ack_count;
            }
            if (n > 0u) {
                licp_writer_u8(&w, LICP_BSEC_SOS_ACK);
                licp_writer_u8(&w, (uint8_t)(n * 2u));
                for (size_t i = 0; i < n; ++i) {
                    licp_writer_u16(&w, b->sos_acks[i]);
                }
                f.sos_acks_sent = (uint8_t)n;
            }
        }
    }
    /* 2. Call-ins, truncated to fit. */
    if (b->call_in_count > 0u) {
        size_t room = w.cap - w.len;
        if (room > SECTION_HDR_LEN) {
            size_t n = (room - SECTION_HDR_LEN) / 2u;
            if (n > b->call_in_count) {
                n = b->call_in_count;
            }
            if (n > 0u) {
                licp_writer_u8(&w, LICP_BSEC_CALL_IN);
                licp_writer_u8(&w, (uint8_t)(n * 2u));
                for (size_t i = 0; i < n; ++i) {
                    licp_writer_u16(&w, b->call_ins[i]);
                }
                f.call_ins_sent = (uint8_t)n;
            }
        }
    }
    /* 3. Broadcast notice, whole or nothing. */
    if (b->broadcast_count > 0u && (w.cap - w.len) >= SECTION_HDR_LEN + b->broadcast_count) {
        licp_writer_u8(&w, LICP_BSEC_BROADCAST);
        licp_writer_u8(&w, b->broadcast_count);
        licp_writer_bytes(&w, b->broadcast_groups, b->broadcast_count);
        f.broadcast_sent = true;
    }
    /* 4. Orbit record, whole or nothing. */
    if (b->has_orbit) {
        uint8_t rec[LICP_ORBIT_RECORD_LEN];
        if (licp_orbit_pack(&b->orbit, rec) != LICP_OK) {
            return LICP_ERR_PARAM;
        }
        if ((w.cap - w.len) >= SECTION_HDR_LEN + LICP_ORBIT_RECORD_LEN) {
            licp_writer_u8(&w, LICP_BSEC_ORBIT);
            licp_writer_u8(&w, LICP_ORBIT_RECORD_LEN);
            licp_writer_bytes(&w, rec, sizeof(rec));
            f.orbit_sent = true;
        }
    }
    /* 5. Extension sections, each whole or nothing, in order. */
    for (uint8_t i = 0; i < b->extra_count; ++i) {
        const licp_beacon_extra_t *x = &b->extra[i];
        if (x->len > 0u && x->data == NULL) {
            return LICP_ERR_PARAM;
        }
        if ((w.cap - w.len) < SECTION_HDR_LEN + (size_t)x->len) {
            break;
        }
        licp_writer_u8(&w, x->type);
        licp_writer_u8(&w, x->len);
        licp_writer_bytes(&w, x->data, x->len);
        f.extra_sent++;
    }

    if (!w.ok) {
        return LICP_ERR_LENGTH;
    }
    append_tag(key, out, w.len);
    *out_len = w.len + TAG_LEN;
    if (fit != NULL) {
        *fit = f;
    }
    return LICP_OK;
}

licp_status_t licp_beacon_decode(const uint8_t *buf, size_t len, const uint8_t key[16], licp_beacon_t *b)
{
    if (buf == NULL || key == NULL || b == NULL) {
        return LICP_ERR_PARAM;
    }
    if (len < PFRAME_HDR_LEN + BEACON_FIXED_LEN + TAG_LEN) {
        return LICP_ERR_LENGTH;
    }
    if (buf[0] != LICP_PFRAME_MHDR || buf[1] != LICP_PFRAME_BEACON || buf[2] != LICP_PFRAME_VERSION) {
        return LICP_ERR_FORMAT;
    }
    if (!check_tag(key, buf, len)) {
        return LICP_ERR_MIC;
    }
    memset(b, 0, sizeof(*b));
    licp_reader_t r;
    licp_reader_init(&r, buf + PFRAME_HDR_LEN, len - PFRAME_HDR_LEN - TAG_LEN);
    b->sat_id = licp_reader_u8(&r);
    b->superframe = licp_reader_u32(&r);
    b->gnss_s = licp_reader_u32(&r);
    b->gnss_ms = licp_reader_u16(&r);
    for (unsigned i = 0; i < LICP_PRIORITY_CLASSES; ++i) {
        b->access_p[i] = licp_reader_u8(&r);
    }
    while (licp_reader_left(&r) > 0u) {
        if (licp_reader_left(&r) < SECTION_HDR_LEN) {
            return LICP_ERR_FORMAT;
        }
        uint8_t type = licp_reader_u8(&r);
        uint8_t slen = licp_reader_u8(&r);
        const uint8_t *v = licp_reader_take(&r, slen);
        if (v == NULL) {
            return LICP_ERR_FORMAT;
        }
        switch (type) {
        case LICP_BSEC_SOS_ACK:
            if ((slen % 2u) != 0u || slen / 2u > LICP_BEACON_MAX_ACKS) {
                return LICP_ERR_FORMAT;
            }
            b->sos_ack_count = (uint8_t)(slen / 2u);
            for (uint8_t i = 0; i < b->sos_ack_count; ++i) {
                b->sos_acks[i] = licp_get_u16(&v[i * 2u]);
            }
            break;
        case LICP_BSEC_CALL_IN:
            if ((slen % 2u) != 0u || slen / 2u > LICP_BEACON_MAX_CALLINS) {
                return LICP_ERR_FORMAT;
            }
            b->call_in_count = (uint8_t)(slen / 2u);
            for (uint8_t i = 0; i < b->call_in_count; ++i) {
                b->call_ins[i] = licp_get_u16(&v[i * 2u]);
            }
            break;
        case LICP_BSEC_BROADCAST:
            if (slen > LICP_BEACON_MAX_GROUPS) {
                return LICP_ERR_FORMAT;
            }
            b->broadcast_count = slen;
            memcpy(b->broadcast_groups, v, slen);
            break;
        case LICP_BSEC_ORBIT:
            if (slen != LICP_ORBIT_RECORD_LEN || licp_orbit_unpack(v, &b->orbit) != LICP_OK) {
                return LICP_ERR_FORMAT;
            }
            b->has_orbit = true;
            break;
        default:
            /* Unknown or extension section: skipped for forward compatibility. */
            break;
        }
    }
    return LICP_OK;
}

licp_status_t licp_ackmap_encode(uint8_t sat_id, const licp_bitmap_t *bm, uint16_t first, uint16_t count,
                                 const uint8_t key[16], uint8_t *out, size_t cap, size_t *out_len)
{
    if (bm == NULL || key == NULL || out == NULL || out_len == NULL || count == 0u ||
        (uint32_t)first + count > bm->size) {
        return LICP_ERR_PARAM;
    }
    size_t bytes = ((size_t)count + 7u) / 8u;
    size_t need = PFRAME_HDR_LEN + 7u + bytes + TAG_LEN;
    if (need > cap) {
        return LICP_ERR_LENGTH;
    }
    size_t pos = 0;
    out[pos++] = LICP_PFRAME_MHDR;
    out[pos++] = LICP_PFRAME_ACK_BITMAP;
    out[pos++] = LICP_PFRAME_VERSION;
    out[pos++] = sat_id;
    licp_put_u16(&out[pos], bm->pass_id);
    pos += 2;
    licp_put_u16(&out[pos], first);
    pos += 2;
    licp_put_u16(&out[pos], count);
    pos += 2;
    if (licp_bitmap_extract(bm, first, count, &out[pos], cap - pos) != bytes) {
        return LICP_ERR_LENGTH;
    }
    pos += bytes;
    append_tag(key, out, pos);
    *out_len = pos + TAG_LEN;
    return LICP_OK;
}

licp_status_t licp_ackmap_decode(const uint8_t *buf, size_t len, const uint8_t key[16], uint8_t *sat_id,
                                 licp_bitmap_t *bm, uint16_t *first, uint16_t *count)
{
    if (buf == NULL || key == NULL || bm == NULL || first == NULL || count == NULL || sat_id == NULL) {
        return LICP_ERR_PARAM;
    }
    if (len < PFRAME_HDR_LEN + 7u + 1u + TAG_LEN) {
        return LICP_ERR_LENGTH;
    }
    if (buf[0] != LICP_PFRAME_MHDR || buf[1] != LICP_PFRAME_ACK_BITMAP || buf[2] != LICP_PFRAME_VERSION) {
        return LICP_ERR_FORMAT;
    }
    if (!check_tag(key, buf, len)) {
        return LICP_ERR_MIC;
    }
    *sat_id = buf[3];
    uint16_t pass_id = licp_get_u16(&buf[4]);
    uint16_t f = licp_get_u16(&buf[6]);
    uint16_t c = licp_get_u16(&buf[8]);
    size_t bytes = ((size_t)c + 7u) / 8u;
    if (c == 0u || PFRAME_HDR_LEN + 7u + bytes + TAG_LEN != len || (uint32_t)f + c > LICP_BITMAP_MAX_BITS) {
        return LICP_ERR_FORMAT;
    }
    if (licp_bitmap_reset(bm, (uint16_t)(f + c), pass_id) != LICP_OK) {
        return LICP_ERR_FORMAT;
    }
    for (uint16_t i = 0; i < c; ++i) {
        if ((buf[10u + i / 8u] & (1u << (i % 8u))) != 0u) {
            (void)licp_bitmap_set(bm, (uint16_t)(f + i));
        }
    }
    *first = f;
    *count = c;
    return LICP_OK;
}
