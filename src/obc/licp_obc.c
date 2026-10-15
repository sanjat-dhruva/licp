/**
 * @file licp_obc.c
 * @brief On-board computer link framing and streaming decoder.
 * @ingroup obc
 */
#include "licp/obc/licp_obc.h"

#include <string.h>

#include "licp/common/licp_bytes.h"
#include "licp/common/licp_crc.h"

size_t licp_obc_encode(uint8_t type, uint16_t seq, const uint8_t *payload, uint16_t len, uint8_t *out, size_t cap)
{
    size_t total = LICP_OBC_HDR_LEN + (size_t)len + LICP_OBC_CRC_LEN;
    if (out == NULL || len > LICP_OBC_MAX_PAYLOAD || total > cap || (len > 0u && payload == NULL)) {
        return 0;
    }
    out[0] = LICP_OBC_SYNC0;
    out[1] = LICP_OBC_SYNC1;
    out[2] = type;
    licp_put_u16(&out[3], seq);
    licp_put_u16(&out[5], len);
    if (len > 0u) {
        memcpy(&out[LICP_OBC_HDR_LEN], payload, len);
    }
    licp_put_u16(&out[LICP_OBC_HDR_LEN + len], licp_crc16_ccitt(&out[2], 5u + (size_t)len));
    return total;
}

void licp_obc_decoder_init(licp_obc_decoder_t *d)
{
    d->have = 0;
    d->need = LICP_OBC_HDR_LEN;
    d->crc_errors = 0;
    d->resyncs = 0;
}

/** Drop the first byte and look for the next sync pair in what is buffered. */
static void resync(licp_obc_decoder_t *d)
{
    size_t i = 1;
    while (i < d->have && d->buf[i] != LICP_OBC_SYNC0) {
        ++i;
    }
    d->resyncs += (uint32_t)i;
    memmove(d->buf, d->buf + i, d->have - i);
    d->have -= i;
    d->need = LICP_OBC_HDR_LEN;
}

bool licp_obc_decoder_feed(licp_obc_decoder_t *d, const uint8_t *data, size_t len, size_t *consumed,
                           licp_obc_msg_t *msg)
{
    size_t used = 0;
    bool done = false;
    if (d == NULL || msg == NULL || (len > 0u && data == NULL)) {
        if (consumed != NULL) {
            *consumed = 0;
        }
        return false;
    }
    for (;;) {
        /* Validate what is buffered before taking more input. */
        if (d->have >= 1u && d->buf[0] != LICP_OBC_SYNC0) {
            resync(d);
            continue;
        }
        if (d->have >= 2u && d->buf[1] != LICP_OBC_SYNC1) {
            resync(d);
            continue;
        }
        if (d->have >= LICP_OBC_HDR_LEN && d->need == LICP_OBC_HDR_LEN) {
            uint16_t plen = licp_get_u16(&d->buf[5]);
            if (plen > LICP_OBC_MAX_PAYLOAD) {
                resync(d);
                continue;
            }
            d->need = LICP_OBC_HDR_LEN + (size_t)plen + LICP_OBC_CRC_LEN;
        }
        if (d->have >= d->need && d->need > LICP_OBC_HDR_LEN) {
            uint16_t plen = licp_get_u16(&d->buf[5]);
            uint16_t crc = licp_get_u16(&d->buf[LICP_OBC_HDR_LEN + plen]);
            if (crc != licp_crc16_ccitt(&d->buf[2], 5u + (size_t)plen)) {
                d->crc_errors++;
                resync(d);
                continue;
            }
            msg->type = d->buf[2];
            msg->seq = licp_get_u16(&d->buf[3]);
            msg->payload = &d->buf[LICP_OBC_HDR_LEN];
            msg->len = plen;
            /* The message stays in the buffer until the next call; mark it consumed then. */
            d->have = 0;
            d->need = LICP_OBC_HDR_LEN;
            done = true;
            break;
        }
        if (used >= len) {
            break;
        }
        /* Take as many bytes as the current stage needs. */
        size_t want = d->need - d->have;
        size_t avail = len - used;
        size_t take = (want < avail) ? want : avail;
        memcpy(&d->buf[d->have], &data[used], take);
        d->have += take;
        used += take;
    }
    if (consumed != NULL) {
        *consumed = used;
    }
    return done;
}
