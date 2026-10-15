/**
 * @file licp_load.c
 * @brief Access-probability controller.
 * @ingroup core
 */
#include "licp/core/licp_load.h"

#include <stddef.h>
#include <string.h>

void licp_load_default_config(licp_load_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    /* Class 0 emergency: always 1.0. Classes 1..3: high, normal, bulk. */
    const double init[LICP_PRIORITY_CLASSES] = {1.0, 0.8, 0.5, 0.3};
    const double pmin[LICP_PRIORITY_CLASSES] = {1.0, 0.2, 0.05, 0.01};
    for (unsigned i = 0; i < LICP_PRIORITY_CLASSES; ++i) {
        cfg->p_init[i] = init[i];
        cfg->p_min[i] = pmin[i];
        cfg->p_max[i] = 1.0;
    }
    cfg->collision_high = 0.30;
    cfg->collision_low = 0.10;
    cfg->decrease_factor = 0.5;
    cfg->increase_step = 0.1;
    cfg->min_samples = 5u;
}

licp_status_t licp_load_init(licp_load_t *l, const licp_load_config_t *cfg)
{
    if (l == NULL || cfg == NULL || cfg->collision_low > cfg->collision_high || cfg->decrease_factor <= 0.0 ||
        cfg->decrease_factor >= 1.0 || cfg->increase_step <= 0.0) {
        return LICP_ERR_PARAM;
    }
    for (unsigned i = 0; i < LICP_PRIORITY_CLASSES; ++i) {
        if (cfg->p_min[i] < 0.0 || cfg->p_max[i] > 1.0 || cfg->p_min[i] > cfg->p_max[i] ||
            cfg->p_init[i] < cfg->p_min[i] || cfg->p_init[i] > cfg->p_max[i]) {
            return LICP_ERR_PARAM;
        }
    }
    memset(l, 0, sizeof(*l));
    l->cfg = *cfg;
    for (unsigned i = 0; i < LICP_PRIORITY_CLASSES; ++i) {
        l->p[i] = cfg->p_init[i];
    }
    l->p[0] = 1.0; /* emergency is never throttled */
    return LICP_OK;
}

void licp_load_on_decoded(licp_load_t *l, uint8_t priority_class, int shared_slot)
{
    if (l == NULL) {
        return;
    }
    if (shared_slot && priority_class < LICP_PRIORITY_CLASSES) {
        l->cur.decoded_shared[priority_class]++;
    } else {
        l->cur.decoded_home++;
    }
}

void licp_load_on_collision(licp_load_t *l)
{
    if (l != NULL) {
        l->cur.header_only++;
    }
}

void licp_load_end_superframe(licp_load_t *l)
{
    if (l == NULL) {
        return;
    }
    uint32_t shared = 0;
    for (unsigned i = 0; i < LICP_PRIORITY_CLASSES; ++i) {
        shared += l->cur.decoded_shared[i];
    }
    uint32_t events = shared + l->cur.header_only;
    double ratio = (events > 0u) ? (double)l->cur.header_only / (double)events : 0.0;
    l->last_ratio = ratio;

    if (events >= l->cfg.min_samples) {
        if (ratio > l->cfg.collision_high) {
            /* Throttle the lowest-priority class that can still go down. */
            for (unsigned c = LICP_PRIORITY_CLASSES - 1u; c >= 1u; --c) {
                if (l->p[c] > l->cfg.p_min[c]) {
                    double np = l->p[c] * l->cfg.decrease_factor;
                    l->p[c] = (np < l->cfg.p_min[c]) ? l->cfg.p_min[c] : np;
                    break;
                }
            }
        } else if (ratio < l->cfg.collision_low) {
            for (unsigned c = 1u; c < LICP_PRIORITY_CLASSES; ++c) {
                double np = l->p[c] + l->cfg.increase_step;
                l->p[c] = (np > l->cfg.p_max[c]) ? l->cfg.p_max[c] : np;
            }
        }
    }
    l->p[0] = 1.0;
    l->last = l->cur;
    memset(&l->cur, 0, sizeof(l->cur));
}

void licp_load_get_wire(const licp_load_t *l, uint8_t out[LICP_PRIORITY_CLASSES])
{
    for (unsigned i = 0; i < LICP_PRIORITY_CLASSES; ++i) {
        out[i] = licp_prob_to_u8(l->p[i]);
    }
}
