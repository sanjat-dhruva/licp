/**
 * @file licp_region.h
 * @brief Project radio plan for the 855–858 megahertz band.
 *
 * Defaults follow "S-MIoT LEO Network – How It Works" (7 Oct 2026):
 *  - 15 channels of 200 kilohertz, centres 855.1, 855.3 … 857.9 megahertz;
 *  - emergency channels 855.1 and 856.7 megahertz;
 *  - one downlink channel, proposed 856.5 megahertz;
 *  - satellite uplinks use frequency hopping (137 kilohertz operating channel)
 *    at coding rate 1/3 (robust) or 2/3 (fast).
 *
 * All values live in ::licp_region_t so that a different band or channel
 * split can be loaded from configuration without code changes.
 *
 * @ingroup region
 */
#ifndef LICP_REGION_REGION_H
#define LICP_REGION_REGION_H

#include <stdbool.h>
#include <stdint.h>

#include "licp/common/licp_status.h"
#include "licp/lorawan/licp_airtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup region Radio plan
 * @brief Channels, emergency channels and downlink parameters.
 * @{
 */

#define LICP_REGION_MAX_CHANNELS 16u /**< Upper bound on channels in a plan. */
#define LICP_REGION_MAX_SOS       4u /**< Upper bound on emergency channels. */

/** @brief Satellite uplink data rates (frequency hopping). */
typedef enum {
    LICP_DR_FHSS_ROBUST = 0, /**< Coding rate 1/3, about 162 bits per second. */
    LICP_DR_FHSS_FAST   = 1, /**< Coding rate 2/3, about 325 bits per second. */
    LICP_DR_CHIRP       = 2  /**< Chirp uplink (ground path or early demonstrations). */
} licp_uplink_dr_t;

/** @brief A complete radio plan. */
typedef struct {
    uint32_t           first_channel_hz;   /**< Centre of channel 0. */
    uint32_t           channel_spacing_hz; /**< Spacing between centres. */
    uint8_t            channel_count;      /**< Number of channels. */
    uint8_t            sos_channels[LICP_REGION_MAX_SOS]; /**< Emergency channel indices. */
    uint8_t            sos_count;          /**< Entries used in @ref sos_channels. */
    uint32_t           downlink_hz;        /**< Downlink centre frequency. */
    licp_lora_params_t downlink;           /**< Downlink chirp parameters. */
    int8_t             downlink_power_dbm; /**< Downlink transmit power. */
    uint8_t            downlink_max_payload; /**< Largest downlink physical payload. */
} licp_region_t;

/** @brief Fill @p r with the defaults described in the file header. */
void licp_region_default(licp_region_t *r);

/** @brief Check a plan for consistency (counts, ranges, emergency indices). */
licp_status_t licp_region_validate(const licp_region_t *r);

/**
 * @brief Centre frequency of a channel.
 * @return Frequency in hertz, or 0 if @p channel is out of range.
 */
uint32_t licp_region_channel_hz(const licp_region_t *r, uint8_t channel);

/**
 * @brief Map a measured frequency to a channel index.
 * @param r            Plan.
 * @param freq_hz      Measured centre frequency.
 * @param tolerance_hz Largest accepted offset (covers frequency shift).
 * @param[out] channel Channel index.
 * @return ::LICP_OK or ::LICP_ERR_NOT_FOUND.
 */
licp_status_t licp_region_channel_of(const licp_region_t *r, uint32_t freq_hz, uint32_t tolerance_hz,
                                     uint8_t *channel);

/** @brief True if @p channel is reserved for emergency frames. */
bool licp_region_is_sos_channel(const licp_region_t *r, uint8_t channel);

/** @brief Number of general (non-emergency) channels. */
uint8_t licp_region_general_count(const licp_region_t *r);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_REGION_REGION_H */
