/**
 * @file licp_store.h
 * @brief Priority store-and-forward of received frames in flash.
 *
 * Frames are appended to a log file. Each record carries a checksum, so a
 * record cut short by a power loss is detected at start-up and the file is
 * truncated back to the last good record. Frames are handed to the ground in
 * priority order (emergency first, then oldest first) and are only removed
 * once the ground confirms delivery ("release"). Released space is reclaimed
 * by compaction, which rewrites the live records to a new file and renames it
 * over the old one.
 *
 * Record layout (little-endian):
 * @verbatim
 *  magic(2)=0x4C53 | type(1) | reserved(1) | data_len(2) | seq(4) | data(data_len) | crc32(4)
 *  FRAME data:   gnss_ms(8) freq_hz(4) channel(1) datarate(1) rssi_x10(2) snr_x10(2)
 *                dev_addr(4) fcnt32(4) priority(1) flags(1) | phy payload
 *  RELEASE data: count(2) | seq(4) x count
 * @endverbatim
 *
 * When the store is full, a new frame may evict the oldest frame of a strictly
 * lower priority; emergency frames therefore always find room unless the
 * store is full of emergency frames.
 *
 * @ingroup storage
 */
#ifndef LICP_STORAGE_STORE_H
#define LICP_STORAGE_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "licp/common/licp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup storage Storage
 * @brief Store-and-forward log.
 * @{
 */

/** @brief Metadata stored with each frame. */
typedef struct {
    uint64_t gnss_ms;   /**< Reception time, satellite navigation milliseconds. */
    uint32_t freq_hz;   /**< Measured centre frequency. */
    uint8_t  channel;   /**< Channel index. */
    uint8_t  datarate;  /**< ::licp_uplink_dr_t value. */
    int16_t  rssi_x10;  /**< Signal strength in 0.1 decibel-milliwatt. */
    int16_t  snr_x10;   /**< Signal-to-noise ratio in 0.1 decibel. */
    uint32_t dev_addr;  /**< Device address. */
    uint32_t fcnt32;    /**< Rebuilt frame counter. */
    uint8_t  priority;  /**< Priority class, 0 = emergency. */
    uint8_t  flags;     /**< Reserved for features (e.g. received in a shared slot). */
} licp_store_meta_t;

/** @brief Store settings. */
typedef struct {
    const char *path;       /**< Log file path. */
    uint64_t    max_bytes;  /**< Size limit of the log file. */
    bool        fsync_each; /**< fsync after every append (safer, slower). */
} licp_store_config_t;

/** @brief Statistics for telemetry. */
typedef struct {
    uint32_t pending;          /**< Frames not yet released. */
    uint32_t in_flight;        /**< Frames handed to the ground, awaiting release. */
    uint32_t pending_by_class[4]; /**< Pending frames per priority class. */
    uint32_t appended;         /**< Frames appended since open. */
    uint32_t released;         /**< Frames released since open. */
    uint32_t evicted;          /**< Lower-priority frames evicted to make room. */
    uint32_t rejected_full;    /**< Frames refused because the store was full. */
    uint32_t truncated_bytes;  /**< Bytes discarded at open after a power loss. */
    uint64_t file_bytes;       /**< Current log size. */
} licp_store_stats_t;

/** @brief Opaque store handle. */
typedef struct licp_store licp_store_t;

/**
 * @brief Open (creating if needed) and recover the log.
 * @param[out] out Handle; free with ::licp_store_close.
 * @param cfg Settings; @ref licp_store_config_t::path is copied.
 * @return ::LICP_OK, ::LICP_ERR_PARAM, ::LICP_ERR_IO or ::LICP_ERR_NO_MEMORY.
 */
licp_status_t licp_store_open(licp_store_t **out, const licp_store_config_t *cfg);

/** @brief Close the store and free the handle (NULL is accepted). */
void licp_store_close(licp_store_t *s);

/**
 * @brief Append a frame.
 * @param s     Store.
 * @param meta  Metadata (priority 0..3).
 * @param phy   Physical payload.
 * @param len   Payload length (1..255).
 * @param[out] seq Sequence number assigned (may be NULL).
 * @return ::LICP_OK, ::LICP_ERR_FULL, ::LICP_ERR_IO or ::LICP_ERR_PARAM.
 */
licp_status_t licp_store_append(licp_store_t *s, const licp_store_meta_t *meta, const uint8_t *phy, size_t len,
                                 uint32_t *seq);

/**
 * @brief Pick up to @p max frames to hand to the ground: unreleased and not
 *        in flight, emergency first, then oldest. They are marked in flight.
 * @param s   Store.
 * @param seqs Output sequence numbers.
 * @param max Capacity of @p seqs.
 * @return Number of sequence numbers written.
 */
size_t licp_store_select(licp_store_t *s, uint32_t *seqs, size_t max);

/**
 * @brief Read a stored frame.
 * @param s Store.
 * @param seq Sequence number.
 * @param[out] meta Metadata.
 * @param[out] phy  Payload buffer (at least 255 bytes).
 * @param cap Capacity of @p phy.
 * @param[out] len Payload length.
 * @return ::LICP_OK, ::LICP_ERR_NOT_FOUND, ::LICP_ERR_IO or ::LICP_ERR_CRC.
 */
licp_status_t licp_store_read(licp_store_t *s, uint32_t seq, licp_store_meta_t *meta, uint8_t *phy, size_t cap,
                              size_t *len);

/**
 * @brief Release frames the ground has confirmed. Unknown or already released
 *        sequence numbers are ignored.
 * @return ::LICP_OK or ::LICP_ERR_IO.
 */
licp_status_t licp_store_release(licp_store_t *s, const uint32_t *seqs, size_t count);

/** @brief Return all in-flight frames to pending (e.g. ground contact lost). */
void licp_store_requeue_in_flight(licp_store_t *s);

/** @brief Rewrite the log with live records only. @return ::LICP_OK or ::LICP_ERR_IO. */
licp_status_t licp_store_compact(licp_store_t *s);

/** @brief Current statistics. */
void licp_store_get_stats(const licp_store_t *s, licp_store_stats_t *st);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_STORAGE_STORE_H */
