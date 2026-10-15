/**
 * @file licp_watchdog.c
 * @brief Watchdog implementation.
 * @ingroup hal
 */
#include "licp/hal/licp_watchdog.h"

#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>

licp_status_t licp_watchdog_open(licp_watchdog_t *w, const char *path)
{
    if (w == NULL) {
        return LICP_ERR_PARAM;
    }
    w->fd = -1;
    if (path == NULL || path[0] == '\0') {
        return LICP_OK;
    }
    w->fd = open(path, O_WRONLY);
    return (w->fd >= 0) ? LICP_OK : LICP_ERR_IO;
}

void licp_watchdog_kick(licp_watchdog_t *w)
{
    if (w != NULL && w->fd >= 0) {
        if (write(w->fd, "k", 1) != 1) {
            /* A missed kick is tolerated; the next poll retries. */
        }
    }
}

void licp_watchdog_close(licp_watchdog_t *w)
{
    if (w != NULL && w->fd >= 0) {
        if (write(w->fd, "V", 1) != 1) {
            /* Magic close failed: the timer keeps running and will reset the board. */
        }
        (void)close(w->fd);
        w->fd = -1;
    }
}
