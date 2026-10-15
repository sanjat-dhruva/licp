/**
 * @file licp_superframe.h
 * @brief Superframe timing: 3.5-second beacon window followed by twelve
 *        3.5-second uplink slots (45.5 seconds in total), aligned to
 *        satellite navigation time.
 *
 * Superframe n starts at `epoch_ms + n * superframe_ms`. All times are in
 * milliseconds of satellite navigation time (GPS time scale, no leap seconds).
 *
 * @ingroup core
 */
#ifndef LICP_CORE_SUPERFRAME_H
#define LICP_CORE_SUPERFRAME_H

#include <stdbool.h>
#include <stdint.h>

#include "licp/common/licp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup core
 *  @{ */

/** @brief Timing configuration. */
typedef struct {
    uint64_t epoch_ms;         /**< Start of superframe 0. */
    uint32_t beacon_window_ms; /**< Beacon window length (3500). */
    uint32_t slot_ms;          /**< Uplink slot length (3500). */
    uint8_t  slot_count;       /**< Uplink slots per superframe (12). */
} licp_sf_config_t;

/** @brief Where a moment falls inside the superframe structure. */
typedef struct {
    uint32_t superframe;    /**< Superframe number. */
    bool     in_beacon;     /**< True inside the beacon window. */
    uint8_t  slot;          /**< Slot index 0..slot_count-1 (valid when !in_beacon). */
    bool     second_half;   /**< True in the second half of the slot (half-slot use). */
    uint32_t offset_ms;     /**< Offset from the start of the window or slot. */
    uint64_t next_beacon_ms;/**< Start of the next superframe. */
} licp_sf_position_t;

/** @brief Defaults: epoch 0, 3500 ms window, twelve 3500 ms slots. */
void licp_sf_default(licp_sf_config_t *c);

/** @brief Superframe length in milliseconds. */
uint32_t licp_sf_length_ms(const licp_sf_config_t *c);

/** @brief Check the configuration (non-zero lengths, 1..64 slots). */
licp_status_t licp_sf_validate(const licp_sf_config_t *c);

/**
 * @brief Locate @p now_ms in the superframe structure.
 * @return ::LICP_OK, ::LICP_ERR_PARAM, or ::LICP_ERR_STATE if @p now_ms is before the epoch.
 */
licp_status_t licp_sf_locate(const licp_sf_config_t *c, uint64_t now_ms, licp_sf_position_t *pos);

/** @brief Start time of superframe @p n. */
uint64_t licp_sf_start_ms(const licp_sf_config_t *c, uint32_t n);

/** @brief Start time of slot @p slot in superframe @p n. */
uint64_t licp_sf_slot_start_ms(const licp_sf_config_t *c, uint32_t n, uint8_t slot);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_CORE_SUPERFRAME_H */
