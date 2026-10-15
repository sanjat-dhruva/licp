/**
 * @file licp_clock.h
 * @brief Satellite navigation time source.
 *
 * On the ZSOM-F01 the Linux system clock is disciplined to the navigation
 * receiver's pulse-per-second line (for example by chrony with a PPS
 * reference). ::licp_clock_system_init binds a source that converts that clock to the GPS time scale:
 * `gps_ms = unix_ms - 315 964 800 000 + leap_seconds * 1000`.
 * A manual clock is provided for tests and simulation.
 *
 * @ingroup hal
 */
#ifndef LICP_HAL_CLOCK_H
#define LICP_HAL_CLOCK_H

#include <stdint.h>

#include "licp/common/licp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup hal
 *  @{ */

#define LICP_GPS_UNIX_OFFSET_MS 315964800000ull /**< 1980-01-06 minus 1970-01-01, in milliseconds. */

/** @brief Clock source. */
typedef struct {
    /** Return current satellite navigation time in milliseconds. */
    uint64_t (*now_ms)(void *ctx);
    void *ctx; /**< Source state. */
} licp_clock_t;

/** @brief System-clock source settings. */
typedef struct {
    int32_t leap_seconds; /**< GPS minus UTC, 18 s since 2017. */
} licp_clock_system_t;

/** @brief Bind the system clock into @p clk. */
void licp_clock_system_init(licp_clock_system_t *sys, int32_t leap_seconds, licp_clock_t *clk);

/** @brief Manual clock for tests: time only moves when set. */
typedef struct {
    uint64_t now; /**< Current time in milliseconds. */
} licp_clock_manual_t;

/** @brief Bind a manual clock starting at @p start_ms. */
void licp_clock_manual_init(licp_clock_manual_t *m, uint64_t start_ms, licp_clock_t *clk);

/** @brief Convenience: read a clock. */
static inline uint64_t licp_clock_now(const licp_clock_t *c)
{
    return c->now_ms(c->ctx);
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_HAL_CLOCK_H */
