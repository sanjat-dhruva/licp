/**
 * @file licp_downlink.h
 * @brief Downlink queue and beacon-window planner.
 *
 * The payload transmits only inside the 3.5-second beacon window, while
 * devices are silent. In each window it sends, in order:
 *  1. the beacon,
 *  2. one-to-one messages for devices that answered a call-in,
 *  3. broadcasts announced in this beacon,
 *  4. the acknowledgement bitmap, if one is queued.
 * Each transmission starts @ref licp_dl_config_t::gap_ms after the previous
 * one ends; anything that does not fit stays queued for the next window.
 *
 * One-to-one messages follow the call-in sequence of the network note:
 * queued (::LICP_DL_WAIT_CALLIN) → listed in a beacon (::LICP_DL_CALLED) →
 * device uplink seen (::LICP_DL_READY) → sent in the next window.
 *
 * @ingroup core
 */
#ifndef LICP_CORE_DOWNLINK_H
#define LICP_CORE_DOWNLINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "licp/common/licp_status.h"
#include "licp/hal/licp_radio.h"
#include "licp/region/licp_region.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup core
 *  @{ */

#define LICP_DLQ_CAPACITY  32u /**< Queued downlinks. */
#define LICP_DL_MAX_TX     16u /**< Transmissions planned per window. */

/** @brief Kind of queued downlink. */
typedef enum {
    LICP_DL_UNICAST   = 1, /**< Pre-built one-to-one frame from the ground. */
    LICP_DL_BROADCAST = 2, /**< Pre-built group frame from the ground. */
    LICP_DL_ACKMAP    = 3  /**< Acknowledgement bitmap built on board. */
} licp_dl_kind_t;

/** @brief State of a one-to-one message. */
typedef enum {
    LICP_DL_WAIT_CALLIN = 0, /**< Not yet announced. */
    LICP_DL_CALLED      = 1, /**< Announced in a beacon; waiting for the device's uplink. */
    LICP_DL_READY       = 2  /**< Uplink seen; send in the next window. */
} licp_dl_state_t;

/** @brief Queued downlink. */
typedef struct {
    licp_dl_kind_t  kind;      /**< Kind. */
    licp_dl_state_t state;     /**< One-to-one state. */
    uint32_t        dev_addr;  /**< Target device (one-to-one). */
    uint16_t        short_id;  /**< Target short identifier (one-to-one). */
    uint8_t         group;     /**< Multicast group (broadcast). */
    bool            announced; /**< Broadcast announced in the beacon being planned. */
    uint8_t         data[LICP_FRAME_MAX_LEN]; /**< Frame. */
    size_t          len;       /**< Frame length. */
    uint8_t         call_ins;  /**< Times announced without an answer. */
} licp_dl_item_t;

/** @brief Planner settings. */
typedef struct {
    uint32_t lead_ms;        /**< Beacon start after the window opens (allows radio warm-up). */
    uint32_t gap_ms;         /**< Gap between transmissions. */
    uint8_t  max_call_ins;   /**< Announcements before an unanswered one-to-one message is dropped. */
} licp_dl_config_t;

/** @brief Downlink queue. */
typedef struct {
    licp_dl_config_t cfg;                       /**< Settings. */
    licp_dl_item_t   items[LICP_DLQ_CAPACITY];  /**< Items. */
    size_t           count;                     /**< Items in use. */
    uint32_t         dropped;                   /**< One-to-one messages dropped unanswered. */
} licp_dl_queue_t;

/** @brief Defaults: lead 50 ms, gap 30 ms, 3 call-ins. */
void licp_dl_default_config(licp_dl_config_t *cfg);

/** @brief Initialise an empty queue. */
void licp_dlq_init(licp_dl_queue_t *q, const licp_dl_config_t *cfg);

/**
 * @brief Queue a downlink. An acknowledgement bitmap replaces any bitmap already queued.
 * @return ::LICP_OK, ::LICP_ERR_FULL or ::LICP_ERR_PARAM.
 */
licp_status_t licp_dlq_add(licp_dl_queue_t *q, licp_dl_kind_t kind, uint32_t dev_addr, uint16_t short_id,
                           uint8_t group, const uint8_t *data, size_t len);

/**
 * @brief Short identifiers to list in the next beacon's call-in section:
 *        one-to-one messages not yet announced, or announced but unanswered.
 *        Marks them ::LICP_DL_CALLED; drops messages announced too often.
 * @return Number written to @p ids.
 */
uint8_t licp_dlq_take_call_ins(licp_dl_queue_t *q, uint16_t *ids, uint8_t max);

/**
 * @brief Groups with a broadcast to announce in the next beacon (marks them announced).
 * @return Number written to @p groups.
 */
uint8_t licp_dlq_take_broadcasts(licp_dl_queue_t *q, uint8_t *groups, uint8_t max);

/** @brief Record an uplink from @p dev_addr: a called-in message becomes ready. */
void licp_dlq_on_uplink(licp_dl_queue_t *q, uint32_t dev_addr);

/**
 * @brief Plan one beacon window.
 * @param q            Queue (sent items are removed).
 * @param region       Radio plan (downlink frequency, modulation, power).
 * @param window_start Window start, satellite navigation milliseconds.
 * @param window_ms    Window length.
 * @param beacon       Encoded beacon frame (sent first).
 * @param beacon_len   Beacon length.
 * @param out          Planned transmissions.
 * @param max          Capacity of @p out.
 * @return Number of planned transmissions (0 if even the beacon does not fit).
 */
size_t licp_dl_plan_window(licp_dl_queue_t *q, const licp_region_t *region, uint64_t window_start,
                           uint32_t window_ms, const uint8_t *beacon, size_t beacon_len, licp_tx_request_t *out,
                           size_t max);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_CORE_DOWNLINK_H */
