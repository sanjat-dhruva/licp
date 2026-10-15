/**
 * @file licp_airtime.c
 * @brief Chirp time-on-air calculation in integer arithmetic.
 * @ingroup lorawan
 */
#include "licp/lorawan/licp_airtime.h"

#include <stddef.h>

uint32_t licp_lora_airtime_us(const licp_lora_params_t *p, uint32_t payload_len)
{
    if (p == NULL || p->sf < 7u || p->sf > 12u || p->cr < 1u || p->cr > 4u ||
        (p->bw_hz != 125000u && p->bw_hz != 250000u && p->bw_hz != 500000u)) {
        return 0u;
    }
    /* Symbol time in nanoseconds: 2^SF / BW (exact for these bandwidths). */
    uint64_t tsym_ns = ((uint64_t)1u << p->sf) * 1000000000ull / p->bw_hz;
    int de = (tsym_ns >= 16000000ull) ? 1 : 0;
    int ih = p->explicit_header ? 0 : 1;
    int crc = p->crc_on ? 1 : 0;

    int num = 8 * (int)payload_len - 4 * (int)p->sf + 28 + 16 * crc - 20 * ih;
    int den = 4 * ((int)p->sf - 2 * de);
    int blocks = 0;
    if (num > 0) {
        blocks = (num + den - 1) / den; /* ceil for positive values */
    }
    uint64_t n_payload = 8u + (uint64_t)blocks * ((uint64_t)p->cr + 4u);

    /* Preamble: (N + 4.25) symbols, kept in quarter symbols for exactness. */
    uint64_t preamble_quarters = (uint64_t)p->preamble_syms * 4u + 17u;
    uint64_t total_ns = (preamble_quarters * tsym_ns) / 4u + n_payload * tsym_ns;
    return (uint32_t)((total_ns + 500u) / 1000u);
}
