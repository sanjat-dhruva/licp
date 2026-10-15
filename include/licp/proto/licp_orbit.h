/**
 * @file licp_orbit.h
 * @brief The 26-byte compressed orbit record carried in the beacon and in
 *        the ground upload plan.
 *
 * Field layout (little-endian), from section 5.1 of the network note:
 * | Field                              | Bytes | Resolution                  |
 * |------------------------------------|-------|-----------------------------|
 * | Satellite identifier               | 1     | —                           |
 * | Epoch                              | 4     | seconds since project date  |
 * | Mean motion                        | 4     | 1e-8 revolutions per day    |
 * | Eccentricity                       | 3     | 1e-7                        |
 * | Inclination                        | 2     | 0.01 degree                 |
 * | Right ascension of ascending node  | 3     | 0.0001 degree               |
 * | Argument of perigee                | 3     | 0.0001 degree               |
 * | Mean anomaly                       | 3     | 0.0001 degree               |
 * | Drag term (B-star)                 | 2     | 12-bit mantissa, 4-bit exponent |
 * | Validity                           | 1     | days                        |
 *
 * Drag term encoding: value = mantissa x 10^(exponent - 3), mantissa a signed
 * 12-bit integer (-2048..2047), exponent a signed 4-bit integer (-8..7).
 * This keeps three significant digits; see the deviations in docs/testing.md.
 *
 * @ingroup proto
 */
#ifndef LICP_PROTO_ORBIT_H
#define LICP_PROTO_ORBIT_H

#include <stddef.h>
#include <stdint.h>

#include "licp/common/licp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup proto Payload message formats
 * @brief Orbit record, beacon, broadcast and acknowledgement bitmap formats.
 *
 * These formats are shared with the device firmware: keep them in one place
 * and change the version byte whenever a layout changes.
 * @{
 */

#define LICP_ORBIT_RECORD_LEN 26u /**< Packed size in bytes. */

/** @brief Orbit record with every field as a scaled integer. */
typedef struct {
    uint8_t  sat_id;          /**< Satellite identifier. */
    uint32_t epoch_s;         /**< Seconds since the project reference date. */
    uint32_t mean_motion_e8;  /**< Mean motion in 1e-8 revolutions per day. */
    uint32_t ecc_e7;          /**< Eccentricity in 1e-7 (0..9 999 999). */
    uint16_t incl_e2;         /**< Inclination in 0.01 degree (0..18 000). */
    uint32_t raan_e4;         /**< Right ascension of ascending node in 1e-4 degree (0..3 599 999). */
    uint32_t argp_e4;         /**< Argument of perigee in 1e-4 degree. */
    uint32_t mean_anom_e4;    /**< Mean anomaly in 1e-4 degree. */
    int16_t  bstar_mant;      /**< Drag term mantissa (-2048..2047). */
    int8_t   bstar_exp;       /**< Drag term exponent (-8..7). */
    uint8_t  validity_days;   /**< Days the record may be used. */
} licp_orbit_t;

/** @brief Check every field against its range. */
licp_status_t licp_orbit_validate(const licp_orbit_t *o);

/**
 * @brief Pack into 26 bytes.
 * @return ::LICP_OK, ::LICP_ERR_PARAM if a field is out of range.
 */
licp_status_t licp_orbit_pack(const licp_orbit_t *o, uint8_t out[LICP_ORBIT_RECORD_LEN]);

/**
 * @brief Unpack 26 bytes.
 * @return ::LICP_OK, ::LICP_ERR_FORMAT if a decoded field is out of range.
 */
licp_status_t licp_orbit_unpack(const uint8_t in[LICP_ORBIT_RECORD_LEN], licp_orbit_t *o);

/**
 * @brief Encode a drag term value into mantissa and exponent.
 * @param value Drag term (inverse earth radii), e.g. 3.4567e-5.
 * @param[out] mant Mantissa.
 * @param[out] exp  Exponent.
 * @return ::LICP_OK or ::LICP_ERR_PARAM if outside the representable range.
 */
licp_status_t licp_orbit_bstar_encode(double value, int16_t *mant, int8_t *exp);

/** @brief Decode mantissa and exponent back to a value. */
double licp_orbit_bstar_decode(int16_t mant, int8_t exp);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_PROTO_ORBIT_H */
