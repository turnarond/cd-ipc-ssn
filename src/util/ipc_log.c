#include "ipc_log.h"
#include <time.h>
#include <stdarg.h>
#include <string.h>

static const char* basename(const char* path) {
    const char* p = strrchr(path, '/');
    if (p == NULL) {
        p = strrchr(path, '\\');
    }
    return p ? p + 1 : path;
}

static const char* level_to_str(int level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO ";
        case LOG_LEVEL_WARN:  return "WARN ";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "?????";
    }
}
void log_write(int level,
                      const char* file,
                      int line,
                      const char* fmt, ...) 
{
    // 获取当前时间
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    // 打印前缀到 stderr
    fprintf(stderr, "[%s] [%s] %s:%d: ",
            time_buf,
            level_to_str(level),
            basename(file),
            line);

    // 打印用户格式内容
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    // 换行并刷新
    fputc('\n', stderr);
    fflush(stderr);
}