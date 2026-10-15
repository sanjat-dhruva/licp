/**
 * @file licp_store.c
 * @brief Append-only, checksummed store-and-forward log.
 * @ingroup storage
 */
#include "licp/storage/licp_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "licp/common/licp_bytes.h"
#include "licp/common/licp_crc.h"
#include "licp/common/licp_log.h"

/** Record magic. */
#define REC_MAGIC        0x4C53u
/** Record type: stored frame. */
#define REC_TYPE_FRAME   1u
/** Record type: release list. */
#define REC_TYPE_RELEASE 2u
/** Record header length. */
#define REC_HDR_LEN      10u
/** Record checksum length. */
#define REC_CRC_LEN      4u
/** Frame metadata length. */
#define META_LEN         28u
/** Largest physical payload. */
#define MAX_PHY          255u
/** Largest record data accepted at recovery (largest release record). */
#define MAX_DATA_LEN     (2u + 4u * 1024u)
/** Sequence numbers per release record. */
#define RELEASE_CHUNK    1024u
/** Priority classes. */
#define NUM_CLASSES      4u

/** Module tag used in log messages. */
static const char *TAG = "store";

/** In-memory index entry, one per frame record still in the file. */
typedef struct {
    uint32_t seq;       /**< Sequence number. */
    uint64_t offset;    /**< Record offset in the file. */
    uint32_t rec_len;   /**< Total record length. */
    uint8_t  priority;  /**< Priority class. */
    bool     live;      /**< Not yet released. */
    bool     in_flight; /**< Handed to the ground, awaiting release. */
} entry_t;

/** @brief Store state (opaque to users, see ::licp_store_t). */
struct licp_store {
    char              *path; /**< Log file path. */
    int                fd; /**< Open descriptor. */
    uint64_t           max_bytes; /**< Size limit. */
    bool               fsync_each; /**< fsync after every append. */
    entry_t           *e; /**< Index of records in the file. */
    size_t             n; /**< Entries used. */
    size_t             cap; /**< Entries allocated. */
    uint32_t           next_seq; /**< Next sequence number. */
    uint64_t           file_bytes; /**< Current file size. */
    licp_store_stats_t st; /**< Counters. */
};

/* ------------------------------------------------------------------------- */
/* Low-level helpers                                                          */
/* ------------------------------------------------------------------------- */

/** @brief Write all bytes at @p off, retrying on EINTR and short writes. @return true on success. */
static bool pwrite_all(int fd, const uint8_t *buf, size_t len, uint64_t off)
{
    while (len > 0u) {
        ssize_t w = pwrite(fd, buf, len, (off_t)off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        buf += (size_t)w;
        len -= (size_t)w;
        off += (uint64_t)w;
    }
    return true;
}

/** Returns bytes read (may be short at end of file) or -1 on error. */
static ssize_t pread_all(int fd, uint8_t *buf, size_t len, uint64_t off)
{
    size_t got = 0;
    while (got < len) {
        ssize_t r = pread(fd, buf + got, len - got, (off_t)(off + got));
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            break;
        }
        got += (size_t)r;
    }
    return (ssize_t)got;
}

/** @brief Append an entry to the in-memory index, growing it as needed. */
static licp_status_t index_push(licp_store_t *s, const entry_t *en)
{
    if (s->n == s->cap) {
        size_t ncap = (s->cap == 0u) ? 256u : s->cap * 2u;
        entry_t *ne = (entry_t *)realloc(s->e, ncap * sizeof(entry_t));
        if (ne == NULL) {
            return LICP_ERR_NO_MEMORY;
        }
        s->e = ne;
        s->cap = ncap;
    }
    s->e[s->n++] = *en;
    return LICP_OK;
}

/** Binary search: entries are kept in increasing sequence order. */
static entry_t *index_find(licp_store_t *s, uint32_t seq)
{
    size_t lo = 0, hi = s->n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (s->e[mid].seq < seq) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return (lo < s->n && s->e[lo].seq == seq) ? &s->e[lo] : NULL;
}

