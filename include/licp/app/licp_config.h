/**
 * @file licp_config.h
 * @brief Service configuration: defaults and a `key = value` file parser.
 *
 * See config/licp.conf.example for every key with its default.
 *
 * @ingroup app
 */
#ifndef LICP_APP_CONFIG_H
#define LICP_APP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "licp/common/licp_status.h"
#include "licp/core/licp_downlink.h"
#include "licp/core/licp_load.h"
#include "licp/core/licp_pipeline.h"
#include "licp/core/licp_superframe.h"
#include "licp/region/licp_region.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup app Application
 * @brief Configuration, service orchestration and the main program.
 * @{
 */

#define LICP_CFG_PATH_LEN 256u /**< Maximum path length in the configuration. */

/** @brief Complete service configuration. */
typedef struct {
    uint8_t                sat_id;                           /**< This satellite's identifier. */
    uint8_t                group_key[16];                    /**< Beacon and bitmap signing key (replaced by plan). */
    licp_region_t          region;                           /**< Radio plan. */
    licp_sf_config_t       sf;                               /**< Superframe timing. */
    licp_load_config_t     load;                             /**< Access-probability controller. */
    licp_pipeline_config_t pipeline;                         /**< Frame checks. */
    licp_dl_config_t       downlink;                         /**< Beacon-window planner. */
    char     store_path[LICP_CFG_PATH_LEN];                  /**< Store-and-forward log file. */
    uint64_t store_max_bytes;                                /**< Log size limit. */
    bool     store_fsync_each;                               /**< fsync after every frame. */
    char     device_table_path[LICP_CFG_PATH_LEN];           /**< Saved device table. */
    uint32_t device_capacity;                                /**< Devices that fit in the table. */
    uint16_t bitmap_size;                                    /**< Bits in the acknowledgement bitmap. */
    uint32_t pass_gap_ms;                                    /**< Quiet time that starts a new pass. */
    uint32_t plan_ahead_ms;                                  /**< Plan a beacon window this early. */
    char     rx_backend[16];                                 /**< "sim", "fhss" or "sx1302". */
    char     fhss_device[LICP_CFG_PATH_LEN];                 /**< Frequency-hopping receiver device. */
    char     tx_backend[16];                                 /**< "sim" or "sx1302". */
    char     sx1302_spi[LICP_CFG_PATH_LEN];                  /**< SPI device of the active radio card. */
    uint16_t obc_tcp_port;                                   /**< Listening port for the on-board computer link (0 = off). */
    char     watchdog_path[LICP_CFG_PATH_LEN];               /**< Watchdog device ("" = off). */
    int32_t  leap_seconds;                                   /**< GPS minus UTC. */
    int      log_level;                                      /**< 0 error .. 3 debug. */
} licp_config_t;

/** @brief Fill @p c with defaults (the values in config/licp.conf.example). */
void licp_config_default(licp_config_t *c);

/**
 * @brief Apply one `key = value` setting.
 * @return ::LICP_OK, ::LICP_ERR_NOT_FOUND for an unknown key, ::LICP_ERR_PARAM for a bad value.
 */
licp_status_t licp_config_set(licp_config_t *c, const char *key, const char *value);

/**
 * @brief Read a configuration file on top of the current values.
 *        Blank lines and lines starting with '#' are ignored.
 * @param c Configuration to update.
 * @param path File path.
 * @param[out] bad_line First line that failed (0 if none; may be NULL).
 * @return ::LICP_OK, ::LICP_ERR_IO, or the error of the first bad line.
 */
licp_status_t licp_config_load(licp_config_t *c, const char *path, int *bad_line);

/** @brief Check cross-field consistency (calls the module validators). */
licp_status_t licp_config_validate(const licp_config_t *c);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_APP_CONFIG_H */
