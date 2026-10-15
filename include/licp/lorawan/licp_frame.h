/**
 * @file licp_frame.h
 * @brief Long Range Wide Area Network (version 1.0.x) frame parsing, message
 *        integrity code, frame-counter reconstruction and payload encryption.
 *
 * The payload only needs the network-side subset of the protocol:
 *  - parse uplink data frames,
 *  - verify their message integrity code with the device's network session key,
 *  - rebuild the 32-bit frame counter from the 16 bits carried on air,
 *  - build proprietary frames (the satellite beacon and broadcasts).
 *
 * Application payload decryption is provided for ground tools and tests; the
 * on-board service stores frames as received and never decrypts them.
 *
 * Wire layout of a data frame:
 * @verbatim
 *  MHDR(1) | DevAddr(4) | FCtrl(1) | FCnt(2) | FOpts(0..15) | [FPort(1) | FRMPayload(N)] | MIC(4)
 * @endverbatim
 *
 * @ingroup lorawan
 */
#ifndef LICP_LORAWAN_FRAME_H
#define LICP_LORAWAN_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "licp/common/licp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup lorawan Long Range Wide Area Network frames
 * @brief Frame parsing, integrity check and counter handling.
 * @{
 */

#define LICP_FRAME_MAX_LEN   255u /**< Largest physical payload handled. */
#define LICP_FRAME_MIN_DATA   12u /**< Smallest data frame: MHDR+FHDR(7)+MIC. */
#define LICP_FRAME_MIC_LEN     4u /**< Message integrity code length. */
#define LICP_KEY_LEN          16u /**< Session key length. */

/** @brief Message type, bits 7..5 of the MAC header. */
typedef enum {
    LICP_MTYPE_JOIN_REQUEST      = 0, /**< Join request (not handled on board). */
    LICP_MTYPE_JOIN_ACCEPT       = 1, /**< Join accept (never sent by the payload). */
    LICP_MTYPE_UNCONF_DATA_UP    = 2, /**< Unconfirmed uplink. */
    LICP_MTYPE_UNCONF_DATA_DOWN  = 3, /**< Unconfirmed downlink. */
    LICP_MTYPE_CONF_DATA_UP      = 4, /**< Confirmed uplink. */
    LICP_MTYPE_CONF_DATA_DOWN    = 5, /**< Confirmed downlink. */
    LICP_MTYPE_REJOIN_REQUEST    = 6, /**< Rejoin request (version 1.1, not handled). */
    LICP_MTYPE_PROPRIETARY       = 7  /**< Proprietary: used for beacon and broadcasts. */
} licp_mtype_t;

/** @brief Direction used in the integrity and encryption blocks. */
typedef enum {
    LICP_DIR_UP   = 0, /**< Device to network. */
    LICP_DIR_DOWN = 1  /**< Network to device. */
} licp_dir_t;

/** @brief Parsed view of a data frame. Pointers refer into the caller's buffer. */
typedef struct {
    licp_mtype_t   mtype;        /**< Message type. */
    uint8_t        major;        /**< Major version bits (0 = version 1). */
    uint32_t       dev_addr;     /**< Device address. */
    uint8_t        fctrl;        /**< Frame control byte. */
    bool           adr;          /**< Adaptive data rate flag. */
    bool           ack;          /**< Acknowledgement flag. */
    uint16_t       fcnt16;       /**< 16-bit frame counter as sent. */
    const uint8_t *fopts;        /**< MAC commands in the header (may be NULL). */
    uint8_t        fopts_len;    /**< Length of @ref fopts. */
    bool           has_port;     /**< True if FPort is present. */
    uint8_t        fport;        /**< Port number. */
    const uint8_t *payload;      /**< Encrypted application payload (may be NULL). */
    size_t         payload_len;  /**< Length of @ref payload. */
    uint32_t       mic;          /**< Received integrity code (little-endian on air). */
    const uint8_t *raw;          /**< Whole frame. */
    size_t         raw_len;      /**< Length of @ref raw. */
} licp_frame_t;

/**
 * @brief Read the message type of a frame without parsing the rest.
 * @param buf Frame bytes (at least 1).
 * @param len Frame length.
 * @param[out] mtype Message type.
 * @return ::LICP_OK, or ::LICP_ERR_LENGTH if @p len is 0.
 */
licp_status_t licp_frame_peek_mtype(const uint8_t *buf, size_t len, licp_mtype_t *mtype);

/**
 * @brief Parse an uplink or downlink data frame.
 * @param buf Frame bytes; must outlive @p out.
 * @param len Frame length.
 * @param[out] out Parsed view.
 * @return ::LICP_OK, ::LICP_ERR_LENGTH if too short or inconsistent,
 *         ::LICP_ERR_FORMAT if not a data frame or the major version is not 0.
 */
licp_status_t licp_frame_parse(const uint8_t *buf, size_t len, licp_frame_t *out);

/**
 * @brief Compute the 4-byte integrity code of a data frame (version 1.0.x).
 *
 * Block B0 = 0x49 | 0x00 x4 | dir | DevAddr | FCnt32 | 0x00 | len(msg),
 * MIC = first 4 bytes of AES-CMAC(key, B0 | msg).
 *
 * @param key      Network session key.
 * @param dir      Direction.
 * @param dev_addr Device address.
 * @param fcnt32   Full 32-bit frame counter.
 * @param msg      Frame without its trailing MIC.
 * @param msg_len  Length of @p msg (at most 255).
 * @return The integrity code as a little-endian 32-bit value (the order it appears on air).
 */
uint32_t licp_frame_compute_mic(const uint8_t key[LICP_KEY_LEN], licp_dir_t dir, uint32_t dev_addr,
                                uint32_t fcnt32, const uint8_t *msg, size_t msg_len);

/**
 * @brief Verify the integrity code of a parsed uplink against a 32-bit counter.
 * @return ::LICP_OK if it matches, ::LICP_ERR_MIC otherwise, ::LICP_ERR_PARAM on bad input.
 */
licp_status_t licp_frame_verify_mic(const licp_frame_t *frame, const uint8_t key[LICP_KEY_LEN], uint32_t fcnt32);

/**
 * @brief Rebuild a 32-bit frame counter from the 16 bits received.
 *
 * Takes the upper 16 bits from @p last_fcnt32. If the result is below
 * @p last_fcnt32, the device has rolled over the low half, so 0x10000 is
 * added. An equal value means a repeat of the last accepted frame.
 *
 * @param last_fcnt32 Last accepted 32-bit counter.
 * @param fcnt16      Counter bits received on air.
 * @return Candidate 32-bit counter.
 */
uint32_t licp_frame_rebuild_fcnt(uint32_t last_fcnt32, uint16_t fcnt16);

/**
 * @brief Encrypt or decrypt an application payload in place (AES-CTR-like,
 *        Long Range Wide Area Network version 1.0.x section 4.3.3).
 * @param key      Application key (port > 0) or network key (port 0).
 * @param dir      Direction.
 * @param dev_addr Device address.
 * @param fcnt32   32-bit frame counter.
 * @param data     Payload, transformed in place.
 * @param len      Payload length.
 */
void licp_frame_crypt_payload(const uint8_t key[LICP_KEY_LEN], licp_dir_t dir, uint32_t dev_addr,
                              uint32_t fcnt32, uint8_t *data, size_t len);

/**
 * @brief Build a data frame (ground tools, simulator and tests).
 *
 * Encrypts @p plain with @p app_key and appends the integrity code computed
 * with @p nwk_key.
 *
 * @param mtype    Data message type (2..5).
 * @param dev_addr Device address.
 * @param fctrl    Frame control byte; FOptsLen bits must match @p fopts_len.
 * @param fcnt32   32-bit counter (low 16 bits go on air).
 * @param fopts    MAC commands (may be NULL).
 * @param fopts_len Length of @p fopts (0..15).
 * @param fport    Port; ignored when @p plain_len is 0 and @p with_port is false.
 * @param with_port Whether to include FPort.
 * @param plain    Plain application payload.
 * @param plain_len Length of @p plain.
 * @param nwk_key  Network session key.
 * @param app_key  Application session key.
 * @param out      Output buffer.
 * @param out_cap  Capacity of @p out.
 * @param[out] out_len Frame length written.
 * @return ::LICP_OK or ::LICP_ERR_PARAM / ::LICP_ERR_LENGTH.
 */
licp_status_t licp_frame_build_data(licp_mtype_t mtype, uint32_t dev_addr, uint8_t fctrl, uint32_t fcnt32,
                                    const uint8_t *fopts, uint8_t fopts_len, uint8_t fport, bool with_port,
                                    const uint8_t *plain, size_t plain_len, const uint8_t nwk_key[LICP_KEY_LEN],
                                    const uint8_t app_key[LICP_KEY_LEN], uint8_t *out, size_t out_cap,
                                    size_t *out_len);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_LORAWAN_FRAME_H */
