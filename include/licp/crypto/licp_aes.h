/**
 * @file licp_aes.h
 * @brief AES-128 block encryption (FIPS-197).
 *
 * Only the forward (encrypt) direction is provided: message integrity codes
 * (CMAC) and Long Range Wide Area Network payload encryption both use the
 * block cipher in the forward direction only.
 *
 * @note This is a portable software implementation. It is not hardened
 *       against timing side channels; the payload is not exposed to an
 *       attacker who can time individual block operations.
 *
 * @ingroup crypto
 */
#ifndef LICP_CRYPTO_AES_H
#define LICP_CRYPTO_AES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup crypto Cryptography
 * @brief AES-128 and AES-CMAC used for integrity checks and encryption.
 * @{
 */

#define LICP_AES_BLOCK_SIZE 16u /**< AES block size in bytes. */
#define LICP_AES_KEY_SIZE   16u /**< AES-128 key size in bytes. */

/** @brief Expanded AES-128 key schedule (11 round keys). */
typedef struct {
    uint8_t round_key[176]; /**< 11 round keys of 16 bytes. */
} licp_aes_ctx_t;

/**
 * @brief Expand a 16-byte key into @p ctx.
 * @param ctx Context to fill.
 * @param key 16-byte key.
 */
void licp_aes_init(licp_aes_ctx_t *ctx, const uint8_t key[LICP_AES_KEY_SIZE]);

/**
 * @brief Encrypt one 16-byte block. @p in and @p out may be the same buffer.
 * @param ctx Expanded key.
 * @param in  Plaintext block.
 * @param out Ciphertext block.
 */
void licp_aes_encrypt(const licp_aes_ctx_t *ctx, const uint8_t in[LICP_AES_BLOCK_SIZE],
                      uint8_t out[LICP_AES_BLOCK_SIZE]);

/** @brief Overwrite the key schedule with zeros. */
void licp_aes_wipe(licp_aes_ctx_t *ctx);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_CRYPTO_AES_H */
