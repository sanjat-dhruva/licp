/**
 * @file licp_radio_sx1302.c
 * @brief SX1302 back-end implementation over Semtech libloragw.
 *
 * Compiled only when LICP_WITH_LIBLORAGW is enabled (see CMakeLists.txt).
 *
 * @ingroup hal
 */
#include "licp/hal/licp_radio_sx1302.h"

#include <string.h>

#include "licp/common/licp_log.h"
#include "loragw_hal.h"

/** Module tag used in log messages. */
static const char *TAG = "sx1302";

/** Chirp receive channels: 0..7 on two radio front-ends, four each. */
#define CHIRP_RX_CHANNELS 8u /* channels 0..7 on two radio front-ends, 4 each */

/** @brief libloragw data-rate code for a spreading factor. */
static uint32_t dr_of_sf(uint8_t sf)
{
    switch (sf) {
    case 7:  return DR_LORA_SF7;
    case 8:  return DR_LORA_SF8;
    case 9:  return DR_LORA_SF9;
    case 10: return DR_LORA_SF10;
    case 11: return DR_LORA_SF11;
    default: return DR_LORA_SF12;
    }
}

/** @brief libloragw bandwidth code for a bandwidth in hertz. */
static uint8_t bw_code(uint32_t hz)
{
    return (hz == 500000u) ? BW_500KHZ : (hz == 250000u) ? BW_250KHZ : BW_125KHZ;
}

/** @brief Configure board, radio front-ends and receive channels, then start the concentrator. */
static licp_status_t sx_start(void *ctx)
{
    licp_radio_sx1302_t *sx = (licp_radio_sx1302_t *)ctx;
    struct lgw_conf_board_s board;
    memset(&board, 0, sizeof(board));
    board.lorawan_public = true;
    board.clksrc = 0;
    board.full_duplex = false;
    board.com_type = LGW_COM_SPI;
    size_t plen = strlen(sx->spi_path);
    if (plen >= sizeof(board.com_path)) {
        return LICP_ERR_PARAM;
    }
    memcpy(board.com_path, sx->spi_path, plen + 1u);
    if (lgw_board_setconf(&board) != LGW_HAL_SUCCESS) {
        return LICP_ERR_IO;
    }

    /* Radio front-ends: centre of channels 0..3 and 4..7. TX uses radio 0. */
    uint32_t centre[2];
    for (uint8_t r = 0; r < 2u; ++r) {
        uint32_t lo = licp_region_channel_hz(sx->region, (uint8_t)(r * 4u));
        uint32_t hi = licp_region_channel_hz(sx->region, (uint8_t)(r * 4u + 3u));
        centre[r] = (lo + hi) / 2u;
        struct lgw_conf_rxrf_s rf;
        memset(&rf, 0, sizeof(rf));
        rf.enable = true;
        rf.freq_hz = centre[r];
        rf.rssi_offset = -215.4f;
        rf.type = LGW_RADIO_TYPE_SX1250;
        rf.tx_enable = (r == 0u);
        if (lgw_rxrf_setconf(r, &rf) != LGW_HAL_SUCCESS) {
            return LICP_ERR_IO;
        }
    }
    for (uint8_t i = 0; i < CHIRP_RX_CHANNELS && i < sx->region->channel_count; ++i) {
        struct lgw_conf_rxif_s ifc;
        memset(&ifc, 0, sizeof(ifc));
        uint8_t rf_chain = (uint8_t)(i / 4u);
        ifc.enable = true;
        ifc.rf_chain = rf_chain;
        ifc.freq_hz = (int32_t)licp_region_channel_hz(sx->region, i) - (int32_t)centre[rf_chain];
        if (lgw_rxif_setconf(i, &ifc) != LGW_HAL_SUCCESS) {
            return LICP_ERR_IO;
        }
    }
    if (lgw_start() != LGW_HAL_SUCCESS) {
        LICP_LOGE(TAG, "lgw_start failed on %s", sx->spi_path);
        return LICP_ERR_IO;
    }
    sx->started = 1;
    return LICP_OK;
}

