/**
 * @file licp_config.c
 * @brief Configuration defaults and parser.
 *
 * To add a key: add a field to ::licp_config_t, set its default in
 * ::licp_config_default, add one line to the table in ::licp_config_set and
 * document it in config/licp.conf.example.
 *
 * @ingroup app
 */
#include "licp/app/licp_config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void licp_config_default(licp_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->sat_id = 1u;
    licp_region_default(&c->region);
    licp_sf_default(&c->sf);
    licp_load_default_config(&c->load);
    licp_pipeline_default_config(&c->pipeline);
    licp_dl_default_config(&c->downlink);
    (void)snprintf(c->store_path, sizeof(c->store_path), "%s", "/var/lib/licp/frames.log");
    c->store_max_bytes = 64ull * 1024ull * 1024ull;
    c->store_fsync_each = true;
    (void)snprintf(c->device_table_path, sizeof(c->device_table_path), "%s", "/var/lib/licp/devices.bin");
    c->device_capacity = 4096u;
    c->bitmap_size = 1024u;
    c->pass_gap_ms = 600000u;
    c->plan_ahead_ms = 500u;
    (void)snprintf(c->rx_backend, sizeof(c->rx_backend), "%s", "fhss");
    (void)snprintf(c->fhss_device, sizeof(c->fhss_device), "%s", "/dev/licp_fhss0");
    (void)snprintf(c->tx_backend, sizeof(c->tx_backend), "%s", "sx1302");
    (void)snprintf(c->sx1302_spi, sizeof(c->sx1302_spi), "%s", "/dev/spidev0.0");
    c->obc_tcp_port = 5600u;
    c->watchdog_path[0] = '\0';
    c->leap_seconds = 18;
    c->log_level = 2;
}

/* ---------------------------------------------------------------------- */
/* Value parsers                                                           */
/* ---------------------------------------------------------------------- */

/** @brief Parse an unsigned integer (decimal, 0x hex or 0 octal) no larger than @p max. */
static licp_status_t parse_u64(const char *v, uint64_t max, uint64_t *out)
{
    char *end = NULL;
    errno = 0;
    unsigned long long x = strtoull(v, &end, 0);
    if (errno != 0 || end == v || *end != '\0' || v[0] == '-' || x > max) {
        return LICP_ERR_PARAM;
    }
    *out = (uint64_t)x;
    return LICP_OK;
}

/** @brief Parse a signed integer within [@p min, @p max]. */
static licp_status_t parse_i64(const char *v, int64_t min, int64_t max, int64_t *out)
{
    char *end = NULL;
    errno = 0;
    long long x = strtoll(v, &end, 0);
    if (errno != 0 || end == v || *end != '\0' || x < min || x > max) {
        return LICP_ERR_PARAM;
    }
    *out = (int64_t)x;
    return LICP_OK;
}

/** @brief Parse a floating-point value within [@p min, @p max]. */
static licp_status_t parse_double(const char *v, double min, double max, double *out)
{
    char *end = NULL;
    errno = 0;
    double x = strtod(v, &end);
    if (errno != 0 || end == v || *end != '\0' || x < min || x > max) {
        return LICP_ERR_PARAM;
    }
    *out = x;
    return LICP_OK;
}

/** @brief Parse 1/0, true/false or yes/no. */
static licp_status_t parse_bool(const char *v, bool *out)
{
    if (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "yes") == 0) {
        *out = true;
        return LICP_OK;
    }
    if (strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "no") == 0) {
        *out = false;
        return LICP_OK;
    }
    return LICP_ERR_PARAM;
}

/** @brief Copy a string that fits in @p cap bytes including the terminator. */
static licp_status_t parse_str(const char *v, char *out, size_t cap)
{
    size_t n = strlen(v);
    if (n >= cap) {
        return LICP_ERR_PARAM;
    }
    memcpy(out, v, n + 1u);
    return LICP_OK;
}

/** @brief Parse a 16-byte key written as 32 hexadecimal digits. */
static licp_status_t parse_key16(const char *v, uint8_t out[16])
{
    if (strlen(v) != 32u) {
        return LICP_ERR_PARAM;
    }
    for (unsigned i = 0; i < 16u; ++i) {
        char byte[3] = {v[2u * i], v[2u * i + 1u], '\0'};
        if (!isxdigit((unsigned char)byte[0]) || !isxdigit((unsigned char)byte[1])) {
            return LICP_ERR_PARAM;
        }
        out[i] = (uint8_t)strtoul(byte, NULL, 16);
    }
    return LICP_OK;
}

/* Typed setters used by the key table. */
/** @brief Parse @c value as unsigned (at most @p maxv) into @p field, or return ::LICP_ERR_PARAM. */
#define SET_UNSIGNED(field, maxv)                                                                  \
    do {                                                                                           \
        uint64_t x_;                                                                               \
        if (parse_u64(value, (maxv), &x_) != LICP_OK) return LICP_ERR_PARAM;                       \
        (field) = (__typeof__(field))x_;                                                           \
    } while (0)

