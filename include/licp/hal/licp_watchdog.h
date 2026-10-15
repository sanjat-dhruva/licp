/**
 * @file licp_watchdog.h
 * @brief Linux hardware watchdog (/dev/watchdog). If the service stops
 *        kicking it, the board resets.
 * @ingroup hal
 */
#ifndef LICP_HAL_WATCHDOG_H
#define LICP_HAL_WATCHDOG_H

#include "licp/common/licp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup hal
 *  @{ */

/** @brief Watchdog handle. */
typedef struct {
    int fd; /**< Device descriptor, -1 when disabled. */
} licp_watchdog_t;

/** @brief Open @p path; NULL or "" gives a disabled (no-op) watchdog. */
licp_status_t licp_watchdog_open(licp_watchdog_t *w, const char *path);

/** @brief Kick the watchdog. */
void licp_watchdog_kick(licp_watchdog_t *w);

/** @brief Close cleanly (writes the magic 'V' so the timer stops). */
void licp_watchdog_close(licp_watchdog_t *w);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_HAL_WATCHDOG_H */