/** @brief Write a record header. */
static void encode_header(uint8_t *h, uint8_t type, uint16_t data_len, uint32_t seq)
{
    licp_put_u16(h, REC_MAGIC);
    h[2] = type;
    h[3] = 0;
    licp_put_u16(&h[4], data_len);
    licp_put_u32(&h[6], seq);
}

/** @brief Serialise frame metadata (28 bytes). */
static void encode_meta(uint8_t *p, const licp_store_meta_t *m)
{
    licp_put_u64(p, m->gnss_ms);
    licp_put_u32(p + 8, m->freq_hz);
    p[12] = m->channel;
    p[13] = m->datarate;
    licp_put_u16(p + 14, (uint16_t)m->rssi_x10);
    licp_put_u16(p + 16, (uint16_t)m->snr_x10);
    licp_put_u32(p + 18, m->dev_addr);
    licp_put_u32(p + 22, m->fcnt32);
    p[26] = m->priority;
    p[27] = m->flags;
}

/** @brief Deserialise frame metadata (28 bytes). */
static void decode_meta(const uint8_t *p, licp_store_meta_t *m)
{
    m->gnss_ms = licp_get_u64(p);
    m->freq_hz = licp_get_u32(p + 8);
    m->channel = p[12];
    m->datarate = p[13];
    m->rssi_x10 = (int16_t)licp_get_u16(p + 14);
    m->snr_x10 = (int16_t)licp_get_u16(p + 16);
    m->dev_addr = licp_get_u32(p + 18);
    m->fcnt32 = licp_get_u32(p + 22);
    m->priority = p[26];
    m->flags = p[27];
}

/** @brief fsync when configured to sync every append. */
static bool sync_if(const licp_store_t *s)
{
    return !s->fsync_each || fsync(s->fd) == 0;
}

/* ------------------------------------------------------------------------- */
/* Recovery                                                                   */
/* ------------------------------------------------------------------------- */

/**
 * @brief Scan the log at open: rebuild the index, apply release records and
 *        truncate a torn or corrupted tail after the last good record.
 */
