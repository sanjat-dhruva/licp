/**
 * @file licp_plan.h
 * @brief Upload plan sent by the central system at each ground-station contact.
 *
 * Layout (little-endian):
 * @verbatim
 *  magic "LPLN"(4) | version(1) | plan_seq(4) | entry_count(2) | entries... | crc32(4)
 *  entry = type(1) | len(2) | value(len)
 * @endverbatim
 *
 * | Type | Name            | Value                                                                 |
 * |------|-----------------|-----------------------------------------------------------------------|
 * | 0x01 | DEVICE_UPSERT   | dev_addr(4) key(16) home_ch(1) home_slot(1) prio(1) short_id(2) fcnt(4) fcnt_valid(1) flags(1) = 31 |
 * | 0x02 | DEVICE_REMOVE   | dev_addr(4)                                                           |
 * | 0x03 | ORBIT           | 26-byte orbit record                                                  |
 * | 0x04 | BROADCAST       | group(1) frame(1..255)                                                |
 * | 0x05 | UNICAST         | dev_addr(4) short_id(2) frame(1..255)                                 |
 * | 0x06 | GROUP_KEY       | 16-byte key used to sign beacons and bitmaps                          |
 * | 0x07 | PARAM           | id(1) value(4)                                                        |
 * | 0x80+| extension       | handled by ::licp_plan_handlers_t::on_unknown, or skipped             |
 *
 * The whole plan is checked (magic, version, checksum, every entry length)
 * before any handler is called, so a corrupted plan changes nothing.
 *
 * @ingroup core
 */
#ifndef LICP_CORE_PLAN_H
#define LICP_CORE_PLAN_H

#include <stddef.h>
#include <stdint.h>

#include "licp/common/licp_status.h"
#include "licp/core/licp_device_table.h"
#include "licp/proto/licp_orbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup core
 *  @{ */

#define LICP_PLAN_VERSION 1u /**< Supported plan layout. */

/** @brief Plan entry types. */
typedef enum {
    LICP_PLAN_DEVICE_UPSERT = 0x01, /**< Add or update a device. */
    LICP_PLAN_DEVICE_REMOVE = 0x02, /**< Remove a device. */
    LICP_PLAN_ORBIT         = 0x03, /**< Orbit record for the beacon rotation. */
    LICP_PLAN_BROADCAST     = 0x04, /**< Pre-built broadcast frame. */
    LICP_PLAN_UNICAST       = 0x05, /**< Pre-built one-to-one frame. */
    LICP_PLAN_GROUP_KEY     = 0x06, /**< Beacon and bitmap signing key. */
    LICP_PLAN_PARAM         = 0x07  /**< Numeric parameter update. */
} licp_plan_type_t;

/** @brief Parameter identifiers for ::LICP_PLAN_PARAM. */
typedef enum {
    LICP_PARAM_BITMAP_SIZE  = 1, /**< Bits in the acknowledgement bitmap. */
    LICP_PARAM_MAX_FCNT_GAP = 2, /**< Largest accepted counter jump. */
    LICP_PARAM_LOG_LEVEL    = 3  /**< Logger level (0..3). */
} licp_param_id_t;

/**
 * @brief Callbacks invoked for each entry, in plan order. Any may be NULL.
 *        Returning an error stops processing and is returned by ::licp_plan_apply.
 */
typedef struct {
    licp_status_t (*on_device_upsert)(void *user, const licp_device_t *d);           /**< DEVICE_UPSERT. */
    licp_status_t (*on_device_remove)(void *user, uint32_t dev_addr);               /**< DEVICE_REMOVE. */
    licp_status_t (*on_orbit)(void *user, const licp_orbit_t *o);                   /**< ORBIT. */
    licp_status_t (*on_broadcast)(void *user, uint8_t group, const uint8_t *frame, size_t len); /**< BROADCAST. */
    licp_status_t (*on_unicast)(void *user, uint32_t dev_addr, uint16_t short_id, const uint8_t *frame,
                                size_t len);                                        /**< UNICAST. */
    licp_status_t (*on_group_key)(void *user, const uint8_t key[16]);              /**< GROUP_KEY. */
    licp_status_t (*on_param)(void *user, uint8_t id, uint32_t value);             /**< PARAM. */
    licp_status_t (*on_unknown)(void *user, uint8_t type, const uint8_t *value, size_t len); /**< Other types. */
    void *user; /**< Passed to every callback. */
} licp_plan_handlers_t;

/**
 * @brief Validate a plan without applying it.
 * @param buf Plan bytes.
 * @param len Plan length.
 * @param[out] plan_seq Plan sequence number (may be NULL).
 * @return ::LICP_OK, ::LICP_ERR_LENGTH, ::LICP_ERR_FORMAT or ::LICP_ERR_CRC.
 */
licp_status_t licp_plan_validate(const uint8_t *buf, size_t len, uint32_t *plan_seq);

/**
 * @brief Validate, then call the handlers for each entry.
 * @param buf Plan bytes.
 * @param len Plan length.
 * @param h Handlers.
 * @param[out] plan_seq Plan sequence number (may be NULL).
 * @return First error from validation or from a handler.
 */
licp_status_t licp_plan_apply(const uint8_t *buf, size_t len, const licp_plan_handlers_t *h, uint32_t *plan_seq);

/**
 * @brief Plan builder for ground tools and tests. Call ::licp_plan_begin,
 *        any number of ::licp_plan_add, then ::licp_plan_end.
 */
typedef struct {
    uint8_t *buf;     /**< Output buffer. */
    size_t   cap;     /**< Capacity. */
    size_t   len;     /**< Bytes written. */
    uint16_t entries; /**< Entries added. */
    int      ok;      /**< Non-zero while no overflow happened. */
} licp_plan_builder_t;

/** @brief Start a plan. */
void licp_plan_begin(licp_plan_builder_t *b, uint8_t *buf, size_t cap, uint32_t plan_seq);

/** @brief Add a raw entry. */
void licp_plan_add(licp_plan_builder_t *b, uint8_t type, const uint8_t *value, uint16_t len);

/** @brief Add a device entry (encodes the 31-byte value). */
void licp_plan_add_device(licp_plan_builder_t *b, const licp_device_t *d);

/** @brief Finish: write the count and checksum. @return Plan length, 0 on overflow. */
size_t licp_plan_end(licp_plan_builder_t *b);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_CORE_PLAN_H */
