/**
 * @file licp_status.c
 * @brief Implementation of ::licp_status_str.
 * @ingroup common
 */
#include "licp/common/licp_status.h"

const char *licp_status_str(licp_status_t status)
{
    switch (status) {
    case LICP_OK:              return "LICP_OK";
    case LICP_ERR_PARAM:       return "LICP_ERR_PARAM";
    case LICP_ERR_LENGTH:      return "LICP_ERR_LENGTH";
    case LICP_ERR_FORMAT:      return "LICP_ERR_FORMAT";
    case LICP_ERR_CRC:         return "LICP_ERR_CRC";
    case LICP_ERR_MIC:         return "LICP_ERR_MIC";
    case LICP_ERR_NOT_FOUND:   return "LICP_ERR_NOT_FOUND";
    case LICP_ERR_FULL:        return "LICP_ERR_FULL";
    case LICP_ERR_EXISTS:      return "LICP_ERR_EXISTS";
    case LICP_ERR_IO:          return "LICP_ERR_IO";
    case LICP_ERR_STATE:       return "LICP_ERR_STATE";
    case LICP_ERR_UNSUPPORTED: return "LICP_ERR_UNSUPPORTED";
    case LICP_ERR_TIMEOUT:     return "LICP_ERR_TIMEOUT";
    case LICP_ERR_NO_MEMORY:   return "LICP_ERR_NO_MEMORY";
    default:                   return "LICP_ERR_UNKNOWN";
    }
}
