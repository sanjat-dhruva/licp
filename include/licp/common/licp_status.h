/**
 * @file licp_status.h
 * @brief Status codes returned by every LICP module.
 *
 * Every public function that can fail returns ::licp_status_t. Zero means
 * success, negative values are errors, so callers can write
 * `if (licp_xxx(...) != LICP_OK)`.
 *
 * @ingroup common
 */
#ifndef LICP_COMMON_STATUS_H
#define LICP_COMMON_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup common Common utilities
 * @brief Status codes, logging, byte packing and checksums used by all modules.
 * @{
 */

/** @brief Result of an LICP operation. */
typedef enum {
    LICP_OK              =   0, /**< Success. */
    LICP_ERR_PARAM       =  -1, /**< Invalid argument (NULL pointer, out-of-range value). */
    LICP_ERR_LENGTH      =  -2, /**< Buffer too short or message length inconsistent. */
    LICP_ERR_FORMAT      =  -3, /**< Malformed message or unsupported version. */
    LICP_ERR_CRC         =  -4, /**< Checksum mismatch. */
    LICP_ERR_MIC         =  -5, /**< Message integrity code mismatch. */
    LICP_ERR_NOT_FOUND   =  -6, /**< Requested item does not exist. */
    LICP_ERR_FULL        =  -7, /**< Container is full. */
    LICP_ERR_EXISTS      =  -8, /**< Item already exists. */
    LICP_ERR_IO          =  -9, /**< File or device input/output failure. */
    LICP_ERR_STATE       = -10, /**< Operation not allowed in the current state. */
    LICP_ERR_UNSUPPORTED = -11, /**< Feature not built in or not supported. */
    LICP_ERR_TIMEOUT     = -12, /**< Operation timed out. */
    LICP_ERR_NO_MEMORY   = -13  /**< Memory allocation failed. */
} licp_status_t;

/**
 * @brief Return a constant, human-readable name for a status code.
 * @param status Status code.
 * @return Never NULL; "LICP_ERR_UNKNOWN" for values outside the enum.
 */
const char *licp_status_str(licp_status_t status);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* LICP_COMMON_STATUS_H */
