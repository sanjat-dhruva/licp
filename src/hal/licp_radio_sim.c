/**
 * @file licp_radio_sim.c
 * @brief In-memory radio back-end.
 * @ingroup hal
 */
#include "licp/hal/licp_radio_sim.h"

#include <string.h>

/** @brief Mark the simulator started. */
static licp_status_t sim_start(void *ctx)
{
    ((licp_radio_sim_t *)ctx)->started = 1;
    return LICP_OK;
}

/** @brief Deliver queued frames in injection order. */
static size_t sim_receive(void *ctx, licp_rx_frame_t *out, size_t max)
{
    licp_radio_sim_t *s = (licp_radio_sim_t *)ctx;
    size_t n = 0;
    while (n < max && s->rx_count > 0u) {
        out[n++] = s->rx[s->rx_head];
        s->rx_head = (s->rx_head + 1u) % LICP_SIM_RX_CAP;
        s->rx_count--;
        s->st.rx_frames++;
    }
    return n;
}

/** @brief Record a transmission request. */
static licp_status_t sim_transmit(void *ctx, const licp_tx_request_t *req)
{
    licp_radio_sim_t *s = (licp_radio_sim_t *)ctx;
    if (!s->started || s->tx_count >= LICP_SIM_TX_CAP) {
        s->st.tx_rejected++;
        return LICP_ERR_STATE;
    }
    s->tx[s->tx_count++] = *req;
    s->st.tx_ok++;
    return LICP_OK;
}

/** @brief Copy statistics. */
static void sim_stats(void *ctx, licp_radio_stats_t *st)
{
    *st = ((licp_radio_sim_t *)ctx)->st;
}

/** @brief Mark the simulator stopped. */
static void sim_stop(void *ctx)
{
    ((licp_radio_sim_t *)ctx)->started = 0;
}

const licp_radio_ops_t licp_radio_sim_ops = {"sim", sim_start, sim_receive, sim_transmit, sim_stats, sim_stop};

void licp_radio_sim_init(licp_radio_sim_t *sim, licp_radio_t *radio)
{
    memset(sim, 0, sizeof(*sim));
    radio->ops = &licp_radio_sim_ops;
    radio->ctx = sim;
}

licp_status_t licp_radio_sim_inject(licp_radio_sim_t *sim, const licp_rx_frame_t *f)
{
    if (sim == NULL || f == NULL) {
        return LICP_ERR_PARAM;
    }
    if (sim->rx_count >= LICP_SIM_RX_CAP) {
        return LICP_ERR_FULL;
    }
    sim->rx[(sim->rx_head + sim->rx_count) % LICP_SIM_RX_CAP] = *f;
    sim->rx_count++;
    return LICP_OK;
}

void licp_radio_sim_clear_tx(licp_radio_sim_t *sim)
{
    sim->tx_count = 0;
}
