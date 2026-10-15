/**
 * @file licp_radio.h
 * @brief Radio back-end interface.
 *
 * The service talks to radios only through ::licp_radio_ops_t, so receivers
 * can be swapped without touching the rest of the code:
 *  - `licp_radio_sim`    : in-memory back-end for tests and ground simulation;
 *  - `licp_radio_fhss`   : frequency-hopping receiver in programmable logic,
 *                          read through a character device;
 *  - `licp_radio_sx1302` : SX1302 concentrator cards through Semtech libloragw
 *                          (built with -DLICP_WITH_LIBLORAGW=ON).
 *
 * A new back-end implements the five operations and is registered in the
 * service configuration (see docs/adding_features.md).
 *
 * @ingroup hal
 */
#ifndef LICP_HAL_RADIO_H
#define LICP_HAL_RADIO_H

#include <stddef.h>
#include <stdint.h>

#include "licp/common/licp_status.h"
#include "licp/core/licp_pipeline.h"
#include "licp/lorawan/licp_airtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup hal Hardware abstraction
 * @brief Radio back-ends, clock and watchdog.
 * @{
 */

/** @brief One scheduled downlink transmission. */
typedef struct {
    uint64_t           time_ms;   /**< Start time, satellite navigation milliseconds. */
    uint32_t           freq_hz;   /**< Centre frequency. */
    licp_lora_params_t mod;       /**< Chirp parameters. */
    int8_t             power_dbm; /**< Transmit power. */
    uint8_t            payload[LICP_FRAME_MAX_LEN]; /**< Frame. */
    size_t             len;       /**< Frame length. */
    uint32_t           airtime_us;/**< Computed time on air. */
} licp_tx_request_t;

/** @brief Radio statistics for telemetry. */
typedef struct {
    uint32_t rx_frames;   /**< Frames delivered. */
    uint32_t rx_errors;   /**< Receive errors. */
    uint32_t tx_ok;       /**< Transmissions accepted. */
    uint32_t tx_rejected; /**< Transmissions refused (too late, busy). */
} licp_radio_stats_t;

/** @brief Operations a radio back-end provides. All take the back-end's own context. */
typedef struct {
    const char *name; /**< Back-end name for logs. */
    /** Start the hardware. */
    licp_status_t (*start)(void *ctx);
    /** Fetch up to @p max received frames; returns the number fetched (non-blocking). */
    size_t (*receive)(void *ctx, licp_rx_frame_t *out, size_t max);
    /** Queue a transmission at an absolute time. */
    licp_status_t (*transmit)(void *ctx, const licp_tx_request_t *req);
    /** Read statistics. */
    void (*stats)(void *ctx, licp_radio_stats_t *st);
    /** Stop the hardware. */
    void (*stop)(void *ctx);
} licp_radio_ops_t;

/** @brief A back-end instance: operations plus their context. */
typedef struct {
    const licp_radio_ops_t *ops; /**< Operations. */
    void                   *ctx; /**< Back-end state. */
} licp_radio_t;

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_HAL_RADIO_H */
