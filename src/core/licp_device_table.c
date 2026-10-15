/**
 * @file licp_device_table.c
 * @brief Sorted device table with file persistence.
 *
 * File format: "LDEV" | version(1) | count(4) | records(count x 33) | CRC32(4).
 * Record: dev_addr(4) key(16) home_channel(1) home_slot(1) priority(1)
 *         short_id(2) fcnt_up_last(4) fcnt_valid(1) flags(1) reserved(2).
 *
 * @ingroup core
 */
#include "licp/core/licp_device_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "licp/common/licp_bytes.h"
#include "licp/common/licp_crc.h"

/** File magic. */
#define DEVFILE_MAGIC   "LDEV"
/** File layout version. */
#define DEVFILE_VERSION 1u
/** Bytes per device record in the file. */
#define DEVFILE_REC_LEN 33u

/** @brief Overwrite memory with zeros in a way the compiler cannot remove. */
static void wipe(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    for (size_t i = 0; i < n; ++i) {
        v[i] = 0;
    }
}

licp_status_t licp_device_table_init(licp_device_table_t *t, size_t capacity)
{
    if (t == NULL || capacity == 0u) {
        return LICP_ERR_PARAM;
    }
    t->items = (licp_device_t *)calloc(capacity, sizeof(licp_device_t));
    if (t->items == NULL) {
        return LICP_ERR_NO_MEMORY;
    }
    t->count = 0;
    t->capacity = capacity;
    return LICP_OK;
}

void licp_device_table_free(licp_device_table_t *t)
{
    if (t == NULL || t->items == NULL) {
        return;
    }
    wipe(t->items, t->capacity * sizeof(licp_device_t));
    free(t->items);
    t->items = NULL;
    t->count = 0;
    t->capacity = 0;
}

/** Index of the first record with address >= @p addr. */
static size_t lower_bound(const licp_device_table_t *t, uint32_t addr)
{
    size_t lo = 0, hi = t->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (t->items[mid].dev_addr < addr) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo;
}

licp_status_t licp_device_table_upsert(licp_device_table_t *t, const licp_device_t *d)
{
    if (t == NULL || d == NULL || t->items == NULL) {
        return LICP_ERR_PARAM;
    }
    size_t i = lower_bound(t, d->dev_addr);
    if (i < t->count && t->items[i].dev_addr == d->dev_addr) {
        licp_device_t *cur = &t->items[i];
        bool keep_counter = cur->fcnt_valid && (!d->fcnt_valid || cur->fcnt_up_last > d->fcnt_up_last);
        uint32_t fcnt = cur->fcnt_up_last;
        *cur = *d;
        if (keep_counter) {
            cur->fcnt_up_last = fcnt;
            cur->fcnt_valid = true;
        }
        return LICP_OK;
    }
    if (t->count >= t->capacity) {
        return LICP_ERR_FULL;
    }
    memmove(&t->items[i + 1u], &t->items[i], (t->count - i) * sizeof(licp_device_t));
    t->items[i] = *d;
    t->count++;
    return LICP_OK;
}

licp_status_t licp_device_table_remove(licp_device_table_t *t, uint32_t dev_addr)
{
    if (t == NULL || t->items == NULL) {
        return LICP_ERR_PARAM;
    }
    size_t i = lower_bound(t, dev_addr);
    if (i >= t->count || t->items[i].dev_addr != dev_addr) {
        return LICP_ERR_NOT_FOUND;
    }
    wipe(&t->items[i], sizeof(licp_device_t));
    memmove(&t->items[i], &t->items[i + 1u], (t->count - i - 1u) * sizeof(licp_device_t));
    t->count--;
    wipe(&t->items[t->count], sizeof(licp_device_t));
    return LICP_OK;
}

licp_device_t *licp_device_table_find(licp_device_table_t *t, uint32_t dev_addr)
{
    if (t == NULL || t->items == NULL) {
        return NULL;
    }
    size_t i = lower_bound(t, dev_addr);
    if (i < t->count && t->items[i].dev_addr == dev_addr) {
        return &t->items[i];
    }
    return NULL;
}

licp_device_t *licp_device_table_find_short(licp_device_table_t *t, uint16_t short_id)
{
    if (t == NULL || t->items == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < t->count; ++i) {
        if (t->items[i].short_id == short_id) {
            return &t->items[i];
        }
    }
    return NULL;
}

size_t licp_device_table_count(const licp_device_table_t *t)
{
    return (t == NULL) ? 0u : t->count;
}

