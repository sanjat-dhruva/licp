/**
 * @file licp_radio_fhss.h
 * @brief Receive-only back-end for the frequency-hopping receiver in
 *        programmable logic, read from a Linux character device.
 *
 * The receiver driver is expected to deliver fixed-size records
 * (::LICP_FHSS_RECORD_LEN bytes, little-endian):
 * @verbatim
 *  magic(2)=0x4648 | flags(1) | datarate(1) | len(1) | reserved(1) | freq_hz(4)
 *  | rssi_x10(2) | snr_x10(2) | gnss_ms(8) | payload(255, first len bytes valid)
 * @endverbatim
 * flags bit 0 = header decoded, payload failed (collision signal).
 *
 * @note This record layout is a proposal to agree with the programmable-logic
 *       team; change it here and in ::licp_fhss_decode_record only.
 *
 * @ingroup hal
 */
#ifndef LICP_HAL_RADIO_FHSS_H
#define LICP_HAL_RADIO_FHSS_H

#include <stdint.h>

#include "licp/hal/licp_radio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup hal
 *  @{ */

#define LICP_FHSS_RECORD_LEN 277u   /**< Fixed record size. */
#define LICP_FHSS_MAGIC      0x4648u /**< "FH". */

/** @brief Back-end state. */
typedef struct {
    char               path[256]; /**< Character device path. */
    int                fd;        /**< Open descriptor, -1 when stopped. */
    uint8_t            partial[LICP_FHSS_RECORD_LEN]; /**< Partially read record. */
    size_t             have;      /**< Bytes in @ref partial. */
    licp_radio_stats_t st;        /**< Statistics. */
} licp_radio_fhss_t;

/** @brief Operations table. Transmit always returns ::LICP_ERR_UNSUPPORTED. */
extern const licp_radio_ops_t licp_radio_fhss_ops;

/** @brief Prepare @p f for device @p path and bind it into @p radio. */
licp_status_t licp_radio_fhss_init(licp_radio_fhss_t *f, const char *path, licp_radio_t *radio);

/**
 * @brief Decode one record (exposed for tests and for the driver team).
 * @return ::LICP_OK, ::LICP_ERR_FORMAT on bad magic or length.
 */
licp_status_t licp_fhss_decode_record(const uint8_t rec[LICP_FHSS_RECORD_LEN], licp_rx_frame_t *out);

/** @brief Encode one record (used by tests and the bench simulator). */
void licp_fhss_encode_record(const licp_rx_frame_t *in, uint8_t rec[LICP_FHSS_RECORD_LEN]);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_HAL_RADIO_FHSS_H */
