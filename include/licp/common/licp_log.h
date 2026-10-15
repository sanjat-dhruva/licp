/**
 * @file licp_log.h
 * @brief Minimal levelled logger with a replaceable output sink.
 *
 * By default messages go to stderr. The service replaces the sink with one
 * that writes to syslog or to the telemetry buffer sent to the ground.
 *
 * @ingroup common
 */
#ifndef LICP_COMMON_LOG_H
#define LICP_COMMON_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup common
 *  @{ */

/** @brief Log severity, lowest value = most severe. */
typedef enum {
    LICP_LOG_ERROR = 0, /**< Something failed; the service continues if it can. */
    LICP_LOG_WARN  = 1, /**< Unexpected but handled. */
    LICP_LOG_INFO  = 2, /**< Normal operating events. */
    LICP_LOG_DEBUG = 3  /**< Detailed tracing for ground testing. */
} licp_log_level_t;

/**
 * @brief Output sink signature.
 * @param level  Severity of the message.
 * @param module Short module tag, e.g. "store".
 * @param msg    Formatted, NUL-terminated message (no trailing newline).
 * @param user   Opaque pointer given to ::licp_log_set_sink.
 */
typedef void (*licp_log_sink_fn)(licp_log_level_t level, const char *module, const char *msg, void *user);

/** @brief Set the most verbose level that is emitted (default ::LICP_LOG_INFO). */
void licp_log_set_level(licp_log_level_t level);

/** @brief Current maximum level. */
licp_log_level_t licp_log_get_level(void);

/** @brief Replace the sink; pass NULL to restore the stderr sink. */
void licp_log_set_sink(licp_log_sink_fn sink, void *user);

/**
 * @brief Emit a printf-style message if @p level is enabled.
 * @param level  Severity.
 * @param module Module tag.
 * @param fmt    printf format string.
 */
void licp_log(licp_log_level_t level, const char *module, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

/** @brief Log an error. */
#define LICP_LOGE(mod, ...) licp_log(LICP_LOG_ERROR, (mod), __VA_ARGS__)
/** @brief Log a warning. */
#define LICP_LOGW(mod, ...) licp_log(LICP_LOG_WARN, (mod), __VA_ARGS__)
/** @brief Log an informational message. */
#define LICP_LOGI(mod, ...) licp_log(LICP_LOG_INFO, (mod), __VA_ARGS__)
/** @brief Log a debug message. */
#define LICP_LOGD(mod, ...) licp_log(LICP_LOG_DEBUG, (mod), __VA_ARGS__)

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_COMMON_LOG_H */
