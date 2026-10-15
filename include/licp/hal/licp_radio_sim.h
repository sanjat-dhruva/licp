/**
 * @file licp_radio_sim.h
 * @brief In-memory radio back-end for unit tests and ground simulation.
 *
 * Received frames are injected with ::licp_radio_sim_inject; transmissions
 * requested by the service are recorded and can be inspected.
 *
 * @ingroup hal
 */
#ifndef LICP_HAL_RADIO_SIM_H
#define LICP_HAL_RADIO_SIM_H

#include <stddef.h>

#include "licp/hal/licp_radio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup hal
 *  @{ */

#define LICP_SIM_RX_CAP 512u /**< Frames that can wait in the receive queue. */
#define LICP_SIM_TX_CAP 512u /**< Transmissions recorded. */

/** @brief Simulator state. */
typedef struct {
    licp_rx_frame_t    rx[LICP_SIM_RX_CAP]; /**< Receive queue (FIFO). */
    size_t             rx_head;             /**< Next frame to deliver. */
    size_t             rx_count;            /**< Frames queued. */
    licp_tx_request_t  tx[LICP_SIM_TX_CAP]; /**< Recorded transmissions. */
    size_t             tx_count;            /**< Transmissions recorded. */
    licp_radio_stats_t st;                  /**< Statistics. */
    int                started;             /**< Non-zero after start. */
} licp_radio_sim_t;

/** @brief Operations table of the simulator. */
extern const licp_radio_ops_t licp_radio_sim_ops;

/** @brief Reset @p sim and bind it into @p radio. */
void licp_radio_sim_init(licp_radio_sim_t *sim, licp_radio_t *radio);

/** @brief Queue a frame for reception. @return ::LICP_ERR_FULL if the queue is full. */
licp_status_t licp_radio_sim_inject(licp_radio_sim_t *sim, const licp_rx_frame_t *f);

/** @brief Forget recorded transmissions. */
void licp_radio_sim_clear_tx(licp_radio_sim_t *sim);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_HAL_RADIO_SIM_H */