/** @brief Parse @c value as a double in [@p lo, @p hi] into @p field, or return ::LICP_ERR_PARAM. */
#define SET_DOUBLE(field, lo, hi)                                                                  \
    do {                                                                                           \
        if (parse_double(value, (lo), (hi), &(field)) != LICP_OK) return LICP_ERR_PARAM;           \
    } while (0)

licp_status_t licp_config_set(licp_config_t *c, const char *key, const char *value)
{
    if (c == NULL || key == NULL || value == NULL) {
        return LICP_ERR_PARAM;
    }
    /* Identity and keys */
    if (strcmp(key, "sat_id") == 0) { SET_UNSIGNED(c->sat_id, 255u); return LICP_OK; }
    if (strcmp(key, "group_key") == 0) { return parse_key16(value, c->group_key); }
    /* Radio plan */
    if (strcmp(key, "first_channel_hz") == 0) { SET_UNSIGNED(c->region.first_channel_hz, 3000000000u); return LICP_OK; }
    if (strcmp(key, "channel_spacing_hz") == 0) { SET_UNSIGNED(c->region.channel_spacing_hz, 10000000u); return LICP_OK; }
    if (strcmp(key, "channel_count") == 0) { SET_UNSIGNED(c->region.channel_count, LICP_REGION_MAX_CHANNELS); return LICP_OK; }
    if (strcmp(key, "sos_channels") == 0) {
        /* comma-separated channel indices */
        char buf[64];
        if (parse_str(value, buf, sizeof(buf)) != LICP_OK) return LICP_ERR_PARAM;
        uint8_t n = 0;
        for (char *tok = strtok(buf, ","); tok != NULL; tok = strtok(NULL, ",")) {
            uint64_t ch;
            if (n >= LICP_REGION_MAX_SOS || parse_u64(tok, 255u, &ch) != LICP_OK) return LICP_ERR_PARAM;
            c->region.sos_channels[n++] = (uint8_t)ch;
        }
        c->region.sos_count = n;
        return LICP_OK;
    }
    if (strcmp(key, "downlink_hz") == 0) { SET_UNSIGNED(c->region.downlink_hz, 3000000000u); return LICP_OK; }
    if (strcmp(key, "downlink_sf") == 0) { SET_UNSIGNED(c->region.downlink.sf, 12u); return LICP_OK; }
    if (strcmp(key, "downlink_bw_hz") == 0) { SET_UNSIGNED(c->region.downlink.bw_hz, 500000u); return LICP_OK; }
    if (strcmp(key, "downlink_power_dbm") == 0) {
        int64_t x;
        if (parse_i64(value, -10, 30, &x) != LICP_OK) return LICP_ERR_PARAM;
        c->region.downlink_power_dbm = (int8_t)x;
        return LICP_OK;
    }
    if (strcmp(key, "downlink_max_payload") == 0) { SET_UNSIGNED(c->region.downlink_max_payload, 255u); return LICP_OK; }
    /* Timing */
    if (strcmp(key, "superframe_epoch_ms") == 0) { SET_UNSIGNED(c->sf.epoch_ms, UINT64_MAX); return LICP_OK; }
    if (strcmp(key, "beacon_window_ms") == 0) { SET_UNSIGNED(c->sf.beacon_window_ms, 60000u); return LICP_OK; }
    if (strcmp(key, "slot_ms") == 0) { SET_UNSIGNED(c->sf.slot_ms, 60000u); return LICP_OK; }
    if (strcmp(key, "slot_count") == 0) { SET_UNSIGNED(c->sf.slot_count, 64u); return LICP_OK; }
    if (strcmp(key, "plan_ahead_ms") == 0) { SET_UNSIGNED(c->plan_ahead_ms, 10000u); return LICP_OK; }
    if (strcmp(key, "pass_gap_ms") == 0) { SET_UNSIGNED(c->pass_gap_ms, 86400000u); return LICP_OK; }
    /* Load control */
    if (strcmp(key, "collision_high") == 0) { SET_DOUBLE(c->load.collision_high, 0.0, 1.0); return LICP_OK; }
    if (strcmp(key, "collision_low") == 0) { SET_DOUBLE(c->load.collision_low, 0.0, 1.0); return LICP_OK; }
    if (strcmp(key, "decrease_factor") == 0) { SET_DOUBLE(c->load.decrease_factor, 0.01, 0.99); return LICP_OK; }
    if (strcmp(key, "increase_step") == 0) { SET_DOUBLE(c->load.increase_step, 0.001, 1.0); return LICP_OK; }
    /* Frame checks */
    if (strcmp(key, "max_fcnt_gap") == 0) { SET_UNSIGNED(c->pipeline.max_fcnt_gap, 0x7FFFFFFFu); return LICP_OK; }
    if (strcmp(key, "freq_tolerance_hz") == 0) { SET_UNSIGNED(c->pipeline.freq_tolerance_hz, 100000u); return LICP_OK; }
    /* Downlink planner */
    if (strcmp(key, "dl_lead_ms") == 0) { SET_UNSIGNED(c->downlink.lead_ms, 3000u); return LICP_OK; }
    if (strcmp(key, "dl_gap_ms") == 0) { SET_UNSIGNED(c->downlink.gap_ms, 1000u); return LICP_OK; }
    if (strcmp(key, "dl_max_call_ins") == 0) { SET_UNSIGNED(c->downlink.max_call_ins, 255u); return LICP_OK; }
    /* Storage */
    if (strcmp(key, "store_path") == 0) { return parse_str(value, c->store_path, sizeof(c->store_path)); }
    if (strcmp(key, "store_max_bytes") == 0) { SET_UNSIGNED(c->store_max_bytes, UINT64_MAX); return LICP_OK; }
    if (strcmp(key, "store_fsync_each") == 0) { return parse_bool(value, &c->store_fsync_each); }
    if (strcmp(key, "device_table_path") == 0) { return parse_str(value, c->device_table_path, sizeof(c->device_table_path)); }
    if (strcmp(key, "device_capacity") == 0) { SET_UNSIGNED(c->device_capacity, 1000000u); return LICP_OK; }
    if (strcmp(key, "bitmap_size") == 0) { SET_UNSIGNED(c->bitmap_size, LICP_BITMAP_MAX_BITS); return LICP_OK; }
    /* Back-ends and system */
    if (strcmp(key, "rx_backend") == 0) { return parse_str(value, c->rx_backend, sizeof(c->rx_backend)); }
    if (strcmp(key, "fhss_device") == 0) { return parse_str(value, c->fhss_device, sizeof(c->fhss_device)); }
    if (strcmp(key, "tx_backend") == 0) { return parse_str(value, c->tx_backend, sizeof(c->tx_backend)); }
    if (strcmp(key, "sx1302_spi") == 0) { return parse_str(value, c->sx1302_spi, sizeof(c->sx1302_spi)); }
    if (strcmp(key, "obc_tcp_port") == 0) { SET_UNSIGNED(c->obc_tcp_port, 65535u); return LICP_OK; }
    if (strcmp(key, "watchdog_path") == 0) { return parse_str(value, c->watchdog_path, sizeof(c->watchdog_path)); }
    if (strcmp(key, "leap_seconds") == 0) {
        int64_t x;
        if (parse_i64(value, 0, 100, &x) != LICP_OK) return LICP_ERR_PARAM;
        c->leap_seconds = (int32_t)x;
        return LICP_OK;
    }
    if (strcmp(key, "log_level") == 0) {
        int64_t x;
        if (parse_i64(value, 0, 3, &x) != LICP_OK) return LICP_ERR_PARAM;
        c->log_level = (int)x;
        return LICP_OK;
    }
    return LICP_ERR_NOT_FOUND;
}

