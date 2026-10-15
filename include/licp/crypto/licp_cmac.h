/**
 * @file licp_cmac.h
 * @brief AES-CMAC (RFC 4493 / NIST SP 800-38B).
 *
 * The Long Range Wide Area Network message integrity code is the first four
 * bytes of AES-CMAC computed over a 16-byte header block followed by the
 * message. The payload also uses CMAC to authenticate its own beacon and
 * broadcast frames.
 *
 * @ingroup crypto
 */
#ifndef LICP_CRYPTO_CMAC_H
#define LICP_CRYPTO_CMAC_H

#include <stddef.h>
#include <stdint.h>

#include "licp/crypto/licp_aes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup crypto
 *  @{ */

/** @brief Incremental CMAC state. */
typedef struct {
    licp_aes_ctx_t aes;                    /**< Expanded key. */
    uint8_t        k1[LICP_AES_BLOCK_SIZE]; /**< First subkey. */
    uint8_t        k2[LICP_AES_BLOCK_SIZE]; /**< Second subkey. */
    uint8_t        x[LICP_AES_BLOCK_SIZE];  /**< Running chaining value. */
    uint8_t        buf[LICP_AES_BLOCK_SIZE];/**< Pending partial block. */
    size_t         buf_len;                 /**< Bytes held in @ref buf. */
} licp_cmac_ctx_t;

/** @brief Start a CMAC computation with a 16-byte key. */
void licp_cmac_init(licp_cmac_ctx_t *ctx, const uint8_t key[LICP_AES_KEY_SIZE]);

/** @brief Add @p len bytes of message. May be called repeatedly. */
void licp_cmac_update(licp_cmac_ctx_t *ctx, const uint8_t *data, size_t len);

/**
 * @brief Finish and write the 16-byte tag. The context is wiped afterwards.
 * @param ctx CMAC state.
 * @param tag Output tag.
 */
void licp_cmac_final(licp_cmac_ctx_t *ctx, uint8_t tag[LICP_AES_BLOCK_SIZE]);

/**
 * @brief One-shot CMAC.
 * @param key  16-byte key.
 * @param data Message (may be NULL if @p len is 0).
 * @param len  Message length.
 * @param tag  16-byte output tag.
 */
void licp_cmac(const uint8_t key[LICP_AES_KEY_SIZE], const uint8_t *data, size_t len,
               uint8_t tag[LICP_AES_BLOCK_SIZE]);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_CRYPTO_CMAC_H */