static licp_status_t recover(licp_store_t *s)
{
    uint64_t off = 0;
    uint8_t *buf = (uint8_t *)malloc(REC_HDR_LEN + MAX_DATA_LEN + REC_CRC_LEN);
    if (buf == NULL) {
        return LICP_ERR_NO_MEMORY;
    }
    struct stat sb;
    if (fstat(s->fd, &sb) != 0) {
        free(buf);
        return LICP_ERR_IO;
    }
    uint64_t size = (uint64_t)sb.st_size;
    licp_status_t st = LICP_OK;
    s->next_seq = 1u;

    while (off < size) {
        ssize_t r = pread_all(s->fd, buf, REC_HDR_LEN, off);
        if (r < 0) {
            st = LICP_ERR_IO;
            break;
        }
        if ((size_t)r < REC_HDR_LEN || licp_get_u16(buf) != REC_MAGIC) {
            break; /* torn or garbage tail */
        }
        uint8_t type = buf[2];
        uint16_t dlen = licp_get_u16(&buf[4]);
        uint32_t seq = licp_get_u32(&buf[6]);
        if (dlen > MAX_DATA_LEN) {
            break;
        }
        size_t rest = (size_t)dlen + REC_CRC_LEN;
        r = pread_all(s->fd, buf + REC_HDR_LEN, rest, off + REC_HDR_LEN);
        if (r < 0) {
            st = LICP_ERR_IO;
            break;
        }
        if ((size_t)r < rest) {
            break;
        }
        uint32_t crc = licp_crc32(0, buf, REC_HDR_LEN + (size_t)dlen);
        if (crc != licp_get_u32(buf + REC_HDR_LEN + dlen)) {
            break;
        }
        uint32_t rec_len = (uint32_t)(REC_HDR_LEN + rest);
        if (type == REC_TYPE_FRAME) {
            if (dlen < META_LEN + 1u || dlen > META_LEN + MAX_PHY || seq < s->next_seq) {
                break;
            }
            entry_t en = {seq, off, rec_len, buf[REC_HDR_LEN + 26], true, false};
            if (en.priority >= NUM_CLASSES) {
                en.priority = NUM_CLASSES - 1u;
            }
            st = index_push(s, &en);
            if (st != LICP_OK) {
                break;
            }
            s->next_seq = seq + 1u;
        } else if (type == REC_TYPE_RELEASE) {
            if (dlen < 2u) {
                break;
            }
            uint16_t cnt = licp_get_u16(buf + REC_HDR_LEN);
            if ((size_t)cnt * 4u + 2u != dlen) {
                break;
            }
            for (uint16_t i = 0; i < cnt; ++i) {
                entry_t *en = index_find(s, licp_get_u32(buf + REC_HDR_LEN + 2u + (size_t)i * 4u));
                if (en != NULL) {
                    en->live = false;
                }
            }
        } else {
            break;
        }
        off += rec_len;
    }
    free(buf);
    if (st != LICP_OK) {
        return st;
    }
    if (off < size) {
        s->st.truncated_bytes = (uint32_t)(size - off);
        LICP_LOGW(TAG, "discarding %llu bytes of torn log tail", (unsigned long long)(size - off));
        if (ftruncate(s->fd, (off_t)off) != 0 || fsync(s->fd) != 0) {
            return LICP_ERR_IO;
        }
    }
    s->file_bytes = off;
    return LICP_OK;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                 */
/* ------------------------------------------------------------------------- */

licp_status_t licp_store_open(licp_store_t **out, const licp_store_config_t *cfg)
{
    if (out == NULL || cfg == NULL || cfg->path == NULL || cfg->max_bytes < 1024u) {
        return LICP_ERR_PARAM;
    }
    licp_store_t *s = (licp_store_t *)calloc(1, sizeof(*s));
    if (s == NULL) {
        return LICP_ERR_NO_MEMORY;
    }
    size_t plen = strlen(cfg->path);
    s->path = (char *)malloc(plen + 1u);
    if (s->path == NULL) {
        free(s);
        return LICP_ERR_NO_MEMORY;
    }
    memcpy(s->path, cfg->path, plen + 1u);
    s->max_bytes = cfg->max_bytes;
    s->fsync_each = cfg->fsync_each;
    s->fd = open(s->path, O_RDWR | O_CREAT, 0600);
    if (s->fd < 0) {
        free(s->path);
        free(s);
        return LICP_ERR_IO;
    }
    licp_status_t st = recover(s);
    if (st != LICP_OK) {
        licp_store_close(s);
        return st;
    }
    *out = s;
    return LICP_OK;
}

void licp_store_close(licp_store_t *s)
{
    if (s == NULL) {
        return;
    }
    if (s->fd >= 0) {
        (void)fsync(s->fd);
        (void)close(s->fd);
    }
    free(s->e);
    free(s->path);
    free(s);
}

licp_status_t licp_store_compact(licp_store_t *s)
{
    if (s == NULL) {
        return LICP_ERR_PARAM;
    }
    size_t plen = strlen(s->path);
    char *tmp = (char *)malloc(plen + 5u);
    uint8_t *buf = (uint8_t *)malloc(REC_HDR_LEN + META_LEN + MAX_PHY + REC_CRC_LEN);
    if (tmp == NULL || buf == NULL) {
        free(tmp);
        free(buf);
        return LICP_ERR_NO_MEMORY;
    }
    memcpy(tmp, s->path, plen);
    memcpy(tmp + plen, ".tmp", 5);

    licp_status_t st = LICP_OK;
    int nfd = open(tmp, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (nfd < 0) {
        st = LICP_ERR_IO;
    }
    uint64_t noff = 0;
    size_t keep = 0;
    for (size_t i = 0; st == LICP_OK && i < s->n; ++i) {
        entry_t *en = &s->e[i];
        if (!en->live) {
            continue;
        }
        if (pread_all(s->fd, buf, en->rec_len, en->offset) != (ssize_t)en->rec_len ||
            !pwrite_all(nfd, buf, en->rec_len, noff)) {
            st = LICP_ERR_IO;
            break;
        }
        entry_t moved = *en;
        moved.offset = noff;
        s->e[keep++] = moved;
        noff += en->rec_len;
    }
    if (st == LICP_OK && (fsync(nfd) != 0 || rename(tmp, s->path) != 0)) {
        st = LICP_ERR_IO;
    }
    if (st == LICP_OK) {
        (void)close(s->fd);
        s->fd = nfd;
        s->n = keep;
        s->file_bytes = noff;
    } else {
        if (nfd >= 0) {
            (void)close(nfd);
        }
        (void)unlink(tmp);
        LICP_LOGE(TAG, "compaction failed");
    }
    free(tmp);
    free(buf);
    return st;
}

/** Oldest live, not-in-flight entry with priority strictly lower than @p prio (higher class number). */
static entry_t *pick_victim(licp_store_t *s, uint8_t prio)
{
    for (int c = (int)NUM_CLASSES - 1; c > (int)prio; --c) {
        for (size_t i = 0; i < s->n; ++i) {
            entry_t *en = &s->e[i];
            if (en->live && !en->in_flight && en->priority == (uint8_t)c) {
                return en;
            }
        }
    }
    return NULL;
}

/** @brief True if any indexed record has been released or evicted. */
static bool has_dead(const licp_store_t *s)
{
    for (size_t i = 0; i < s->n; ++i) {
        if (!s->e[i].live) {
            return true;
        }
    }
    return false;
}

licp_status_t licp_store_append(licp_store_t *s, const licp_store_meta_t *meta, const uint8_t *phy, size_t len,
                                 uint32_t *seq)
{
    if (s == NULL || meta == NULL || phy == NULL || len == 0u || len > MAX_PHY || meta->priority >= NUM_CLASSES) {
        return LICP_ERR_PARAM;
    }
    uint32_t rec_len = (uint32_t)(REC_HDR_LEN + META_LEN + len + REC_CRC_LEN);

    while (s->file_bytes + rec_len > s->max_bytes) {
        if (has_dead(s)) {
            licp_status_t st = licp_store_compact(s);
            if (st != LICP_OK) {
                return st;
            }
            continue;
        }
        entry_t *victim = pick_victim(s, meta->priority);
        if (victim == NULL) {
            s->st.rejected_full++;
            return LICP_ERR_FULL;
        }
        victim->live = false;
        s->st.evicted++;
        LICP_LOGW(TAG, "evicting seq %u (class %u) for class %u frame", (unsigned)victim->seq,
                  (unsigned)victim->priority, (unsigned)meta->priority);
    }

    uint8_t rec[REC_HDR_LEN + META_LEN + MAX_PHY + REC_CRC_LEN];
    uint16_t dlen = (uint16_t)(META_LEN + len);
    encode_header(rec, REC_TYPE_FRAME, dlen, s->next_seq);
    encode_meta(rec + REC_HDR_LEN, meta);
    memcpy(rec + REC_HDR_LEN + META_LEN, phy, len);
    licp_put_u32(rec + REC_HDR_LEN + dlen, licp_crc32(0, rec, REC_HDR_LEN + (size_t)dlen));

    if (!pwrite_all(s->fd, rec, rec_len, s->file_bytes) || !sync_if(s)) {
        return LICP_ERR_IO;
    }
    entry_t en = {s->next_seq, s->file_bytes, rec_len, meta->priority, true, false};
    licp_status_t st = index_push(s, &en);
    if (st != LICP_OK) {
        return st;
    }
    s->file_bytes += rec_len;
    if (seq != NULL) {
        *seq = s->next_seq;
    }
    s->next_seq++;
    s->st.appended++;
    return LICP_OK;
}

size_t licp_store_select(licp_store_t *s, uint32_t *seqs, size_t max)
{
    size_t k = 0;
    if (s == NULL || seqs == NULL) {
        return 0;
    }
    for (uint8_t c = 0; c < NUM_CLASSES && k < max; ++c) {
        for (size_t i = 0; i < s->n && k < max; ++i) {
            entry_t *en = &s->e[i];
            if (en->live && !en->in_flight && en->priority == c) {
                en->in_flight = true;
                seqs[k++] = en->seq;
            }
        }
    }
    return k;
}

licp_status_t licp_store_read(licp_store_t *s, uint32_t seq, licp_store_meta_t *meta, uint8_t *phy, size_t cap,
                              size_t *len)
{
    if (s == NULL || meta == NULL || phy == NULL || len == NULL) {
        return LICP_ERR_PARAM;
    }
    entry_t *en = index_find(s, seq);
    if (en == NULL || !en->live) {
        return LICP_ERR_NOT_FOUND;
    }
    uint8_t rec[REC_HDR_LEN + META_LEN + MAX_PHY + REC_CRC_LEN];
    if (en->rec_len > sizeof(rec) || pread_all(s->fd, rec, en->rec_len, en->offset) != (ssize_t)en->rec_len) {
        return LICP_ERR_IO;
    }
    uint16_t dlen = licp_get_u16(&rec[4]);
    if (licp_crc32(0, rec, REC_HDR_LEN + (size_t)dlen) != licp_get_u32(rec + REC_HDR_LEN + dlen)) {
        return LICP_ERR_CRC;
    }
    size_t plen = (size_t)dlen - META_LEN;
    if (plen > cap) {
        return LICP_ERR_LENGTH;
    }
    decode_meta(rec + REC_HDR_LEN, meta);
    memcpy(phy, rec + REC_HDR_LEN + META_LEN, plen);
    *len = plen;
    return LICP_OK;
}

licp_status_t licp_store_release(licp_store_t *s, const uint32_t *seqs, size_t count)
{
    if (s == NULL || (count > 0u && seqs == NULL)) {
        return LICP_ERR_PARAM;
    }
    size_t done = 0;
    while (done < count) {
        size_t chunk = count - done;
        if (chunk > RELEASE_CHUNK) {
            chunk = RELEASE_CHUNK;
        }
        uint8_t *rec = (uint8_t *)malloc(REC_HDR_LEN + 2u + chunk * 4u + REC_CRC_LEN);
        if (rec == NULL) {
            return LICP_ERR_NO_MEMORY;
        }
        uint16_t n = 0;
        for (size_t i = 0; i < chunk; ++i) {
            entry_t *en = index_find(s, seqs[done + i]);
            if (en != NULL && en->live) {
                en->live = false;
                en->in_flight = false;
                licp_put_u32(rec + REC_HDR_LEN + 2u + (size_t)n * 4u, en->seq);
                ++n;
            }
        }
        done += chunk;
        if (n == 0u) {
            free(rec);
            continue;
        }
        s->st.released += n;
        uint16_t dlen = (uint16_t)(2u + (size_t)n * 4u);
        encode_header(rec, REC_TYPE_RELEASE, dlen, 0u);
        licp_put_u16(rec + REC_HDR_LEN, n);
        licp_put_u32(rec + REC_HDR_LEN + dlen, licp_crc32(0, rec, REC_HDR_LEN + (size_t)dlen));
        uint32_t rec_len = (uint32_t)(REC_HDR_LEN + dlen + REC_CRC_LEN);
        licp_status_t st = LICP_OK;
        if (s->file_bytes + rec_len > s->max_bytes) {
            st = licp_store_compact(s); /* drops the released frames; no record needed */
        } else if (!pwrite_all(s->fd, rec, rec_len, s->file_bytes) || !sync_if(s)) {
            st = LICP_ERR_IO;
        } else {
            s->file_bytes += rec_len;
        }
        free(rec);
        if (st != LICP_OK) {
            return st;
        }
    }
    return LICP_OK;
}

void licp_store_requeue_in_flight(licp_store_t *s)
{
    if (s == NULL) {
        return;
    }
    for (size_t i = 0; i < s->n; ++i) {
        s->e[i].in_flight = false;
    }
}

void licp_store_get_stats(const licp_store_t *s, licp_store_stats_t *st)
{
    if (s == NULL || st == NULL) {
        return;
    }
    *st = s->st;
    st->pending = 0;
    st->in_flight = 0;
    memset(st->pending_by_class, 0, sizeof(st->pending_by_class));
    for (size_t i = 0; i < s->n; ++i) {
        if (s->e[i].live) {
            st->pending++;
            st->pending_by_class[s->e[i].priority]++;
            if (s->e[i].in_flight) {
                st->in_flight++;
            }
        }
    }
    st->file_bytes = s->file_bytes;
}
