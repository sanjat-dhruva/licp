/**
 * @file licp_service.h
 * @brief The on-board service: ties every module together.
 *
 * The service is driven by ::licp_service_poll, called often (every 10–20
 * milliseconds) from the main loop. Each call:
 *  1. reads received frames from every receive back-end and runs them
 *     through the pipeline;
 *  2. starts a new pass (fresh acknowledgement bitmap) after a quiet gap;
 *  3. shortly before each beacon window, closes the load-control period,
 *     builds the beacon and plans the window's transmissions.
 *
 * Messages from the on-board computer are handled by
 * ::licp_service_handle_obc, which returns the encoded reply.
 *
 * Extension points: pipeline hooks (::licp_service_pipeline), extra beacon
 * sections (::licp_service_set_beacon_extra) and unknown plan entries
 * (::licp_service_set_plan_extension).
 *
 * @ingroup app
 */
#ifndef LICP_APP_SERVICE_H
#define LICP_APP_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "licp/app/licp_config.h"
#include "licp/core/licp_device_table.h"
#include "licp/core/licp_downlink.h"
#include "licp/core/licp_load.h"
#include "licp/core/licp_pipeline.h"
#include "licp/core/licp_plan.h"
#include "licp/hal/licp_clock.h"
#include "licp/hal/licp_radio.h"
#include "licp/obc/licp_obc.h"
#include "licp/proto/licp_beacon.h"
#include "licp/proto/licp_bitmap.h"
#include "licp/storage/licp_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup app
 *  @{ */

#define LICP_SVC_MAX_RX      4u   /**< Receive back-ends. */
#define LICP_SVC_MAX_ORBITS  8u   /**< Orbit records in the beacon rotation. */
#define LICP_SVC_RX_BATCH   32u   /**< Frames read per back-end per poll. */
#define LICP_SVC_MAX_FRAMES_PER_MSG 50u /**< Stored frames per FRAMES message. */

/** @brief Telemetry identifiers in the STATUS message (id(1) value(4) pairs). */
typedef enum {
    LICP_TM_UPTIME_S = 1,      /**< Seconds since start. */
    LICP_TM_DEVICES,           /**< Devices in the table. */
    LICP_TM_PENDING,           /**< Frames waiting for the ground. */
    LICP_TM_PENDING_SOS,       /**< Of which emergency. */
    LICP_TM_IN_FLIGHT,         /**< Frames handed over, unconfirmed. */
    LICP_TM_EVICTED,           /**< Frames evicted for space. */
    LICP_TM_REJECTED_FULL,     /**< Frames refused, store full. */
    LICP_TM_RX_ACCEPTED,       /**< Pipeline: accepted. */
    LICP_TM_RX_COLLISIONS,     /**< Pipeline: header-only events. */
    LICP_TM_RX_REJECTED,       /**< Pipeline: every drop reason. */
    LICP_TM_P_CLASS1,          /**< Access probability class 1 (0..255). */
    LICP_TM_P_CLASS2,          /**< Access probability class 2. */
    LICP_TM_P_CLASS3,          /**< Access probability class 3. */
    LICP_TM_COLLISION_PERMIL,  /**< Last superframe collision ratio x 1000. */
    LICP_TM_TX_OK,             /**< Transmissions accepted by the radio. */
    LICP_TM_TX_REJECTED,       /**< Transmissions refused. */
    LICP_TM_PLAN_SEQ,          /**< Last applied plan. */
    LICP_TM_DL_DROPPED,        /**< One-to-one messages dropped unanswered. */
    LICP_TM_PASS_ID            /**< Current pass. */
} licp_tm_id_t;

/** @brief Service state. Treat fields as read-only outside the service. */
typedef struct {
    licp_config_t       cfg;                       /**< Configuration. */
    licp_clock_t        clock;                     /**< Time source. */
    licp_radio_t        rx[LICP_SVC_MAX_RX];       /**< Receive back-ends. */
    size_t              rx_count;                  /**< Back-ends in use. */
    licp_radio_t        tx;                        /**< Transmit back-end. */
    licp_device_table_t devices;                   /**< Device table. */
    licp_store_t       *store;                     /**< Store-and-forward log. */
    licp_load_t         load;                      /**< Load controller. */
    licp_bitmap_t       bitmap;                    /**< Current pass bitmap. */
    licp_pipeline_t     pipeline;                  /**< Frame pipeline. */
    licp_dl_queue_t     dlq;                       /**< Downlink queue. */
    licp_orbit_t        orbits[LICP_SVC_MAX_ORBITS]; /**< Orbit records in rotation. */
    uint8_t             orbit_count;               /**< Records held. */
    uint8_t             orbit_next;                /**< Next record to send. */
    licp_beacon_extra_t beacon_extra[LICP_BEACON_MAX_EXTRA]; /**< Extension sections. */
    uint8_t             beacon_extra_count;        /**< Extension sections set. */
    licp_status_t     (*plan_ext)(void *user, uint8_t type, const uint8_t *value, size_t len); /**< Plan extension handler. */
    void               *plan_ext_user;             /**< Its user pointer. */
    int64_t             last_planned_sf;           /**< Last superframe whose window was planned (-1 = none). */
    uint64_t            last_accept_ms;            /**< Time of the last accepted frame. */
    uint16_t            pass_id;                   /**< Current pass. */
    uint16_t            bitmap_sent_count;         /**< Bits set when the bitmap was last queued. */
    uint32_t            plan_seq;                  /**< Last applied plan. */
    uint64_t            start_ms;                  /**< Start time. */
    uint16_t            obc_seq;                   /**< Outgoing message counter. */
    int                 devices_dirty;             /**< Device table needs saving. */
} licp_service_t;

/**
 * @brief Initialise the service. Opens the store and loads the saved device
 *        table if present. Radios are bound afterwards with
 *        ::licp_service_add_rx and ::licp_service_set_tx.
 * @return ::LICP_OK or the first error.
 */
licp_status_t licp_service_init(licp_service_t *s, const licp_config_t *cfg, const licp_clock_t *clock);

/** @brief Add a receive back-end (already initialised). */
licp_status_t licp_service_add_rx(licp_service_t *s, const licp_radio_t *radio);

/** @brief Set the transmit back-end. */
void licp_service_set_tx(licp_service_t *s, const licp_radio_t *radio);

/** @brief Start every radio. */
licp_status_t licp_service_start(licp_service_t *s);

/** @brief One iteration of work (see file description). */
void licp_service_poll(licp_service_t *s);

/**
 * @brief Handle one message from the on-board computer.
 * @param s     Service.
 * @param msg   Decoded message.
 * @param reply Output buffer for the encoded reply.
 * @param cap   Capacity of @p reply (at least ::LICP_OBC_HDR_LEN + ::LICP_OBC_MAX_PAYLOAD + 2).
 * @return Length of the reply (0 if none).
 */
size_t licp_service_handle_obc(licp_service_t *s, const licp_obc_msg_t *msg, uint8_t *reply, size_t cap);

/** @brief Save the device table if it changed. */
licp_status_t licp_service_flush(licp_service_t *s);

/** @brief Stop radios, save state, free memory. */
void licp_service_shutdown(licp_service_t *s);

/** @brief Access the pipeline, e.g. to register hooks. */
licp_pipeline_t *licp_service_pipeline(licp_service_t *s);

/**
 * @brief Set the extension sections added to every beacon (copied by pointer;
 *        the data must stay valid). Pass @p count 0 to clear.
 */
licp_status_t licp_service_set_beacon_extra(licp_service_t *s, const licp_beacon_extra_t *extra, uint8_t count);

/** @brief Handler for plan entry types the service does not know. */
void licp_service_set_plan_extension(licp_service_t *s,
                                     licp_status_t (*fn)(void *user, uint8_t type, const uint8_t *value, size_t len),
                                     void *user);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_APP_SERVICE_H */
