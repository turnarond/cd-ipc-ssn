/**
 * @file ipc_error.c
 * @brief IPC错误处理实现
 */

#include <stddef.h>
#include "cd_ipc_error.h"
#include "util/ssn_log.h"

/**
 * @brief 带版本的错误码生成宏
 * @param category 错误类别
 * @param subcategory 错误子类别
 * @param code 具体错误码
 * @return 带版本的错误码
 */
#define IPC_ERR_MAKE_WITH_VERSION(category, subcategory, code) \
    ((uint32_t)((IPC_ERR_VERSION << 28) | (category << 24) | (subcategory << 16) | (code)))

/**
 * @brief 获取错误码版本
 * @param error 错误码
 * @return 错误码版本
 */
uint8_t ipc_error_version(ipc_error_t error) {
    return (error >> 28) & 0x0F;
}

/**
 * @brief 错误消息映射表
 */
static struct {
    ipc_error_t error;     /**< 错误码 */
    const char* message;   /**< 错误消息 */
} error_messages[] = {
    { IPC_ERR_SUCCESS,             "Success" },
    { IPC_ERR_INVALID_ARGS,        "Invalid arguments" },
    { IPC_ERR_NOT_FOUND,           "Not found" },
    { IPC_ERR_TIMEOUT,             "Timeout" },
    { IPC_ERR_INTERNAL,            "Internal error" },
    { IPC_ERR_NET_CONNECT,         "Network connection failed" },
    { IPC_ERR_NET_DISCONNECT,      "Network disconnected" },
    { IPC_ERR_NET_READ,            "Network read failed" },
    { IPC_ERR_NET_WRITE,           "Network write failed" },
    { IPC_ERR_SERVICE_NOT_FOUND,   "Service not found" },
    { IPC_ERR_SERVICE_BUSY,        "Service busy" },
    { IPC_ERR_SERVICE_ERROR,       "Service error" },
    { IPC_ERR_OUT_OF_MEMORY,       "Out of memory" },
    { IPC_ERR_RESOURCE_LIMIT,      "Resource limit exceeded" },
    { IPC_ERR_AUTH_FAILED,         "Authentication failed" },
    { IPC_ERR_ACCESS_DENIED,       "Access denied" },
    { IPC_ERR_SERIALIZE_FAILED,    "Serialization failed" },
    { IPC_ERR_DESERIALIZE_FAILED,  "Deserialization failed" },
    { 0, NULL } /* 结束标记 */
};

/**
 * @brief 获取错误消息
 * @param error 错误码
 * @return 错误消息字符串
 */
const char* ipc_error_message(ipc_error_t error) {
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
uint8_t ipc_error_category(ipc_error_t error) {
    return (error >> 24) & 0xFF;
}

/**
 * @brief 获取错误子类别
 * @param error 错误码
 * @return 错误子类别
 */
uint8_t ipc_error_subcategory(ipc_error_t error) {
    return (error >> 16) & 0xFF;
}

/**
 * @brief 获取具体错误码
 * @param error 错误码
 * @return 具体错误码
 */
uint16_t ipc_error_code(ipc_error_t error) {
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
void ipc_handle_error(ipc_error_t error, const char *file, int line, const char *func, const char *format, ...) {
    const char *error_msg = ipc_error_message(error);
    LOG_ERROR("[%s:%d] %s: %s - %s", file, line, func, error_msg, format);
}