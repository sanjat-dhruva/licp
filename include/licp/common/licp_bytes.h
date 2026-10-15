/**
 * @file licp_bytes.h
 * @brief Little-endian packing helpers and a bounded byte writer/reader.
 *
 * Long Range Wide Area Network frames and every LICP message format are
 * little-endian. These helpers keep all byte-order handling in one place.
 *
 * @ingroup common
 */
#ifndef LICP_COMMON_BYTES_H
#define LICP_COMMON_BYTES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup common
 *  @{ */

/** @brief Write a 16-bit value little-endian. */
static inline void licp_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

/** @brief Write the low 24 bits of a value little-endian. */
static inline void licp_put_u24(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
}

/** @brief Write a 32-bit value little-endian. */
static inline void licp_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

/** @brief Write a 64-bit value little-endian. */
static inline void licp_put_u64(uint8_t *p, uint64_t v)
{
    licp_put_u32(p, (uint32_t)(v & 0xFFFFFFFFu));
    licp_put_u32(p + 4, (uint32_t)(v >> 32));
}

/** @brief Read a 16-bit little-endian value. */
static inline uint16_t licp_get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/** @brief Read a 24-bit little-endian value. */
static inline uint32_t licp_get_u24(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

/** @brief Read a 32-bit little-endian value. */
static inline uint32_t licp_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** @brief Read a 64-bit little-endian value. */
static inline uint64_t licp_get_u64(const uint8_t *p)
{
    return (uint64_t)licp_get_u32(p) | ((uint64_t)licp_get_u32(p + 4) << 32);
}

/**
 * @brief Bounded writer. Once an operation overflows, @ref ok becomes false
 *        and every later write is ignored, so callers check once at the end.
 */
typedef struct {
    uint8_t *buf;  /**< Destination buffer. */
    size_t   cap;  /**< Capacity of @ref buf in bytes. */
    size_t   len;  /**< Bytes written so far. */
    bool     ok;   /**< False after the first overflow. */
} licp_writer_t;

/** @brief Start writing into @p buf of @p cap bytes. */
static inline void licp_writer_init(licp_writer_t *w, uint8_t *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->ok = (buf != NULL);
}

/** @brief Reserve @p n bytes; returns a pointer to them or NULL on overflow. */
static inline uint8_t *licp_writer_reserve(licp_writer_t *w, size_t n)
{
    if (!w->ok || (w->cap - w->len) < n) {
        w->ok = false;
        return NULL;
    }
    uint8_t *p = w->buf + w->len;
    w->len += n;
    return p;
}

/** @brief Append one byte. */
static inline void licp_writer_u8(licp_writer_t *w, uint8_t v)
{
    uint8_t *p = licp_writer_reserve(w, 1);
    if (p) { p[0] = v; }
}

/** @brief Append a 16-bit little-endian value. */
static inline void licp_writer_u16(licp_writer_t *w, uint16_t v)
{
    uint8_t *p = licp_writer_reserve(w, 2);
    if (p) { licp_put_u16(p, v); }
}

/** @brief Append a 32-bit little-endian value. */
static inline void licp_writer_u32(licp_writer_t *w, uint32_t v)
{
    uint8_t *p = licp_writer_reserve(w, 4);
    if (p) { licp_put_u32(p, v); }
}

/** @brief Append raw bytes. */
static inline void licp_writer_bytes(licp_writer_t *w, const uint8_t *src, size_t n)
{
    uint8_t *p = licp_writer_reserve(w, n);
    if (p && n > 0u) {
        for (size_t i = 0; i < n; ++i) { p[i] = src[i]; }
    }
}

/**
 * @brief Bounded reader, mirror of ::licp_writer_t. After the first
 *        under-run @ref ok becomes false and reads return zero.
 */
typedef struct {
    const uint8_t *buf; /**< Source buffer. */
    size_t         len; /**< Length of @ref buf. */
    size_t         pos; /**< Read position. */
    bool           ok;  /**< False after the first under-run. */
} licp_reader_t;

/** @brief Start reading @p len bytes from @p buf. */
static inline void licp_reader_init(licp_reader_t *r, const uint8_t *buf, size_t len)
{
    r->buf = buf;
    r->len = len;
    r->pos = 0;
    r->ok = (buf != NULL) || (len == 0u);
}

/** @brief Bytes still available. */
static inline size_t licp_reader_left(const licp_reader_t *r)
{
    return r->ok ? (r->len - r->pos) : 0u;
}

/** @brief Take @p n bytes; returns a pointer to them or NULL on under-run. */
static inline const uint8_t *licp_reader_take(licp_reader_t *r, size_t n)
{
    if (!r->ok || (r->len - r->pos) < n) {
        r->ok = false;
        return NULL;
    }
    const uint8_t *p = r->buf + r->pos;
    r->pos += n;
    return p;
}

/** @brief Read one byte (0 on under-run). */
static inline uint8_t licp_reader_u8(licp_reader_t *r)
{
    const uint8_t *p = licp_reader_take(r, 1);
    return p ? p[0] : 0u;
}

/** @brief Read a 16-bit little-endian value (0 on under-run). */
static inline uint16_t licp_reader_u16(licp_reader_t *r)
{
    const uint8_t *p = licp_reader_take(r, 2);
    return p ? licp_get_u16(p) : 0u;
}

/** @brief Read a 32-bit little-endian value (0 on under-run). */
static inline uint32_t licp_reader_u32(licp_reader_t *r)
{
    const uint8_t *p = licp_reader_take(r, 4);
    return p ? licp_get_u32(p) : 0u;
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_COMMON_BYTES_H */
