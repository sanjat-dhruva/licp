/**
 * @file licp_beacon.h
 * @brief Satellite beacon and acknowledgement-bitmap broadcast frames.
 *
 * Both are Long Range Wide Area Network proprietary frames (message type 7)
 * so that standard devices and gateways ignore them. They are authenticated
 * with a 4-byte AES-CMAC tag computed with a payload group key.
 *
 * Common header:
 * @verbatim
 *  MHDR=0xE0 (1) | kind (1) | version (1) | ... | MIC (4)
 * @endverbatim
 *
 * Beacon (kind ::LICP_PFRAME_BEACON):
 * @verbatim
 *  sat_id(1) | superframe(4) | gnss_s(4) | gnss_ms(2) | p[4](4) | sections... | MIC(4)
 *  section = type(1) | len(1) | value(len)
 * @endverbatim
 * Section types are listed in ::licp_beacon_section_type_t. Decoders skip
 * section types they do not know, so new sections can be added without
 * breaking deployed devices.
 *
 * Acknowledgement bitmap (kind ::LICP_PFRAME_ACK_BITMAP):
 * @verbatim
 *  sat_id(1) | pass_id(2) | first_index(2) | bit_count(2) | bits(ceil(bit_count/8)) | MIC(4)
 * @endverbatim
 *
 * @ingroup proto
 */
