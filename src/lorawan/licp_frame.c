/**
 * @file licp_frame.c
 * @brief Implementation of the Long Range Wide Area Network frame helpers.
 * @ingroup lorawan
 */
#include "licp/lorawan/licp_frame.h"

#include <string.h>

#include "licp/common/licp_bytes.h"
#include "licp/crypto/licp_aes.h"
#include "licp/crypto/licp_cmac.h"

licp_status_t licp_frame_peek_mtype(const uint8_t *buf, size_t len, licp_mtype_t *mtype)
{
    if (buf == NULL || mtype == NULL) {
        return LICP_ERR_PARAM;
    }
    if (len == 0u) {
        return LICP_ERR_LENGTH;
    }
    *mtype = (licp_mtype_t)((buf[0] >> 5) & 0x07u);
    return LICP_OK;
}

/** @brief True for the four data message types. */
static bool is_data_type(licp_mtype_t t)
{
    return t == LICP_MTYPE_UNCONF_DATA_UP || t == LICP_MTYPE_UNCONF_DATA_DOWN ||
           t == LICP_MTYPE_CONF_DATA_UP || t == LICP_MTYPE_CONF_DATA_DOWN;
}

licp_status_t licp_frame_parse(const uint8_t *buf, size_t len, licp_frame_t *out)
{
    if (buf == NULL || out == NULL) {
        return LICP_ERR_PARAM;
    }
    memset(out, 0, sizeof(*out));
    if (len < LICP_FRAME_MIN_DATA || len > LICP_FRAME_MAX_LEN) {
        return LICP_ERR_LENGTH;
    }

    out->mtype = (licp_mtype_t)((buf[0] >> 5) & 0x07u);
    out->major = (uint8_t)(buf[0] & 0x03u);
    if (!is_data_type(out->mtype)) {
        return LICP_ERR_FORMAT;
    }
    if (out->major != 0u) {
        return LICP_ERR_FORMAT;
    }

    out->dev_addr = licp_get_u32(&buf[1]);
    out->fctrl = buf[5];
    out->adr = (out->fctrl & 0x80u) != 0u;
    out->ack = (out->fctrl & 0x20u) != 0u;
    out->fcnt16 = licp_get_u16(&buf[6]);
    out->fopts_len = (uint8_t)(out->fctrl & 0x0Fu);

    size_t pos = 8;
    size_t body_end = len - LICP_FRAME_MIC_LEN;
    if (pos + out->fopts_len > body_end) {
        return LICP_ERR_LENGTH;
    }
    out->fopts = (out->fopts_len > 0u) ? &buf[pos] : NULL;
    pos += out->fopts_len;

    if (pos < body_end) {
        out->has_port = true;
        out->fport = buf[pos++];
        out->payload = (pos < body_end) ? &buf[pos] : NULL;
        out->payload_len = body_end - pos;
        /* Port 0 carries MAC commands in the payload; FOpts must then be empty. */
        if (out->fport == 0u && out->fopts_len > 0u) {
            return LICP_ERR_FORMAT;
        }
    }

    out->mic = licp_get_u32(&buf[body_end]);
    out->raw = buf;
    out->raw_len = len;
    return LICP_OK;
}

/** @copydoc licp_frame_compute_mic */
uint32_t licp_frame_compute_mic(const uint8_t key[LICP_KEY_LEN], licp_dir_t dir, uint32_t dev_addr,
                                uint32_t fcnt32, const uint8_t *msg, size_t msg_len)
{
    uint8_t b0[16] = {0};
    b0[0] = 0x49u;
    b0[5] = (uint8_t)dir;
    licp_put_u32(&b0[6], dev_addr);
    licp_put_u32(&b0[10], fcnt32);
    b0[15] = (uint8_t)(msg_len & 0xFFu);

    licp_cmac_ctx_t ctx;
    uint8_t tag[16];
    licp_cmac_init(&ctx, key);
    licp_cmac_update(&ctx, b0, sizeof(b0));
    licp_cmac_update(&ctx, msg, msg_len);
    licp_cmac_final(&ctx, tag);
    return licp_get_u32(tag);
}

