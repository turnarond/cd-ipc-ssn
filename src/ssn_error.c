/**
 * @file ssn_error.c
 * @brief IPC错误处理实现
 */

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include "ssn_error.h"
#include "util/ssn_log.h"

/**
 * @brief 带版本的错误码生成宏
 * @param category 错误类别
 * @param subcategory 错误子类别
 * @param code 具体错误码
 * @return 带版本的错误码
 */
#define SSN_ECODE_MAKE_WITH_VERSION(category, subcategory, code) \
    ((uint32_t)((SSN_ECODE_VERSION << 28) | (category << 24) | (subcategory << 16) | (code)))

/**
 * @brief 错误消息映射表
 */
static struct {
    ssn_ecode_t error;     /**< 错误码 */
    const char* message;   /**< 错误消息 */
} error_messages[] = {
    { SSN_ECODE_SUCCESS,             "Success" },
    { SSN_ECODE_INVALID_ARGS,        "Invalid arguments" },
    { SSN_ECODE_NOT_FOUND,           "Not found" },
    { SSN_ECODE_TIMEOUT,             "Timeout" },
    { SSN_ECODE_INTERNAL,            "Internal error" },
    { SSN_ECODE_NET_CONNECT,         "Network connection failed" },
    { SSN_ECODE_NET_DISCONNECT,      "Network disconnected" },
    { SSN_ECODE_NET_READ,            "Network read failed" },
    { SSN_ECODE_NET_WRITE,           "Network write failed" },
    { SSN_ECODE_SERVICE_NOT_FOUND,   "Service not found" },
    { SSN_ECODE_SERVICE_BUSY,        "Service busy" },
    { SSN_ECODE_SERVICE_ERROR,       "Service error" },
    { SSN_ECODE_OUT_OF_MEMORY,       "Out of memory" },
    { SSN_ECODE_RESOURCE_LIMIT,      "Resource limit exceeded" },
    { SSN_ECODE_AUTH_FAILED,         "Authentication failed" },
    { SSN_ECODE_ACCESS_DENIED,       "Access denied" },
    { SSN_ECODE_SERIALIZE_FAILED,    "Serialization failed" },
    { SSN_ECODE_DESERIALIZE_FAILED,  "Deserialization failed" },
    { 0, NULL } /* 结束标记 */
};

/**
 * @brief 获取错误消息
 * @param error 错误码
 * @return 错误消息字符串
 */
__attribute__((visibility("default"))) const char* ssn_ecode_message(ssn_ecode_t error) {
    for (int i = 0; error_messages[i].message != NULL; i++) {
        if (error_messages[i].error == error) {
            return error_messages[i].message;
        }
    }
    return "Unknown error";
}

/**
 * @brief 获取错误类别
 * @param error 错误码
 * @return 错误类别
 */
uint8_t ssn_ecode_category(ssn_ecode_t error) {
    return (error >> 24) & 0xFF;
}

/**
 * @brief 获取错误子类别
 * @param error 错误码
 * @return 错误子类别
 */
uint8_t ssn_ecode_subcategory(ssn_ecode_t error) {
    return (error >> 16) & 0xFF;
}

/**
 * @brief 获取具体错误码
 * @param error 错误码
 * @return 具体错误码
 */
uint16_t ssn_ecode_code(ssn_ecode_t error) {
    return error & 0xFFFF;
}

/**
 * @brief 处理错误并记录日志
 * 
 * @param error 错误码
 * @param file 文件路径
 * @param line 行号
 * @param func 函数名
 * @param format 日志格式
 * @param ... 可变参数
 */
void ssn_handle_error(ssn_ecode_t error, const char *file, int line, const char *func, const char *format, ...) {
    const char *error_msg = ssn_ecode_message(error);
    char buf[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    LOG_ERROR("[%s:%d] %s: %s - %s", file, line, func, error_msg, buf);
}