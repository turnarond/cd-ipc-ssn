/**
 * @file cd_ipc_error.h
 * @brief IPC错误码定义和错误处理函数
 */

#ifndef CD_IPC_ERROR_H
#define CD_IPC_ERROR_H

#include <stdint.h>

/**
 * @brief 错误码版本
 */
#define IPC_ERR_VERSION 0x01

/**
 * @brief 错误码类别
 */
#define IPC_ERR_CATEGORY_COMMON     0x00  /**< 通用错误 */
#define IPC_ERR_CATEGORY_NETWORK    0x01  /**< 网络错误 */
#define IPC_ERR_CATEGORY_SERVICE    0x02  /**< 服务错误 */
#define IPC_ERR_CATEGORY_RESOURCE   0x03  /**< 资源错误 */
#define IPC_ERR_CATEGORY_SECURITY   0x04  /**< 安全错误 */
#define IPC_ERR_CATEGORY_SERIALIZE  0x05  /**< 序列化错误 */

/**
 * @brief 错误码生成宏
 * @param category 错误类别
 * @param subcategory 错误子类别
 * @param code 具体错误码
 * @return 组合后的错误码
 */
#define IPC_ERR_MAKE(category, subcategory, code) \
    ((uint32_t)((category << 24) | (subcategory << 16) | (code)))

/**
 * @brief 通用错误
 */
#define IPC_ERR_SUCCESS             IPC_ERR_MAKE(IPC_ERR_CATEGORY_COMMON, 0x00, 0x0000)  /**< 成功 */
#define IPC_ERR_INVALID_ARGS        IPC_ERR_MAKE(IPC_ERR_CATEGORY_COMMON, 0x00, 0x0001)  /**< 无效参数 */
#define IPC_ERR_NOT_FOUND           IPC_ERR_MAKE(IPC_ERR_CATEGORY_COMMON, 0x00, 0x0002)  /**< 未找到 */
#define IPC_ERR_TIMEOUT             IPC_ERR_MAKE(IPC_ERR_CATEGORY_COMMON, 0x00, 0x0003)  /**< 超时 */
#define IPC_ERR_INTERNAL            IPC_ERR_MAKE(IPC_ERR_CATEGORY_COMMON, 0x00, 0x0004)  /**< 内部错误 */

/**
 * @brief 网络错误
 */
#define IPC_ERR_NET_CONNECT         IPC_ERR_MAKE(IPC_ERR_CATEGORY_NETWORK, 0x00, 0x0001)  /**< 连接失败 */
#define IPC_ERR_NET_DISCONNECT      IPC_ERR_MAKE(IPC_ERR_CATEGORY_NETWORK, 0x00, 0x0002)  /**< 连接断开 */
#define IPC_ERR_NET_READ            IPC_ERR_MAKE(IPC_ERR_CATEGORY_NETWORK, 0x00, 0x0003)  /**< 读取失败 */
#define IPC_ERR_NET_WRITE           IPC_ERR_MAKE(IPC_ERR_CATEGORY_NETWORK, 0x00, 0x0004)  /**< 写入失败 */

/**
 * @brief 服务错误
 */
#define IPC_ERR_SERVICE_NOT_FOUND   IPC_ERR_MAKE(IPC_ERR_CATEGORY_SERVICE, 0x00, 0x0001)  /**< 服务未找到 */
#define IPC_ERR_SERVICE_BUSY        IPC_ERR_MAKE(IPC_ERR_CATEGORY_SERVICE, 0x00, 0x0002)  /**< 服务繁忙 */
#define IPC_ERR_SERVICE_ERROR       IPC_ERR_MAKE(IPC_ERR_CATEGORY_SERVICE, 0x00, 0x0003)  /**< 服务错误 */

/**
 * @brief 资源错误
 */
#define IPC_ERR_OUT_OF_MEMORY       IPC_ERR_MAKE(IPC_ERR_CATEGORY_RESOURCE, 0x00, 0x0001)  /**< 内存不足 */
#define IPC_ERR_RESOURCE_LIMIT      IPC_ERR_MAKE(IPC_ERR_CATEGORY_RESOURCE, 0x00, 0x0002)  /**< 资源限制 */

/**
 * @brief 安全错误
 */
#define IPC_ERR_AUTH_FAILED         IPC_ERR_MAKE(IPC_ERR_CATEGORY_SECURITY, 0x00, 0x0001)  /**< 认证失败 */
#define IPC_ERR_ACCESS_DENIED       IPC_ERR_MAKE(IPC_ERR_CATEGORY_SECURITY, 0x00, 0x0002)  /**< 访问拒绝 */

/**
 * @brief 序列化错误
 */
#define IPC_ERR_SERIALIZE_FAILED    IPC_ERR_MAKE(IPC_ERR_CATEGORY_SERIALIZE, 0x00, 0x0001)  /**< 序列化失败 */
#define IPC_ERR_DESERIALIZE_FAILED  IPC_ERR_MAKE(IPC_ERR_CATEGORY_SERIALIZE, 0x00, 0x0002)  /**< 反序列化失败 */

/**
 * @brief 错误码类型
 */
typedef uint32_t ipc_error_t;

/**
 * @brief 获取错误消息
 * @param error 错误码
 * @return 错误消息字符串
 */
const char* ipc_error_message(ipc_error_t error);

/**
 * @brief 获取错误类别
 * @param error 错误码
 * @return 错误类别
 */
uint8_t ipc_error_category(ipc_error_t error);

/**
 * @brief 获取错误子类别
 * @param error 错误码
 * @return 错误子类别
 */
uint8_t ipc_error_subcategory(ipc_error_t error);

/**
 * @brief 获取具体错误码
 * @param error 错误码
 * @return 具体错误码
 */
uint16_t ipc_error_code(ipc_error_t error);

/**
 * @brief 处理错误并记录日志
 * @param error 错误码
 * @param file 文件名
 * @param line 行号
 * @param func 函数名
 * @param format 日志格式
 * @param ... 可变参数
 */
void ipc_handle_error(ipc_error_t error, const char *file, int line, const char *func, const char *format, ...);

#endif /* CD_IPC_ERROR_H */