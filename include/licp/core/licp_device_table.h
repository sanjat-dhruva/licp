/**
 * @file licp_device_table.h
 * @brief Table of devices the payload accepts frames from.
 *
 * Records are uploaded by the central system at each ground-station contact
 * (see licp_plan.h). The table is kept sorted by device address so lookups
 * are a binary search. It can be saved to and loaded from a file so that a
 * reset of the payload computer does not lose it.
 *
 * @ingroup core
 */
#ifndef LICP_CORE_DEVICE_TABLE_H
#define LICP_CORE_DEVICE_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "licp/common/licp_status.h"
#include "licp/lorawan/licp_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup core On-board service core
 * @brief Device table, frame pipeline, timing, load control, downlinks and plans.
 * @{
 */

/** @brief Device record flags. */
enum {
    LICP_DEV_FLAG_BLOCKED = 0x01u /**< Frames are rejected (lost or compromised device). */
};

/** @brief One device as uploaded by the central system. */
typedef struct {
    uint32_t dev_addr;                 /**< Device address (sort key). */
    uint8_t  nwk_s_key[LICP_KEY_LEN];  /**< Network session key for the integrity check. */
    uint8_t  home_channel;             /**< Reserved channel. */
    uint8_t  home_slot;                /**< Reserved slot in each superframe (0..11). */
    uint8_t  priority;                 /**< Default priority class 1..3 (0 is emergency). */
    uint16_t short_id;                 /**< Bit index in the acknowledgement bitmap. */
    uint32_t fcnt_up_last;             /**< Last accepted 32-bit uplink counter. */
    bool     fcnt_valid;               /**< False until a counter is known. */
    uint8_t  flags;                    /**< ::LICP_DEV_FLAG_BLOCKED etc. */
} licp_device_t;

/** @brief Device table. Allocate with ::licp_device_table_init. */
typedef struct {
    licp_device_t *items;    /**< Sorted array. */
    size_t         count;    /**< Records in use. */
    size_t         capacity; /**< Allocated records. */
} licp_device_table_t;

/** @brief Allocate a table for up to @p capacity devices. */
licp_status_t licp_device_table_init(licp_device_table_t *t, size_t capacity);

/** @brief Free the table and wipe its keys. */
void licp_device_table_free(licp_device_table_t *t);

/**
 * @brief Insert or replace a record (replacing keeps the larger of the two counters
 *        so an older upload cannot roll the counter back).
 * @return ::LICP_OK or ::LICP_ERR_FULL.
 */
licp_status_t licp_device_table_upsert(licp_device_table_t *t, const licp_device_t *d);

/** @brief Remove a record. @return ::LICP_OK or ::LICP_ERR_NOT_FOUND. */
licp_status_t licp_device_table_remove(licp_device_table_t *t, uint32_t dev_addr);

/** @brief Look up a record; returns NULL if absent. The pointer is valid until the next insert or remove. */
licp_device_t *licp_device_table_find(licp_device_table_t *t, uint32_t dev_addr);

/** @brief Look up by short identifier (linear search); NULL if absent. */
licp_device_t *licp_device_table_find_short(licp_device_table_t *t, uint16_t short_id);

/** @brief Number of records. */
size_t licp_device_table_count(const licp_device_table_t *t);

/**
 * @brief Save the table atomically (write to "<path>.tmp", fsync, rename).
 * @return ::LICP_OK or ::LICP_ERR_IO.
 */
licp_status_t licp_device_table_save(const licp_device_table_t *t, const char *path);

/**
 * @brief Replace the table content with a saved file.
 * @return ::LICP_OK, ::LICP_ERR_IO, ::LICP_ERR_CRC, ::LICP_ERR_FORMAT or ::LICP_ERR_FULL.
 */
licp_status_t licp_device_table_load(licp_device_table_t *t, const char *path);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_CORE_DEVICE_TABLE_H */
