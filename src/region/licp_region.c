/**
 * @file licp_region.c
 * @brief Radio plan implementation.
 * @ingroup region
 */
#include "licp/region/licp_region.h"

#include <string.h>

void licp_region_default(licp_region_t *r)
{
    memset(r, 0, sizeof(*r));
    r->first_channel_hz = 855100000u;
    r->channel_spacing_hz = 200000u;
    r->channel_count = 15u;
    r->sos_channels[0] = 0u; /* 855.1 MHz */
    r->sos_channels[1] = 8u; /* 856.7 MHz */
    r->sos_count = 2u;
    r->downlink_hz = 856500000u;
    r->downlink.sf = 10u;
    r->downlink.bw_hz = 125000u;
    r->downlink.cr = 1u;
    r->downlink.preamble_syms = 8u;
    r->downlink.explicit_header = true;
    r->downlink.crc_on = false;
    r->downlink_power_dbm = 27;
    r->downlink_max_payload = 115u;
}

licp_status_t licp_region_validate(const licp_region_t *r)
{
    if (r == NULL || r->channel_count == 0u || r->channel_count > LICP_REGION_MAX_CHANNELS ||
        r->channel_spacing_hz == 0u || r->sos_count > LICP_REGION_MAX_SOS || r->downlink_hz == 0u ||
        r->downlink_max_payload < 16u) {
        return LICP_ERR_PARAM;
    }
    for (uint8_t i = 0; i < r->sos_count; ++i) {
        if (r->sos_channels[i] >= r->channel_count) {
            return LICP_ERR_PARAM;
        }
    }
    if (licp_lora_airtime_us(&r->downlink, 1u) == 0u) {
        return LICP_ERR_PARAM;
    }
    return LICP_OK;
}

uint32_t licp_region_channel_hz(const licp_region_t *r, uint8_t channel)
{
    if (r == NULL || channel >= r->channel_count) {
        return 0u;
    }
    return r->first_channel_hz + (uint32_t)channel * r->channel_spacing_hz;
}

licp_status_t licp_region_channel_of(const licp_region_t *r, uint32_t freq_hz, uint32_t tolerance_hz,
                                     uint8_t *channel)
{
    if (r == NULL || channel == NULL) {
        return LICP_ERR_PARAM;
    }
    for (uint8_t i = 0; i < r->channel_count; ++i) {
        uint32_t c = licp_region_channel_hz(r, i);
        uint32_t diff = (freq_hz > c) ? (freq_hz - c) : (c - freq_hz);
        if (diff <= tolerance_hz) {
            *channel = i;
            return LICP_OK;
        }
    }
    return LICP_ERR_NOT_FOUND;
}

bool licp_region_is_sos_channel(const licp_region_t *r, uint8_t channel)
{
    if (r == NULL) {
        return false;
    }
    for (uint8_t i = 0; i < r->sos_count; ++i) {
        if (r->sos_channels[i] == channel) {
            return true;
        }
    }
    return false;
}

uint8_t licp_region_general_count(const licp_region_t *r)
{
    return (r == NULL) ? 0u : (uint8_t)(r->channel_count - r->sos_count);
}
