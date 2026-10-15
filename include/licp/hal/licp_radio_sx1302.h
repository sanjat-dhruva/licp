/**
 * @file licp_radio_sx1302.h
 * @brief SX1302 concentrator back-end through Semtech libloragw (sx1302_hal).
 *
 * Built only with `-DLICP_WITH_LIBLORAGW=ON`. It provides:
 *  - timed chirp downlinks (beacon, broadcasts, one-to-one messages);
 *  - chirp reception on channels 0..7 of the radio plan, for early
 *    demonstrations and the ground fallback (satellite uplinks in the
 *    baseline design are frequency hopping only and arrive through the
 *    programmable-logic receiver, see licp_radio_fhss.h).
 *
 * Time mapping: the concentrator counts microseconds since start
 * (`count_us`). At each transmit or receive the back-end reads the counter
 * and the navigation clock together and converts between them. This gives
 * about one millisecond of accuracy, enough for the 3.5-second window.
 *
 * @note Not hardware-tested. Must be validated on the ZSOM-F01 with the
 *       radio cards (see docs/testing.md, deviations).
 *
 * @ingroup hal
 */
#ifndef LICP_HAL_RADIO_SX1302_H
#define LICP_HAL_RADIO_SX1302_H

#include "licp/hal/licp_clock.h"
#include "licp/hal/licp_radio.h"
#include "licp/region/licp_region.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup hal
 *  @{ */

/** @brief Back-end state. */
typedef struct {
    char                 spi_path[64]; /**< SPI device of the concentrator. */
    const licp_region_t *region;       /**< Radio plan (borrowed). */
    const licp_clock_t  *clock;        /**< Navigation clock (borrowed). */
    int                  started;      /**< Non-zero after start. */
    licp_radio_stats_t   st;           /**< Statistics. */
} licp_radio_sx1302_t;

/** @brief Operations table. */
extern const licp_radio_ops_t licp_radio_sx1302_ops;

/** @brief Prepare the back-end and bind it into @p radio. */
licp_status_t licp_radio_sx1302_init(licp_radio_sx1302_t *sx, const char *spi_path, const licp_region_t *region,
                                     const licp_clock_t *clock, licp_radio_t *radio);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_HAL_RADIO_SX1302_H */
