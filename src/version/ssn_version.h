/*
 * ssn_version.h - Version information for cd-ipc-ssn
 *
 * This file contains version information for the library.
 */

#ifndef SSN_VERSION_H
#define SSN_VERSION_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup SSN_Version Version Information
 * @{
 */

/**
 * @brief Major version number
 */
#define SSN_VERSION_MAJOR 1

/**
 * @brief Minor version number
 */
#define SSN_VERSION_MINOR 0

/**
 * @brief Patch version number
 */
#define SSN_VERSION_PATCH 0

/**
 * @brief Version string
 */
#define SSN_VERSION_STRING "1.0.0"

/**
 * @brief Version number (encoded as MNNPP)
 * 
 * M = major, NN = minor, PP = patch
 */
#define SSN_VERSION ((SSN_VERSION_MAJOR << 16) | (SSN_VERSION_MINOR << 8) | SSN_VERSION_PATCH)

/**
 * @brief Get version string
 * 
 * @return Version string
 */
const char *ssn_version_get_string(void);

/**
 * @brief Get major version number
 * 
 * @return Major version
 */
int ssn_version_get_major(void);

/**
 * @brief Get minor version number
 * 
 * @return Minor version
 */
int ssn_version_get_minor(void);

/**
 * @brief Get patch version number
 * 
 * @return Patch version
 */
int ssn_version_get_patch(void);

/**
 * @brief Check if version is compatible
 * 
 * @param major Major version to check
 * @param minor Minor version to check
 * @return true if compatible, false otherwise
 */
bool ssn_version_is_compatible(int major, int minor);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* SSN_VERSION_H */
