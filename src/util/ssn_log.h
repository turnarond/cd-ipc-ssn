/*
 * SSN Logging Interface
 */

#ifndef SSN_LOG_H
#define SSN_LOG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>
#include <stdio.h>

#include "../ssn_export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SSN_LOG_LEVEL_DEBUG = 0,
    SSN_LOG_LEVEL_INFO,
    SSN_LOG_LEVEL_WARN,
    SSN_LOG_LEVEL_ERROR,
    SSN_LOG_LEVEL_FATAL
} ssn_log_level_t;

#define LOG_DEBUG(fmt, ...) \
    ssn_log_write(SSN_LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#ifndef LOG_INFO
#define LOG_INFO(fmt, ...) \
    ssn_log_write(SSN_LOG_LEVEL_INFO, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_WARN
#define LOG_WARN(fmt, ...) \
    ssn_log_write(SSN_LOG_LEVEL_WARN, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(fmt, ...) \
    ssn_log_write(SSN_LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif
#define LOG_FATAL(fmt, ...) \
    ssn_log_write(SSN_LOG_LEVEL_FATAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

SSN_API void ssn_log_write(ssn_log_level_t level,
                   const char* file,
                   int line,
                   const char* func,
                   const char* fmt, ...);

SSN_API void ssn_log_set_level(ssn_log_level_t level);
SSN_API ssn_log_level_t ssn_log_get_level(void);
SSN_API void ssn_log_set_file(FILE* file);
SSN_API FILE* ssn_log_get_file(void);

#ifdef __cplusplus
}
#endif

#endif

