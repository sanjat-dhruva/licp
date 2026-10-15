/**
 * @file licp_cmac.c
 * @brief AES-CMAC implementation following RFC 4493 section 2.
 * @ingroup crypto
 */
#include "licp/crypto/licp_cmac.h"

#include <string.h>

/** Left-shift a 16-byte block by one bit; XOR 0x87 if the top bit was set. */
static void double_block(const uint8_t in[16], uint8_t out[16])
{
    uint8_t carry = 0;
    for (int i = 15; i >= 0; --i) {
        uint8_t b = in[i];
        out[i] = (uint8_t)((uint8_t)(b << 1) | carry);
        carry = (uint8_t)(b >> 7);
    }
    if (carry != 0u) {
        out[15] ^= 0x87u;
    }
}

void licp_cmac_init(licp_cmac_ctx_t *ctx, const uint8_t key[LICP_AES_KEY_SIZE])
{
    uint8_t l[16] = {0};
    licp_aes_init(&ctx->aes, key);
    licp_aes_encrypt(&ctx->aes, l, l);
    double_block(l, ctx->k1);
    double_block(ctx->k1, ctx->k2);
    memset(ctx->x, 0, sizeof(ctx->x));
    ctx->buf_len = 0;
}

void licp_cmac_update(licp_cmac_ctx_t *ctx, const uint8_t *data, size_t len)
{
    while (len > 0u) {
        /* Keep the last (possibly full) block buffered: it is processed in final. */
        if (ctx->buf_len == LICP_AES_BLOCK_SIZE) {
            for (unsigned i = 0; i < 16; ++i) {
                ctx->x[i] ^= ctx->buf[i];
            }
            licp_aes_encrypt(&ctx->aes, ctx->x, ctx->x);
            ctx->buf_len = 0;
        }
        size_t take = LICP_AES_BLOCK_SIZE - ctx->buf_len;
        if (take > len) {
            take = len;
        }
        memcpy(&ctx->buf[ctx->buf_len], data, take);
        ctx->buf_len += take;
        data += take;
        len -= take;
    }
}

void licp_cmac_final(licp_cmac_ctx_t *ctx, uint8_t tag[LICP_AES_BLOCK_SIZE])
{
    uint8_t last[16];
    if (ctx->buf_len == LICP_AES_BLOCK_SIZE) {
        for (unsigned i = 0; i < 16; ++i) {
            last[i] = (uint8_t)(ctx->buf[i] ^ ctx->k1[i]);
        }
    } else {
        memset(last, 0, sizeof(last));
        memcpy(last, ctx->buf, ctx->buf_len);
        last[ctx->buf_len] = 0x80u;
        for (unsigned i = 0; i < 16; ++i) {
            last[i] ^= ctx->k2[i];
        }
    }
    for (unsigned i = 0; i < 16; ++i) {
        ctx->x[i] ^= last[i];
    }
    licp_aes_encrypt(&ctx->aes, ctx->x, tag);

    licp_aes_wipe(&ctx->aes);
    memset(ctx->k1, 0, sizeof(ctx->k1));
    memset(ctx->k2, 0, sizeof(ctx->k2));
    memset(ctx->x, 0, sizeof(ctx->x));
    memset(ctx->buf, 0, sizeof(ctx->buf));
    ctx->buf_len = 0;
}

void licp_cmac(const uint8_t key[LICP_AES_KEY_SIZE], const uint8_t *data, size_t len,
               uint8_t tag[LICP_AES_BLOCK_SIZE])
{
    licp_cmac_ctx_t ctx;
    licp_cmac_init(&ctx, key);
    if (len > 0u) {
        licp_cmac_update(&ctx, data, len);
    }
    licp_cmac_final(&ctx, tag);
}