/** @copydoc licp_frame_verify_mic */
licp_status_t licp_frame_verify_mic(const licp_frame_t *frame, const uint8_t key[LICP_KEY_LEN], uint32_t fcnt32)
{
    if (frame == NULL || key == NULL || frame->raw == NULL || frame->raw_len < LICP_FRAME_MIN_DATA) {
        return LICP_ERR_PARAM;
    }
    licp_dir_t dir = (frame->mtype == LICP_MTYPE_UNCONF_DATA_UP || frame->mtype == LICP_MTYPE_CONF_DATA_UP)
                         ? LICP_DIR_UP
                         : LICP_DIR_DOWN;
    uint32_t mic = licp_frame_compute_mic(key, dir, frame->dev_addr, fcnt32, frame->raw,
                                          frame->raw_len - LICP_FRAME_MIC_LEN);
    return (mic == frame->mic) ? LICP_OK : LICP_ERR_MIC;
}

uint32_t licp_frame_rebuild_fcnt(uint32_t last_fcnt32, uint16_t fcnt16)
{
    uint32_t candidate = (last_fcnt32 & 0xFFFF0000u) | fcnt16;
    if (candidate < last_fcnt32) {
        candidate += 0x10000u;
    }
    return candidate;
}

/** @copydoc licp_frame_crypt_payload */
void licp_frame_crypt_payload(const uint8_t key[LICP_KEY_LEN], licp_dir_t dir, uint32_t dev_addr,
                              uint32_t fcnt32, uint8_t *data, size_t len)
{
    licp_aes_ctx_t aes;
    licp_aes_init(&aes, key);
    uint8_t a[16] = {0};
    uint8_t s[16];
    a[0] = 0x01u;
    a[5] = (uint8_t)dir;
    licp_put_u32(&a[6], dev_addr);
    licp_put_u32(&a[10], fcnt32);

    size_t pos = 0;
    uint8_t block = 1;
    while (pos < len) {
        a[15] = block++;
        licp_aes_encrypt(&aes, a, s);
        for (size_t i = 0; i < 16u && pos < len; ++i, ++pos) {
            data[pos] ^= s[i];
        }
    }
    licp_aes_wipe(&aes);
}

/** @copydoc licp_frame_build_data */
licp_status_t licp_frame_build_data(licp_mtype_t mtype, uint32_t dev_addr, uint8_t fctrl, uint32_t fcnt32,
                                    const uint8_t *fopts, uint8_t fopts_len, uint8_t fport, bool with_port,
                                    const uint8_t *plain, size_t plain_len, const uint8_t nwk_key[LICP_KEY_LEN],
                                    const uint8_t app_key[LICP_KEY_LEN], uint8_t *out, size_t out_cap,
                                    size_t *out_len)
{
    if (!is_data_type(mtype) || out == NULL || out_len == NULL || nwk_key == NULL || fopts_len > 15u ||
        (fctrl & 0x0Fu) != fopts_len || (fopts_len > 0u && fopts == NULL) || (plain_len > 0u && plain == NULL) ||
        (plain_len > 0u && !with_port) || (plain_len > 0u && app_key == NULL)) {
        return LICP_ERR_PARAM;
    }
    size_t total = 8u + fopts_len + (with_port ? 1u : 0u) + plain_len + LICP_FRAME_MIC_LEN;
    if (total > out_cap || total > LICP_FRAME_MAX_LEN) {
        return LICP_ERR_LENGTH;
    }
    licp_dir_t dir = (mtype == LICP_MTYPE_UNCONF_DATA_UP || mtype == LICP_MTYPE_CONF_DATA_UP) ? LICP_DIR_UP
                                                                                            : LICP_DIR_DOWN;
    size_t pos = 0;
    out[pos++] = (uint8_t)((unsigned)mtype << 5);
    licp_put_u32(&out[pos], dev_addr);
    pos += 4;
    out[pos++] = fctrl;
    licp_put_u16(&out[pos], (uint16_t)(fcnt32 & 0xFFFFu));
    pos += 2;
    if (fopts_len > 0u) {
        memcpy(&out[pos], fopts, fopts_len);
        pos += fopts_len;
    }
    if (with_port) {
        out[pos++] = fport;
        if (plain_len > 0u) {
            memcpy(&out[pos], plain, plain_len);
            licp_frame_crypt_payload(app_key, dir, dev_addr, fcnt32, &out[pos], plain_len);
            pos += plain_len;
        }
    }
    uint32_t mic = licp_frame_compute_mic(nwk_key, dir, dev_addr, fcnt32, out, pos);
    licp_put_u32(&out[pos], mic);
    pos += 4;
    *out_len = pos;
    return LICP_OK;
}
