/**
 * @file licp_obc.h
 * @brief Message protocol between the payload and the on-board computer.
 *
 * The on-board computer relays these messages to and from the ground
 * station. Each message is framed on a byte stream (Ethernet socket or
 * serial line):
 * @verbatim
 *  sync 0xA5 0x5A | type(1) | seq(2) | len(2) | payload(len) | crc16(2)
 * @endverbatim
 * The checksum is CRC-16/CCITT over type, seq, len and payload.
 *
 * @note The exact transport and message set must be agreed in the interface
 *       control document with the satellite team. This is a working proposal.
 *
 * @ingroup obc
 */
#ifndef LICP_OBC_OBC_H
#define LICP_OBC_OBC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "licp/common/licp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup obc On-board computer link
 * @brief Framing, message types and a streaming decoder.
 * @{
 */

#define LICP_OBC_SYNC0       0xA5u   /**< First sync byte. */
#define LICP_OBC_SYNC1       0x5Au   /**< Second sync byte. */
#define LICP_OBC_HDR_LEN     7u      /**< Sync(2) type(1) seq(2) len(2). */
#define LICP_OBC_CRC_LEN     2u      /**< Trailer length. */
#define LICP_OBC_MAX_PAYLOAD 16384u  /**< Largest payload accepted. */

/** @brief Message types. */
typedef enum {
    LICP_OBC_PLAN_UPLOAD    = 0x01, /**< Ground to payload: upload plan (see licp_plan.h). */
    LICP_OBC_PLAN_RESULT    = 0x02, /**< Payload to ground: plan_seq(4) status(1, signed). */
    LICP_OBC_FRAMES_REQUEST = 0x03, /**< Ground to payload: max_frames(2). */
    LICP_OBC_FRAMES         = 0x04, /**< Payload to ground: count(2) then per frame seq(4) meta(28) len(1) phy(len). */
    LICP_OBC_RELEASE        = 0x05, /**< Ground to payload: count(2) seq(4) x count. */
    LICP_OBC_STATUS_REQUEST = 0x06, /**< Ground to payload: no payload. */
    LICP_OBC_STATUS         = 0x07, /**< Payload to ground: telemetry block. */
    LICP_OBC_CONTACT_END    = 0x08  /**< Ground to payload: contact over; unconfirmed frames go back to pending. */
} licp_obc_type_t;

/** @brief A decoded message. @ref payload points into the decoder's buffer. */
typedef struct {
    uint8_t        type;    /**< ::licp_obc_type_t. */
    uint16_t       seq;     /**< Sender's sequence number. */
    const uint8_t *payload; /**< Payload bytes. */
    uint16_t       len;     /**< Payload length. */
} licp_obc_msg_t;

/**
 * @brief Encode a message.
 * @return Bytes written, or 0 if @p cap is too small or @p len too large.
 */
size_t licp_obc_encode(uint8_t type, uint16_t seq, const uint8_t *payload, uint16_t len, uint8_t *out, size_t cap);

/** @brief Streaming decoder state. */
typedef struct {
    uint8_t  buf[LICP_OBC_HDR_LEN + LICP_OBC_MAX_PAYLOAD + LICP_OBC_CRC_LEN]; /**< Assembly buffer. */
    size_t   have;      /**< Bytes held. */
    size_t   need;      /**< Bytes needed for the current message. */
    uint32_t crc_errors;/**< Messages dropped for bad checksum. */
    uint32_t resyncs;   /**< Bytes skipped while looking for sync. */
} licp_obc_decoder_t;

/** @brief Reset the decoder. */
void licp_obc_decoder_init(licp_obc_decoder_t *d);

/**
 * @brief Feed bytes. Stops after one complete message.
 * @param d Decoder.
 * @param data Input bytes.
 * @param len Input length.
 * @param[out] consumed Bytes used from @p data.
 * @param[out] msg Message, valid until the next call.
 * @return true if @p msg holds a complete, checksummed message.
 */
bool licp_obc_decoder_feed(licp_obc_decoder_t *d, const uint8_t *data, size_t len, size_t *consumed,
                           licp_obc_msg_t *msg);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_OBC_OBC_H */
