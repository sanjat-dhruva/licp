/**
 * @file licp_bitmap.c
 * @brief Acknowledgement bitmap implementation.
 * @ingroup proto
 */
#include "licp/proto/licp_bitmap.h"

#include <string.h>

licp_status_t licp_bitmap_reset(licp_bitmap_t *bm, uint16_t size, uint16_t pass_id)
{
    if (bm == NULL || size > LICP_BITMAP_MAX_BITS) {
        return LICP_ERR_PARAM;
    }
    memset(bm->bits, 0, sizeof(bm->bits));
    bm->size = size;
    bm->pass_id = pass_id;
    return LICP_OK;
}

licp_status_t licp_bitmap_set(licp_bitmap_t *bm, uint16_t index)
{
    if (bm == NULL || index >= bm->size) {
        return LICP_ERR_PARAM;
    }
    bm->bits[index / 8u] = (uint8_t)(bm->bits[index / 8u] | (1u << (index % 8u)));
    return LICP_OK;
}

bool licp_bitmap_test(const licp_bitmap_t *bm, uint16_t index)
{
    if (bm == NULL || index >= bm->size) {
        return false;
    }
    return (bm->bits[index / 8u] & (1u << (index % 8u))) != 0u;
}

uint16_t licp_bitmap_count(const licp_bitmap_t *bm)
{
    uint16_t n = 0;
    if (bm == NULL) {
        return 0;
    }
    for (uint16_t i = 0; i < bm->size; ++i) {
        if (licp_bitmap_test(bm, i)) {
            ++n;
        }
    }
    return n;
}

size_t licp_bitmap_extract(const licp_bitmap_t *bm, uint16_t first, uint16_t count, uint8_t *out, size_t cap)
{
    if (bm == NULL || out == NULL || (uint32_t)first + count > bm->size) {
        return 0u;
    }
    size_t bytes = ((size_t)count + 7u) / 8u;
    if (bytes > cap) {
        return 0u;
    }
    memset(out, 0, bytes);
    for (uint16_t i = 0; i < count; ++i) {
        if (licp_bitmap_test(bm, (uint16_t)(first + i))) {
            out[i / 8u] = (uint8_t)(out[i / 8u] | (1u << (i % 8u)));
        }
    }
    return bytes;
}
