/**
 * @file licp_orbit.c
 * @brief Orbit record packing.
 * @ingroup proto
 */
#include "licp/proto/licp_orbit.h"

#include <math.h>

#include "licp/common/licp_bytes.h"

/** Exclusive upper bound of angles in 1e-4 degree (360 degrees). */
#define ANGLE_E4_MAX 3600000u /* exclusive */

licp_status_t licp_orbit_validate(const licp_orbit_t *o)
{
    if (o == NULL) {
        return LICP_ERR_PARAM;
    }
    if (o->ecc_e7 >= 10000000u || o->incl_e2 > 18000u || o->raan_e4 >= ANGLE_E4_MAX ||
        o->argp_e4 >= ANGLE_E4_MAX || o->mean_anom_e4 >= ANGLE_E4_MAX || o->bstar_mant < -2048 ||
        o->bstar_mant > 2047 || o->bstar_exp < -8 || o->bstar_exp > 7 || o->mean_motion_e8 == 0u) {
        return LICP_ERR_PARAM;
    }
    return LICP_OK;
}

/** @copydoc licp_orbit_pack */
licp_status_t licp_orbit_pack(const licp_orbit_t *o, uint8_t out[LICP_ORBIT_RECORD_LEN])
{
    if (out == NULL || licp_orbit_validate(o) != LICP_OK) {
        return LICP_ERR_PARAM;
    }
    out[0] = o->sat_id;
    licp_put_u32(&out[1], o->epoch_s);
    licp_put_u32(&out[5], o->mean_motion_e8);
    licp_put_u24(&out[9], o->ecc_e7);
    licp_put_u16(&out[12], o->incl_e2);
    licp_put_u24(&out[14], o->raan_e4);
    licp_put_u24(&out[17], o->argp_e4);
    licp_put_u24(&out[20], o->mean_anom_e4);
    uint16_t bstar = (uint16_t)(((uint16_t)o->bstar_mant & 0x0FFFu) | (((uint16_t)o->bstar_exp & 0x0Fu) << 12));
    licp_put_u16(&out[23], bstar);
    out[25] = o->validity_days;
    return LICP_OK;
}

/** @copydoc licp_orbit_unpack */
licp_status_t licp_orbit_unpack(const uint8_t in[LICP_ORBIT_RECORD_LEN], licp_orbit_t *o)
{
    if (in == NULL || o == NULL) {
        return LICP_ERR_PARAM;
    }
    o->sat_id = in[0];
    o->epoch_s = licp_get_u32(&in[1]);
    o->mean_motion_e8 = licp_get_u32(&in[5]);
    o->ecc_e7 = licp_get_u24(&in[9]);
    o->incl_e2 = licp_get_u16(&in[12]);
    o->raan_e4 = licp_get_u24(&in[14]);
    o->argp_e4 = licp_get_u24(&in[17]);
    o->mean_anom_e4 = licp_get_u24(&in[20]);
    uint16_t bstar = licp_get_u16(&in[23]);
    int mant = (int)(bstar & 0x0FFFu);
    if (mant >= 2048) {
        mant -= 4096; /* sign-extend 12 bits */
    }
    int exp = (int)((bstar >> 12) & 0x0Fu);
    if (exp >= 8) {
        exp -= 16; /* sign-extend 4 bits */
    }
    o->bstar_mant = (int16_t)mant;
    o->bstar_exp = (int8_t)exp;
    o->validity_days = in[25];
    return (licp_orbit_validate(o) == LICP_OK) ? LICP_OK : LICP_ERR_FORMAT;
}

licp_status_t licp_orbit_bstar_encode(double value, int16_t *mant, int8_t *exp)
{
    if (mant == NULL || exp == NULL || isnan(value) || isinf(value)) {
        return LICP_ERR_PARAM;
    }
    if (value == 0.0) {
        *mant = 0;
        *exp = 0;
        return LICP_OK;
    }
    /* Choose the exponent that gives a mantissa of three significant digits (100..999). */
    double mag = fabs(value);
    int e = (int)floor(log10(mag)) + 1; /* value = 0.ddd x 10^e  =>  mantissa ddd, exponent e */
    if (e < -8 || e > 7) {
        return LICP_ERR_PARAM;
    }
    double m = value / pow(10.0, (double)(e - 3));
    long rm = lround(m);
    if (rm > 2047 || rm < -2048) {
        return LICP_ERR_PARAM;
    }
    *mant = (int16_t)rm;
    *exp = (int8_t)e;
    return LICP_OK;
}

double licp_orbit_bstar_decode(int16_t mant, int8_t exp)
{
    return (double)mant * pow(10.0, (double)(exp - 3));
}