#ifndef LICP_PROTO_BEACON_H
#define LICP_PROTO_BEACON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "licp/common/licp_status.h"
#include "licp/proto/licp_bitmap.h"
#include "licp/proto/licp_orbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup proto
 *  @{ */

#define LICP_PFRAME_MHDR        0xE0u /**< MAC header of a proprietary frame. */
#define LICP_PFRAME_VERSION     1u    /**< Current layout version. */
#define LICP_BEACON_MAX_ACKS    16u   /**< Emergency acknowledgements per beacon. */
#define LICP_BEACON_MAX_CALLINS 8u    /**< Call-in notices per beacon. */
#define LICP_BEACON_MAX_GROUPS  4u    /**< Broadcast notices per beacon. */
#define LICP_BEACON_MAX_EXTRA   4u    /**< Caller-supplied extension sections. */
#define LICP_PRIORITY_CLASSES   4u    /**< Priority classes (0 = emergency). */

/** @brief Kind byte of a proprietary frame. */
typedef enum {
    LICP_PFRAME_BEACON     = 0x01, /**< Satellite beacon. */
    LICP_PFRAME_ACK_BITMAP = 0x02  /**< Acknowledgement bitmap for a pass. */
} licp_pframe_kind_t;

/** @brief Beacon section types. */
typedef enum {
    LICP_BSEC_SOS_ACK   = 0x01, /**< List of short identifiers (2 bytes each) whose emergency frame was stored. */
    LICP_BSEC_CALL_IN   = 0x02, /**< List of short identifiers called in for a one-to-one message. */
    LICP_BSEC_BROADCAST = 0x03, /**< List of multicast group numbers (1 byte each) with a broadcast following. */
    LICP_BSEC_ORBIT     = 0x04, /**< One 26-byte orbit record. */
    LICP_BSEC_EXT_FIRST = 0x80  /**< First type reserved for project extensions. */
} licp_beacon_section_type_t;

/** @brief Extension section supplied by a feature module. */
typedef struct {
    uint8_t        type; /**< Section type (>= ::LICP_BSEC_EXT_FIRST recommended). */
    uint8_t        len;  /**< Value length. */
    const uint8_t *data; /**< Value bytes. */
} licp_beacon_extra_t;

/** @brief Content of one beacon. */
typedef struct {
    uint8_t  sat_id;                              /**< Satellite identifier. */
    uint32_t superframe;                          /**< Superframe number. */
    uint32_t gnss_s;                              /**< Satellite navigation time at beacon start, seconds. */
    uint16_t gnss_ms;                             /**< Milliseconds part. */
    uint8_t  access_p[LICP_PRIORITY_CLASSES];     /**< Access probability per class, 0..255 = 0..1. */
    uint16_t sos_acks[LICP_BEACON_MAX_ACKS];      /**< Short identifiers acknowledged. */
    uint8_t  sos_ack_count;                       /**< Entries used. */
    uint16_t call_ins[LICP_BEACON_MAX_CALLINS];   /**< Short identifiers called in. */
    uint8_t  call_in_count;                       /**< Entries used. */
    uint8_t  broadcast_groups[LICP_BEACON_MAX_GROUPS]; /**< Groups with a broadcast following. */
    uint8_t  broadcast_count;                     /**< Entries used. */
    bool     has_orbit;                           /**< True if @ref orbit is included. */
    licp_orbit_t orbit;                           /**< Orbit record in rotation. */
    licp_beacon_extra_t extra[LICP_BEACON_MAX_EXTRA]; /**< Extension sections (encode only). */
    uint8_t  extra_count;                         /**< Entries used. */
} licp_beacon_t;

/** @brief What the encoder could not fit, so the caller can carry it to the next beacon. */
typedef struct {
    uint8_t sos_acks_sent;   /**< Emergency acknowledgements actually encoded. */
    uint8_t call_ins_sent;   /**< Call-ins actually encoded. */
    bool    broadcast_sent;  /**< Broadcast notice encoded. */
    bool    orbit_sent;      /**< Orbit record encoded. */
    uint8_t extra_sent;      /**< Extension sections encoded. */
} licp_beacon_fit_t;

/**
 * @brief Encode a beacon, fitting sections in priority order
 *        (emergency acknowledgements, call-ins, broadcast notices, orbit, extensions).
 *
 * Emergency acknowledgements and call-ins are truncated to what fits; other
 * sections are included whole or not at all.
 *
 * @param b        Beacon content.
 * @param key      16-byte group key for the tag.
 * @param max_len  Largest frame length allowed (downlink payload limit).
 * @param out      Output buffer.
 * @param cap      Capacity of @p out.
 * @param[out] out_len Frame length.
 * @param[out] fit  What was included (may be NULL).
 * @return ::LICP_OK, ::LICP_ERR_PARAM or ::LICP_ERR_LENGTH if even the fixed part does not fit.
 */
licp_status_t licp_beacon_encode(const licp_beacon_t *b, const uint8_t key[16], size_t max_len, uint8_t *out,
                                 size_t cap, size_t *out_len, licp_beacon_fit_t *fit);

/**
 * @brief Decode and authenticate a beacon (used by tests, simulator and device firmware).
 * @return ::LICP_OK, ::LICP_ERR_MIC, ::LICP_ERR_FORMAT or ::LICP_ERR_LENGTH.
 *         Unknown section types are skipped; extension sections are not returned.
 */
licp_status_t licp_beacon_decode(const uint8_t *buf, size_t len, const uint8_t key[16], licp_beacon_t *b);

/**
 * @brief Encode an acknowledgement-bitmap broadcast covering bits [first, first+count).
 * @return ::LICP_OK, ::LICP_ERR_PARAM or ::LICP_ERR_LENGTH.
 */
licp_status_t licp_ackmap_encode(uint8_t sat_id, const licp_bitmap_t *bm, uint16_t first, uint16_t count,
                                 const uint8_t key[16], uint8_t *out, size_t cap, size_t *out_len);

/**
 * @brief Decode an acknowledgement-bitmap broadcast into @p bm (bits outside the frame stay clear).
 * @param buf Frame bytes.
 * @param len Frame length.
 * @param key 16-byte group key.
 * @param[out] sat_id Satellite identifier.
 * @param[out] bm Bitmap (sized to first + count).
 * @param[out] first First index covered.
 * @param[out] count Number of bits covered.
 */
licp_status_t licp_ackmap_decode(const uint8_t *buf, size_t len, const uint8_t key[16], uint8_t *sat_id,
                                 licp_bitmap_t *bm, uint16_t *first, uint16_t *count);

/** @brief Convert a probability 0.0..1.0 to the 0..255 wire value. */
uint8_t licp_prob_to_u8(double p);

/** @brief Convert a 0..255 wire value to a probability. */
double licp_prob_from_u8(uint8_t v);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_PROTO_BEACON_H */
