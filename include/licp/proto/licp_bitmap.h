/**
 * @file licp_bitmap.h
 * @brief Acknowledgement bitmap for one pass: one bit per device.
 *
 * Each device is given a bit index (its "short identifier") by the central
 * system. The payload sets the bit when it stores a routine frame from that
 * device; the bitmap is broadcast in the last beacon window of the pass.
 * About 13 bytes cover 100 devices.
 *
 * @ingroup proto
 */
#ifndef LICP_PROTO_BITMAP_H
#define LICP_PROTO_BITMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "licp/common/licp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup proto
 *  @{ */

#define LICP_BITMAP_MAX_BITS 4096u /**< Largest number of devices per bitmap. */

/** @brief Fixed-size acknowledgement bitmap. */
typedef struct {
    uint8_t  bits[LICP_BITMAP_MAX_BITS / 8u]; /**< Bit i = device with short identifier i. */
    uint16_t size;                            /**< Number of bits in use. */
    uint16_t pass_id;                         /**< Pass this bitmap belongs to. */
} licp_bitmap_t;

/** @brief Clear all bits and set the size (at most ::LICP_BITMAP_MAX_BITS). */
licp_status_t licp_bitmap_reset(licp_bitmap_t *bm, uint16_t size, uint16_t pass_id);

/** @brief Set bit @p index. @return ::LICP_ERR_PARAM if out of range. */
licp_status_t licp_bitmap_set(licp_bitmap_t *bm, uint16_t index);

/** @brief Test bit @p index (false if out of range). */
bool licp_bitmap_test(const licp_bitmap_t *bm, uint16_t index);

/** @brief Number of bits set. */
uint16_t licp_bitmap_count(const licp_bitmap_t *bm);

/**
 * @brief Copy bits [first, first+count) into @p out, packed LSB first.
 * @return Bytes written, 0 on error.
 */
size_t licp_bitmap_extract(const licp_bitmap_t *bm, uint16_t first, uint16_t count, uint8_t *out, size_t cap);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_PROTO_BITMAP_H */
