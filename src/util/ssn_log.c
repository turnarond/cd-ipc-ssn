/*
 * SSN Logging Implementation
 */

#include "ssn_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static ssn_log_level_t g_log_level = SSN_LOG_LEVEL_INFO;
static FILE* g_log_file = NULL;

const char* g_log_level_strings[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};

void ssn_log_write(ssn_log_level_t level,
                   const char* file,
                   int line,
                   const char* func,
                   const char* fmt, ...)
{
    if (level < g_log_level) {
        return;
    }

    FILE* output = g_log_file ? g_log_file : stderr;

    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(output, "[%s] [%s] [%s:%d] %s(): ",
            time_str, g_log_level_strings[level], file, line, func);

    va_list args;
    va_start(args, fmt);
    vfprintf(output, fmt, args);
    va_end(args);

    fprintf(output, "\n");
    fflush(output);
}

void ssn_log_set_level(ssn_log_level_t level)
{
    g_log_level = level;
}

ssn_log_level_t ssn_log_get_level(void)
{
    return g_log_level;
}

void ssn_log_set_file(FILE* file)
{
    g_log_file = file;
}

FILE* ssn_log_get_file(void)
{
    return g_log_file ? g_log_file : stderr;
}

