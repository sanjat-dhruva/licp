/**
 * @file licp_pipeline.h
 * @brief Per-frame processing: the flow "Every frame is checked, then stored
 *        by priority" from the implementation plan.
 *
 * Steps for each received frame:
 *  1. Header only (payload failed)? Count a collision and stop.
 *  2. Parse the header; unknown or blocked device? Drop.
 *  3. Rebuild the 32-bit counter and verify the integrity code. Fail? Drop.
 *  4. Counter equal to the last accepted one? Duplicate: drop.
 *     Counter jump larger than the allowed gap? Drop.
 *  5. Store by priority (emergency channels give class 0).
 *  6. Commit the counter, set the device's bit in the acknowledgement
 *     bitmap, queue an emergency acknowledgement, update load counters.
 *  7. Call the registered hooks (extension point for new features).
 *
 * @ingroup core
 */
#ifndef LICP_CORE_PIPELINE_H
#define LICP_CORE_PIPELINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "licp/common/licp_status.h"
#include "licp/core/licp_device_table.h"
#include "licp/core/licp_load.h"
#include "licp/core/licp_superframe.h"
#include "licp/lorawan/licp_frame.h"
#include "licp/proto/licp_bitmap.h"
#include "licp/region/licp_region.h"
#include "licp/storage/licp_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup core
 *  @{ */

#define LICP_PIPELINE_MAX_HOOKS  8u  /**< Hooks that can be registered. */
#define LICP_PIPELINE_SOS_QUEUE 32u  /**< Emergency acknowledgements waiting for a beacon. */

/** @brief Radio flags on a received frame. */
enum {
    LICP_RX_FLAG_HEADER_ONLY = 0x01u /**< Header decoded, payload failed: collision signal. */
};

/** @brief A frame as delivered by a radio back-end. */
typedef struct {
    uint8_t  phy[LICP_FRAME_MAX_LEN]; /**< Physical payload. */
    size_t   len;                     /**< Payload length (0 for header-only events). */
    uint64_t gnss_ms;                 /**< Reception time. */
    uint32_t freq_hz;                 /**< Measured centre frequency. */
    uint8_t  datarate;                /**< ::licp_uplink_dr_t. */
    int16_t  rssi_x10;                /**< Signal strength, 0.1 decibel-milliwatt. */
    int16_t  snr_x10;                 /**< Signal-to-noise ratio, 0.1 decibel. */
    uint8_t  flags;                   /**< ::LICP_RX_FLAG_HEADER_ONLY. */
} licp_rx_frame_t;

/** @brief Outcome of processing one frame. */
typedef enum {
    LICP_RX_ACCEPTED = 0,   /**< Stored. */
    LICP_RX_COLLISION,      /**< Header only; counted for load control. */
    LICP_RX_BAD_FORMAT,     /**< Not a valid uplink data frame or unknown channel. */
    LICP_RX_UNKNOWN_DEVICE, /**< Address not in the device table. */
    LICP_RX_BLOCKED,        /**< Device is blocked. */
    LICP_RX_BAD_MIC,        /**< Integrity check failed. */
    LICP_RX_DUPLICATE,      /**< Same counter as the last accepted frame. */
    LICP_RX_BAD_COUNTER,    /**< Counter jump larger than allowed. */
    LICP_RX_STORE_FULL,     /**< Store full of equal or higher priority frames. */
    LICP_RX_STORE_ERROR,    /**< Input/output error while storing. */
    LICP_RX_OUTCOME_COUNT   /**< Number of outcomes (array size). */
} licp_rx_outcome_t;

/**
 * @brief Hook called after every processed frame.
 * @param outcome Result.
 * @param frame   Parsed frame (NULL for collisions and format errors).
 * @param dev     Device record (NULL if unknown).
 * @param rx      Raw reception.
 * @param user    Pointer given at registration.
 */
typedef void (*licp_pipeline_hook_fn)(licp_rx_outcome_t outcome, const licp_frame_t *frame, const licp_device_t *dev,
                                      const licp_rx_frame_t *rx, void *user);

/** @brief Pipeline settings. */
typedef struct {
    uint32_t max_fcnt_gap;    /**< Largest accepted counter jump (16384 by default). */
    uint32_t freq_tolerance_hz; /**< Tolerance when mapping frequency to channel (30 kHz). */
} licp_pipeline_config_t;

/** @brief Pipeline state; all pointers are borrowed, not owned. */
typedef struct {
    licp_pipeline_config_t cfg;          /**< Settings. */
    licp_device_table_t   *devices;      /**< Device table. */
    licp_store_t          *store;        /**< Store-and-forward log. */
    licp_load_t           *load;         /**< Load controller. */
    licp_bitmap_t         *bitmap;       /**< Acknowledgement bitmap of the current pass. */
    const licp_region_t   *region;       /**< Radio plan. */
    const licp_sf_config_t *sf;          /**< Superframe timing. */
    uint16_t sos_queue[LICP_PIPELINE_SOS_QUEUE]; /**< Short identifiers awaiting acknowledgement. */
    uint8_t  sos_count;                  /**< Entries in @ref sos_queue. */
    uint32_t outcomes[LICP_RX_OUTCOME_COUNT]; /**< Count per outcome. */
    licp_pipeline_hook_fn hooks[LICP_PIPELINE_MAX_HOOKS]; /**< Registered hooks. */
    void    *hook_user[LICP_PIPELINE_MAX_HOOKS];          /**< Their user pointers. */
    uint8_t  hook_count;                 /**< Hooks registered. */
} licp_pipeline_t;

/** @brief Default settings. */
void licp_pipeline_default_config(licp_pipeline_config_t *cfg);

/** @brief Initialise with borrowed modules. @return ::LICP_ERR_PARAM if any is NULL. */
licp_status_t licp_pipeline_init(licp_pipeline_t *p, const licp_pipeline_config_t *cfg, licp_device_table_t *devices,
                                 licp_store_t *store, licp_load_t *load, licp_bitmap_t *bitmap,
                                 const licp_region_t *region, const licp_sf_config_t *sf);

/** @brief Register a hook. @return ::LICP_ERR_FULL when ::LICP_PIPELINE_MAX_HOOKS are registered. */
licp_status_t licp_pipeline_add_hook(licp_pipeline_t *p, licp_pipeline_hook_fn fn, void *user);

/** @brief Process one received frame. */
licp_rx_outcome_t licp_pipeline_process(licp_pipeline_t *p, const licp_rx_frame_t *rx);

/**
 * @brief Move up to @p max queued emergency acknowledgements into @p out
 *        (oldest first) and remove them from the queue.
 * @return Number moved.
 */
uint8_t licp_pipeline_take_sos_acks(licp_pipeline_t *p, uint16_t *out, uint8_t max);

/** @brief Put acknowledgements that did not fit in a beacon back at the front of the queue. */
void licp_pipeline_return_sos_acks(licp_pipeline_t *p, const uint16_t *ids, uint8_t count);

/** @brief Name of an outcome, for logs. */
const char *licp_rx_outcome_str(licp_rx_outcome_t o);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_CORE_PIPELINE_H */
