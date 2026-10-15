/**
 * @file licp_clock.c
 * @brief Clock sources.
 * @ingroup hal
 */
#include "licp/hal/licp_clock.h"

#include <time.h>

/** @brief System clock converted to GPS milliseconds. */
static uint64_t system_now(void *ctx)
{
    const licp_clock_system_t *sys = (const licp_clock_system_t *)ctx;
    struct timespec ts;
    (void)clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t unix_ms = (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
    int64_t gps = (int64_t)unix_ms - (int64_t)LICP_GPS_UNIX_OFFSET_MS + (int64_t)sys->leap_seconds * 1000;
    return (gps > 0) ? (uint64_t)gps : 0u;
}

void licp_clock_system_init(licp_clock_system_t *sys, int32_t leap_seconds, licp_clock_t *clk)
{
    sys->leap_seconds = leap_seconds;
    clk->now_ms = system_now;
    clk->ctx = sys;
}

/** @brief Manual clock: returns the value last set. */
static uint64_t manual_now(void *ctx)
{
    return ((const licp_clock_manual_t *)ctx)->now;
}

void licp_clock_manual_init(licp_clock_manual_t *m, uint64_t start_ms, licp_clock_t *clk)
{
    m->now = start_ms;
    clk->now_ms = manual_now;
    clk->ctx = m;
}
