/**
 * @file licp_radio_fhss.c
 * @brief Character-device back-end for the frequency-hopping receiver.
 * @ingroup hal
 */
#include "licp/hal/licp_radio_fhss.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "licp/common/licp_bytes.h"
#include "licp/common/licp_log.h"

/** Module tag used in log messages. */
static const char *TAG = "fhss";

/** @copydoc licp_fhss_decode_record */
licp_status_t licp_fhss_decode_record(const uint8_t rec[LICP_FHSS_RECORD_LEN], licp_rx_frame_t *out)
{
    if (rec == NULL || out == NULL) {
        return LICP_ERR_PARAM;
    }
    if (licp_get_u16(rec) != LICP_FHSS_MAGIC) {
        return LICP_ERR_FORMAT;
    }
    memset(out, 0, sizeof(*out));
    out->flags = rec[2];
    out->datarate = rec[3];
    out->len = rec[4];
    out->freq_hz = licp_get_u32(&rec[6]);
    out->rssi_x10 = (int16_t)licp_get_u16(&rec[10]);
    out->snr_x10 = (int16_t)licp_get_u16(&rec[12]);
    out->gnss_ms = licp_get_u64(&rec[14]);
    if (out->len > LICP_FRAME_MAX_LEN) {
        return LICP_ERR_FORMAT;
    }
    memcpy(out->phy, &rec[22], out->len);
    return LICP_OK;
}

/** @copydoc licp_fhss_encode_record */
void licp_fhss_encode_record(const licp_rx_frame_t *in, uint8_t rec[LICP_FHSS_RECORD_LEN])
{
    memset(rec, 0, LICP_FHSS_RECORD_LEN);
    licp_put_u16(rec, LICP_FHSS_MAGIC);
    rec[2] = in->flags;
    rec[3] = in->datarate;
    rec[4] = (uint8_t)in->len;
    licp_put_u32(&rec[6], in->freq_hz);
    licp_put_u16(&rec[10], (uint16_t)in->rssi_x10);
    licp_put_u16(&rec[12], (uint16_t)in->snr_x10);
    licp_put_u64(&rec[14], in->gnss_ms);
    memcpy(&rec[22], in->phy, in->len);
}

/** @brief Open the receiver device in non-blocking mode. */
static licp_status_t fhss_start(void *ctx)
{
    licp_radio_fhss_t *f = (licp_radio_fhss_t *)ctx;
    f->fd = open(f->path, O_RDONLY | O_NONBLOCK);
    if (f->fd < 0) {
        LICP_LOGE(TAG, "cannot open %s", f->path);
        return LICP_ERR_IO;
    }
    f->have = 0;
    return LICP_OK;
}

/** @brief Read whole records; a partial record is kept for the next call. */
static size_t fhss_receive(void *ctx, licp_rx_frame_t *out, size_t max)
{
    licp_radio_fhss_t *f = (licp_radio_fhss_t *)ctx;
    size_t n = 0;
    if (f->fd < 0) {
        return 0;
    }
    while (n < max) {
        ssize_t r = read(f->fd, &f->partial[f->have], LICP_FHSS_RECORD_LEN - f->have);
        if (r < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                f->st.rx_errors++;
            }
            break;
        }
        if (r == 0) {
            break; /* no data (or end of file in tests) */
        }
        f->have += (size_t)r;
        if (f->have < LICP_FHSS_RECORD_LEN) {
            continue;
        }
        f->have = 0;
        if (licp_fhss_decode_record(f->partial, &out[n]) == LICP_OK) {
            ++n;
            f->st.rx_frames++;
        } else {
            f->st.rx_errors++;
        }
    }
    return n;
}

/** @brief Not supported: the receiver cannot transmit. */
static licp_status_t fhss_transmit(void *ctx, const licp_tx_request_t *req)
{
    (void)ctx;
    (void)req;
    return LICP_ERR_UNSUPPORTED;
}

/** @brief Copy statistics. */
static void fhss_stats(void *ctx, licp_radio_stats_t *st)
{
    *st = ((licp_radio_fhss_t *)ctx)->st;
}

/** @brief Close the device. */
static void fhss_stop(void *ctx)
{
    licp_radio_fhss_t *f = (licp_radio_fhss_t *)ctx;
    if (f->fd >= 0) {
        (void)close(f->fd);
        f->fd = -1;
    }
}

const licp_radio_ops_t licp_radio_fhss_ops = {"fhss", fhss_start, fhss_receive, fhss_transmit, fhss_stats,
                                              fhss_stop};

licp_status_t licp_radio_fhss_init(licp_radio_fhss_t *f, const char *path, licp_radio_t *radio)
{
    if (f == NULL || path == NULL || radio == NULL || strlen(path) >= sizeof(f->path)) {
        return LICP_ERR_PARAM;
    }
    memset(f, 0, sizeof(*f));
    memcpy(f->path, path, strlen(path) + 1u);
    f->fd = -1;
    radio->ops = &licp_radio_fhss_ops;
    radio->ctx = f;
    return LICP_OK;
}
