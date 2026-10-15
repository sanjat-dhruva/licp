/**
 * @file licp_load.h
 * @brief Load measurement and access-probability control for shared slots.
 *
 * Per superframe the payload counts frames decoded in shared slots and
 * "header decoded, payload failed" events (a direct collision signal). At the
 * end of each superframe the collision ratio drives the access probability
 * sent in the next beacon:
 *  - ratio above `collision_high`: multiply the probability of the
 *    lowest-priority class still above its minimum by `decrease_factor`
 *    (bulk backlog is throttled first);
 *  - ratio below `collision_low`: add `increase_step` to every class, up to
 *    its maximum;
 *  - class 0 (emergency) is never throttled.
 *
 * @ingroup core
 */
#ifndef LICP_CORE_LOAD_H
#define LICP_CORE_LOAD_H

#include <stdint.h>

#include "licp/common/licp_status.h"
#include "licp/proto/licp_beacon.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup core
 *  @{ */

/** @brief Controller settings. */
typedef struct {
    double   p_init[LICP_PRIORITY_CLASSES]; /**< Starting probability per class. */
    double   p_min[LICP_PRIORITY_CLASSES];  /**< Lower bound per class. */
    double   p_max[LICP_PRIORITY_CLASSES];  /**< Upper bound per class. */
    double   collision_high;                /**< Throttle above this ratio (e.g. 0.30). */
    double   collision_low;                 /**< Relax below this ratio (e.g. 0.10). */
    double   decrease_factor;               /**< Multiplier when throttling (e.g. 0.5). */
    double   increase_step;                 /**< Additive step when relaxing (e.g. 0.1). */
    uint32_t min_samples;                   /**< Fewer events than this: no change. */
} licp_load_config_t;

/** @brief Counters of one superframe (exposed for telemetry). */
typedef struct {
    uint32_t decoded_shared[LICP_PRIORITY_CLASSES]; /**< Frames decoded in shared slots, per class. */
    uint32_t decoded_home;                          /**< Frames decoded in home slots. */
    uint32_t header_only;                           /**< Header decoded, payload failed. */
} licp_load_counters_t;

/** @brief Controller state. */
typedef struct {
    licp_load_config_t   cfg;                      /**< Settings. */
    double               p[LICP_PRIORITY_CLASSES]; /**< Current probabilities. */
    licp_load_counters_t cur;                      /**< Current superframe counters. */
    licp_load_counters_t last;                     /**< Counters of the superframe just closed. */
    double               last_ratio;               /**< Collision ratio of the superframe just closed. */
} licp_load_t;

/** @brief Default settings (see file description). */
void licp_load_default_config(licp_load_config_t *cfg);

/** @brief Initialise the controller. @return ::LICP_ERR_PARAM if bounds are inconsistent. */
licp_status_t licp_load_init(licp_load_t *l, const licp_load_config_t *cfg);

/** @brief Count a decoded frame. @p shared_slot false for home slots and emergency channels. */
void licp_load_on_decoded(licp_load_t *l, uint8_t priority_class, int shared_slot);

/** @brief Count a "header decoded, payload failed" event. */
void licp_load_on_collision(licp_load_t *l);

/** @brief Close the superframe: update probabilities and reset counters. */
void licp_load_end_superframe(licp_load_t *l);

/** @brief Current probabilities in wire format (0..255). */
void licp_load_get_wire(const licp_load_t *l, uint8_t out[LICP_PRIORITY_CLASSES]);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_CORE_LOAD_H */