/** @brief Serialise a device record (33 bytes). */
static void encode_record(const licp_device_t *d, uint8_t *p)
{
    licp_put_u32(p, d->dev_addr);
    memcpy(p + 4, d->nwk_s_key, LICP_KEY_LEN);
    p[20] = d->home_channel;
    p[21] = d->home_slot;
    p[22] = d->priority;
    licp_put_u16(p + 23, d->short_id);
    licp_put_u32(p + 25, d->fcnt_up_last);
    p[29] = d->fcnt_valid ? 1u : 0u;
    p[30] = d->flags;
    p[31] = 0;
    p[32] = 0;
}

/** @brief Deserialise a device record (33 bytes). */
static void decode_record(const uint8_t *p, licp_device_t *d)
{
    memset(d, 0, sizeof(*d));
    d->dev_addr = licp_get_u32(p);
    memcpy(d->nwk_s_key, p + 4, LICP_KEY_LEN);
    d->home_channel = p[20];
    d->home_slot = p[21];
    d->priority = p[22];
    d->short_id = licp_get_u16(p + 23);
    d->fcnt_up_last = licp_get_u32(p + 25);
    d->fcnt_valid = (p[29] != 0u);
    d->flags = p[30];
}

licp_status_t licp_device_table_save(const licp_device_table_t *t, const char *path)
{
    if (t == NULL || path == NULL) {
        return LICP_ERR_PARAM;
    }
    char tmp[512];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return LICP_ERR_PARAM;
    }
    FILE *f = fopen(tmp, "wb");
    if (f == NULL) {
        return LICP_ERR_IO;
    }
    uint32_t crc = 0;
    uint8_t hdr[9];
    memcpy(hdr, DEVFILE_MAGIC, 4);
    hdr[4] = DEVFILE_VERSION;
    licp_put_u32(&hdr[5], (uint32_t)t->count);
    bool ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    crc = licp_crc32(crc, hdr, sizeof(hdr));
    for (size_t i = 0; ok && i < t->count; ++i) {
        uint8_t rec[DEVFILE_REC_LEN];
        encode_record(&t->items[i], rec);
        ok = fwrite(rec, 1, sizeof(rec), f) == sizeof(rec);
        crc = licp_crc32(crc, rec, sizeof(rec));
        wipe(rec, sizeof(rec));
    }
    uint8_t c[4];
    licp_put_u32(c, crc);
    ok = ok && fwrite(c, 1, 4, f) == 4u;
    ok = ok && fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp, path) != 0) {
        (void)remove(tmp);
        return LICP_ERR_IO;
    }
    return LICP_OK;
}

licp_status_t licp_device_table_load(licp_device_table_t *t, const char *path)
{
    if (t == NULL || path == NULL || t->items == NULL) {
        return LICP_ERR_PARAM;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return LICP_ERR_IO;
    }
    licp_status_t st = LICP_OK;
    uint8_t hdr[9];
    uint32_t crc = 0;
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        st = LICP_ERR_FORMAT;
    } else if (memcmp(hdr, DEVFILE_MAGIC, 4) != 0 || hdr[4] != DEVFILE_VERSION) {
        st = LICP_ERR_FORMAT;
    }
    uint32_t n = (st == LICP_OK) ? licp_get_u32(&hdr[5]) : 0u;
    if (st == LICP_OK && n > t->capacity) {
        st = LICP_ERR_FULL;
    }
    licp_device_t *tmp = NULL;
    if (st == LICP_OK) {
        crc = licp_crc32(crc, hdr, sizeof(hdr));
        tmp = (licp_device_t *)calloc(n > 0u ? n : 1u, sizeof(licp_device_t));
        if (tmp == NULL) {
            st = LICP_ERR_NO_MEMORY;
        }
    }
    for (uint32_t i = 0; st == LICP_OK && i < n; ++i) {
        uint8_t rec[DEVFILE_REC_LEN];
        if (fread(rec, 1, sizeof(rec), f) != sizeof(rec)) {
            st = LICP_ERR_FORMAT;
            break;
        }
        crc = licp_crc32(crc, rec, sizeof(rec));
        decode_record(rec, &tmp[i]);
        wipe(rec, sizeof(rec));
        if (i > 0u && tmp[i].dev_addr <= tmp[i - 1u].dev_addr) {
            st = LICP_ERR_FORMAT; /* must be strictly sorted */
        }
    }
    if (st == LICP_OK) {
        uint8_t c[4];
        if (fread(c, 1, 4, f) != 4u) {
            st = LICP_ERR_FORMAT;
        } else if (licp_get_u32(c) != crc) {
            st = LICP_ERR_CRC;
        }
    }
    (void)fclose(f);
    if (st == LICP_OK) {
        wipe(t->items, t->capacity * sizeof(licp_device_t));
        if (n > 0u) {
            memcpy(t->items, tmp, n * sizeof(licp_device_t));
        }
        t->count = n;
    }
    if (tmp != NULL) {
        wipe(tmp, (n > 0u ? n : 1u) * sizeof(licp_device_t));
        free(tmp);
    }
    return st;
}