/** @brief Fetch received chirp frames and convert their timestamps to navigation time. */
static size_t sx_receive(void *ctx, licp_rx_frame_t *out, size_t max)
{
    licp_radio_sx1302_t *sx = (licp_radio_sx1302_t *)ctx;
    if (!sx->started || max == 0u) {
        return 0;
    }
    struct lgw_pkt_rx_s pkts[16];
    uint8_t want = (uint8_t)((max < 16u) ? max : 16u);
    int n = lgw_receive(want, pkts);
    if (n <= 0) {
        if (n < 0) {
            sx->st.rx_errors++;
        }
        return 0;
    }
    uint32_t cnt_now = 0;
    (void)lgw_get_instcnt(&cnt_now);
    uint64_t now_ms = licp_clock_now(sx->clock);
    size_t k = 0;
    for (int i = 0; i < n; ++i) {
        const struct lgw_pkt_rx_s *p = &pkts[i];
        if (p->status != STAT_CRC_OK || p->size > LICP_FRAME_MAX_LEN) {
            sx->st.rx_errors++;
            continue;
        }
        licp_rx_frame_t *f = &out[k++];
        memset(f, 0, sizeof(*f));
        memcpy(f->phy, p->payload, p->size);
        f->len = p->size;
        uint32_t age_us = cnt_now - p->count_us; /* wraps correctly in 32 bits */
        f->gnss_ms = now_ms - age_us / 1000u;
        f->freq_hz = p->freq_hz;
        f->datarate = 2u; /* LICP_DR_CHIRP */
        f->rssi_x10 = (int16_t)(p->rssic * 10.0f);
        f->snr_x10 = (int16_t)(p->snr * 10.0f);
        sx->st.rx_frames++;
    }
    return k;
}

/** @brief Queue a timed transmission, converting navigation time to the concentrator counter. */
static licp_status_t sx_transmit(void *ctx, const licp_tx_request_t *req)
{
    licp_radio_sx1302_t *sx = (licp_radio_sx1302_t *)ctx;
    if (!sx->started) {
        return LICP_ERR_STATE;
    }
    uint32_t cnt_now = 0;
    (void)lgw_get_instcnt(&cnt_now);
    uint64_t now_ms = licp_clock_now(sx->clock);
    if (req->time_ms <= now_ms + 5u) {
        sx->st.tx_rejected++;
        return LICP_ERR_TIMEOUT; /* too late */
    }
    struct lgw_pkt_tx_s pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.freq_hz = req->freq_hz;
    pkt.tx_mode = TIMESTAMPED;
    pkt.count_us = cnt_now + (uint32_t)((req->time_ms - now_ms) * 1000u);
    pkt.rf_chain = 0;
    pkt.rf_power = req->power_dbm;
    pkt.modulation = MOD_LORA;
    pkt.bandwidth = bw_code(req->mod.bw_hz);
    pkt.datarate = dr_of_sf(req->mod.sf);
    pkt.coderate = (uint8_t)(CR_LORA_4_5 + (req->mod.cr - 1u));
    pkt.invert_pol = true; /* downlinks use inverted polarity */
    pkt.preamble = req->mod.preamble_syms;
    pkt.no_crc = !req->mod.crc_on;
    pkt.no_header = !req->mod.explicit_header;
    pkt.size = (uint16_t)req->len;
    memcpy(pkt.payload, req->payload, req->len);
    if (lgw_send(&pkt) != LGW_HAL_SUCCESS) {
        sx->st.tx_rejected++;
        return LICP_ERR_IO;
    }
    sx->st.tx_ok++;
    return LICP_OK;
}

/** @brief Copy statistics. */
static void sx_stats(void *ctx, licp_radio_stats_t *st)
{
    *st = ((licp_radio_sx1302_t *)ctx)->st;
}

/** @brief Stop the concentrator. */
static void sx_stop(void *ctx)
{
    licp_radio_sx1302_t *sx = (licp_radio_sx1302_t *)ctx;
    if (sx->started) {
        (void)lgw_stop();
        sx->started = 0;
    }
}

const licp_radio_ops_t licp_radio_sx1302_ops = {"sx1302", sx_start, sx_receive, sx_transmit, sx_stats, sx_stop};

licp_status_t licp_radio_sx1302_init(licp_radio_sx1302_t *sx, const char *spi_path, const licp_region_t *region,
                                     const licp_clock_t *clock, licp_radio_t *radio)
{
    if (sx == NULL || spi_path == NULL || region == NULL || clock == NULL || radio == NULL ||
        strlen(spi_path) >= sizeof(sx->spi_path)) {
        return LICP_ERR_PARAM;
    }
    memset(sx, 0, sizeof(*sx));
    memcpy(sx->spi_path, spi_path, strlen(spi_path) + 1u);
    sx->region = region;
    sx->clock = clock;
    radio->ops = &licp_radio_sx1302_ops;
    radio->ctx = sx;
    return LICP_OK;
}
