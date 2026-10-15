/**
 * @file licp_log.c
 * @brief Logger implementation.
 * @ingroup common
 */
#include "licp/common/licp_log.h"

#include <stdarg.h>
#include <stdio.h>

/** Most verbose level emitted. */
static licp_log_level_t g_level = LICP_LOG_INFO;
/** Current sink, NULL for stderr. */
static licp_log_sink_fn g_sink = NULL;
/** User pointer passed to the sink. */
static void *g_sink_user = NULL;

/** @brief One-letter name of a level for the stderr sink. */
static const char *level_name(licp_log_level_t level)
{
    switch (level) {
    case LICP_LOG_ERROR: return "E";
    case LICP_LOG_WARN:  return "W";
    case LICP_LOG_INFO:  return "I";
    default:             return "D";
    }
}

void licp_log_set_level(licp_log_level_t level) { g_level = level; }

licp_log_level_t licp_log_get_level(void) { return g_level; }

void licp_log_set_sink(licp_log_sink_fn sink, void *user)
{
    g_sink = sink;
    g_sink_user = user;
}

void licp_log(licp_log_level_t level, const char *module, const char *fmt, ...)
{
    if (level > g_level) {
        return;
    }
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    if (g_sink != NULL) {
        g_sink(level, module ? module : "-", msg, g_sink_user);
    } else {
        (void)fprintf(stderr, "[%s] %-8s %s\n", level_name(level), module ? module : "-", msg);
    }
}
