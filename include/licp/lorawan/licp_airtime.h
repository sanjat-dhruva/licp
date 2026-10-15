/**
 * @file licp_airtime.h
 * @brief Time on air of chirp (LoRa) frames, used to fit downlinks into the
 *        3.5-second beacon window.
 *
 * Formula from Semtech application note AN1200.13 ("LoRa Modem Designer's
 * Guide"):
 * @verbatim
 *  Tsym      = 2^SF / BW
 *  Tpreamble = (Npreamble + 4.25) * Tsym
 *  Npayload  = 8 + max( ceil( (8*PL - 4*SF + 28 + 16*CRC - 20*IH) / (4*(SF - 2*DE)) ) * (CR + 4), 0 )
 *  Tframe    = Tpreamble + Npayload * Tsym
 * @endverbatim
 * DE (low data rate optimisation) is on when Tsym >= 16 ms.
 *
 * @ingroup lorawan
 */
#ifndef LICP_LORAWAN_AIRTIME_H
#define LICP_LORAWAN_AIRTIME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup lorawan
 *  @{ */

/** @brief Chirp modulation parameters. */
typedef struct {
    uint8_t  sf;             /**< Spreading factor 7..12. */
    uint32_t bw_hz;          /**< Bandwidth in hertz (125000, 250000, 500000). */
    uint8_t  cr;             /**< Coding rate index 1..4 meaning 4/5..4/8. */
    uint16_t preamble_syms;  /**< Preamble length in symbols (8 for this network). */
    bool     explicit_header;/**< True for explicit header (normal). */
    bool     crc_on;         /**< Payload CRC present (downlinks: off in standard networks). */
} licp_lora_params_t;

/**
 * @brief Time on air in microseconds.
 * @param p           Modulation parameters.
 * @param payload_len Physical payload length in bytes.
 * @return Duration in microseconds, or 0 if parameters are invalid.
 */
uint32_t licp_lora_airtime_us(const licp_lora_params_t *p, uint32_t payload_len);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_LORAWAN_AIRTIME_H */
