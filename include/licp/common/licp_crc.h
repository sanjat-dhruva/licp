/**
 * @file licp_crc.h
 * @brief Checksums used by storage records and link messages.
 * @ingroup common
 */
#ifndef LICP_COMMON_CRC_H
#define LICP_COMMON_CRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup common
 *  @{ */

/**
 * @brief CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320).
 *
 * Check value: CRC-32 of the ASCII string "123456789" is 0xCBF43926.
 *
 * @param crc  Previous value; pass 0 to start a new checksum.
 * @param data Bytes to add (may be NULL when @p len is 0).
 * @param len  Number of bytes.
 * @return Updated CRC-32.
 */
uint32_t licp_crc32(uint32_t crc, const void *data, size_t len);

/**
 * @brief CRC-16/CCITT-FALSE (polynomial 0x1021, initial value 0xFFFF).
 *
 * Check value: CRC-16 of "123456789" is 0x29B1.
 *
 * @param data Bytes to checksum.
 * @param len  Number of bytes.
 * @return CRC-16.
 */
uint16_t licp_crc16_ccitt(const void *data, size_t len);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_COMMON_CRC_H */
