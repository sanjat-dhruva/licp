/**
 * @file licp_superframe.c
 * @brief Superframe timing implementation.
 * @ingroup core
 */
#include "licp/core/licp_superframe.h"

#include <stddef.h>

void licp_sf_default(licp_sf_config_t *c)
{
    c->epoch_ms = 0u;
    c->beacon_window_ms = 3500u;
    c->slot_ms = 3500u;
    c->slot_count = 12u;
}

uint32_t licp_sf_length_ms(const licp_sf_config_t *c)
{
    return c->beacon_window_ms + (uint32_t)c->slot_count * c->slot_ms;
}

licp_status_t licp_sf_validate(const licp_sf_config_t *c)
{
    if (c == NULL || c->beacon_window_ms == 0u || c->slot_ms == 0u || c->slot_count == 0u || c->slot_count > 64u) {
        return LICP_ERR_PARAM;
    }
    return LICP_OK;
}

licp_status_t licp_sf_locate(const licp_sf_config_t *c, uint64_t now_ms, licp_sf_position_t *pos)
{
    if (pos == NULL || licp_sf_validate(c) != LICP_OK) {
        return LICP_ERR_PARAM;
    }
    if (now_ms < c->epoch_ms) {
        return LICP_ERR_STATE;
    }
    uint64_t len = licp_sf_length_ms(c);
    uint64_t since = now_ms - c->epoch_ms;
    uint64_t n = since / len;
    if (n > UINT32_MAX) {
        return LICP_ERR_STATE;
    }
    uint32_t in_sf = (uint32_t)(since % len);

    pos->superframe = (uint32_t)n;
    pos->next_beacon_ms = c->epoch_ms + (n + 1u) * len;
    if (in_sf < c->beacon_window_ms) {
        pos->in_beacon = true;
        pos->slot = 0;
        pos->second_half = in_sf >= c->beacon_window_ms / 2u;
        pos->offset_ms = in_sf;
    } else {
        uint32_t t = in_sf - c->beacon_window_ms;
        pos->in_beacon = false;
        pos->slot = (uint8_t)(t / c->slot_ms);
        pos->offset_ms = t % c->slot_ms;
        pos->second_half = pos->offset_ms >= c->slot_ms / 2u;
    }
    return LICP_OK;
}

uint64_t licp_sf_start_ms(const licp_sf_config_t *c, uint32_t n)
{
    return c->epoch_ms + (uint64_t)n * licp_sf_length_ms(c);
}

uint64_t licp_sf_slot_start_ms(const licp_sf_config_t *c, uint32_t n, uint8_t slot)
{
    return licp_sf_start_ms(c, n) + c->beacon_window_ms + (uint64_t)slot * c->slot_ms;
}
