#ifndef LOG_H_
#define LOG_H_

#include <stdio.h>

#define STRINGIFY(x) STRINGIFY2(x)
#define STRINGIFY2(x) #x

// 编译器支持 __attribute__((format)) 时启用格式检查
#if defined(__GNUC__) || defined(__clang__)
#  define LOG_PRINTF_ATTR(fmt_idx, var_idx) \
     __attribute__((__format__(__printf__, fmt_idx, var_idx)))
#else
#  define LOG_PRINTF_ATTR(fmt_idx, var_idx)
#endif

// 日志级别
#define LOG_LEVEL_DEBUG    0
#define LOG_LEVEL_INFO     1
#define LOG_LEVEL_WARN     2
#define LOG_LEVEL_ERROR    3
#define LOG_LEVEL_NONE     4

// 全局日志级别（默认 ERROR）
#ifndef IPC_LOG_LEVEL
#   ifdef NDEBUG
#       define IPC_LOG_LEVEL LOG_LEVEL_NONE
#   else
#       define IPC_LOG_LEVEL LOG_LEVEL_ERROR
#   endif
#endif

// 核心日志函数
void log_write(int level,
                      const char* file,
                      int line,
                      const char* fmt, ...)
    LOG_PRINTF_ATTR(4, 5);

// 方便使用的宏：自动传入 __FILE__ 和 __LINE__
# if (IPC_LOG_LEVEL <= LOG_LEVEL_DEBUG)
#define LOG_DEBUG(fmt, ...) \
    log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#   define LOG_DEBUG(fmt, ...) ((void)0)
#endif

# if IPC_LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...) \
    log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#   define LOG_INFO(fmt, ...) ((void)0)
#endif

# if IPC_LOG_LEVEL <= LOG_LEVEL_WARN
#define LOG_WARN(fmt, ...) \
    log_write(LOG_LEVEL_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#   define LOG_WARN(fmt, ...) ((void)0)
#endif

# if IPC_LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERROR(fmt, ...) \
    log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#   define LOG_ERROR(fmt, ...) ((void)0)
#endif

#endif // LOG_H_