/** @brief Strip leading and trailing white space in place. @return Pointer to the first non-space character. */
static char *trim(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s)) {
        ++s;
    }
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) {
        *--e = '\0';
    }
    return s;
}

licp_status_t licp_config_load(licp_config_t *c, const char *path, int *bad_line)
{
    if (bad_line != NULL) {
        *bad_line = 0;
    }
    if (c == NULL || path == NULL) {
        return LICP_ERR_PARAM;
    }
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return LICP_ERR_IO;
    }
    char line[512];
    int lineno = 0;
    licp_status_t st = LICP_OK;
    while (fgets(line, sizeof(line), f) != NULL) {
        ++lineno;
        char *s = trim(line);
        if (*s == '\0' || *s == '#') {
            continue;
        }
        char *eq = strchr(s, '=');
        if (eq == NULL) {
            st = LICP_ERR_FORMAT;
        } else {
            *eq = '\0';
            st = licp_config_set(c, trim(s), trim(eq + 1));
        }
        if (st != LICP_OK) {
            if (bad_line != NULL) {
                *bad_line = lineno;
            }
            break;
        }
    }
    (void)fclose(f);
    return st;
}

licp_status_t licp_config_validate(const licp_config_t *c)
{
    if (c == NULL || licp_region_validate(&c->region) != LICP_OK || licp_sf_validate(&c->sf) != LICP_OK ||
        c->store_path[0] == '\0' || c->store_max_bytes < 1024u || c->device_capacity == 0u ||
        c->plan_ahead_ms >= licp_sf_length_ms(&c->sf) || c->downlink.lead_ms >= c->sf.beacon_window_ms) {
        return LICP_ERR_PARAM;
    }
    licp_load_t probe;
    if (licp_load_init(&probe, &c->load) != LICP_OK) {
        return LICP_ERR_PARAM;
    }
    return LICP_OK;
}
