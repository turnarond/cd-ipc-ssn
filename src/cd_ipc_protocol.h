/**
 * @file ipc_protocol.h
 * @brief IPC协议定义和相关函数
 */

#ifndef CD_IPC_PROTOCOL_H
#define CD_IPC_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef _WIN32
#include <winsock.h>
#else
#include <arpa/inet.h>
#endif
#include "transports/ssn_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup IPC_Protocol IPC协议定义
 * @{
 */

/**
 * @name 常量定义
 * @{
 */

/**
 * @brief IPC头部大小
 */
#define IPC_HEADER_SIZE     sizeof(ipc_header_t)

/**
 * @brief 最大数据包大小
 */
#define IPC_MAX_PACKET_SIZE (131072U)          /**< 128 KiB, 32-bit aligned */

/**
 * @brief 最大负载大小
 */
#define IPC_MAX_PAYLOAD_SIZE  (IPC_MAX_PACKET_SIZE - IPC_HEADER_SIZE)

/**
 * @brief 魔数字节
 */
#define IPC_MAGIC_BYTE          (0x09U)

/**
 * @brief 协议版本号
 */
#define IPC_PROTOCOL_VERSION    (0x02U)

/**
 * @name 消息类型
 * @{
 */

/**
 * @brief 服务信息消息
 */
#define IPC_MSG_TYPE_SERVICE_INFO     (0x00U)

/**
 * @brief RPC请求消息
 */
#define IPC_MSG_TYPE_RPC_REQUEST      (0x01U)

/**
 * @brief 订阅消息
 */
#define IPC_MSG_TYPE_SUBSCRIBE        (0x02U)

/**
 * @brief 取消订阅消息
 */
#define IPC_MSG_TYPE_UNSUBSCRIBE      (0x03U)

/**
 * @brief 发布消息
 */
#define IPC_MSG_TYPE_PUBLISH          (0x04U)

/**
 * @brief 普通消息
 */
#define IPC_MSG_TYPE_MESSAGE         (0x05U)

/**
 * @brief 心跳消息
 */
#define IPC_MSG_TYPE_PING_ECHO        (0xFFU)

/** @} */

/** @} */

/**
 * @defgroup IPC_Structures IPC数据结构
 * @{
 */

/**
 * @brief IPC数据包头部（压缩，多字节字段使用网络字节序）
 */
typedef struct __attribute__((packed)) {
    uint8_t magic;          /**< 魔数 */
    uint8_t version;        /**< 协议版本号 */
    uint8_t msg_type;       /**< 消息类型 */
    uint32_t status;        /**< 状态码（响应时使用，32位） */
    uint16_t url_len;       /**< URL 长度 */
    uint16_t seqno;         /**< 序列号（用于匹配请求/响应） */
    uint32_t data_len;      /**< 数据长度 */
} ipc_header_t;

/**
 * @brief 流接收上下文，用于处理部分数据包
 */
typedef struct {
    uint32_t cur_len;       /**< 当前接收长度 */
    uint32_t total_len;     /**< 总长度 */
    uint8_t buffer[IPC_MAX_PACKET_SIZE]; /**< 接收缓冲区 */
} ipc_stream_ctx_t;

/**
 * @brief IPC URL引用
 */
typedef struct {
    char *url;              /**< URL字符串 */
    size_t url_len;         /**< URL长度 */
} ipc_url_ref_t;

/**
 * @brief IPC数据引用
 */
typedef struct {
    void *data;             /**< 数据指针 */
    size_t length;          /**< 数据长度 */
} ipc_data_ref_t;

/**
 * @brief IPC数据包输入回调函数类型
 * @param ipc_hdr IPC消息头部
 * @param arg 回调参数
 * @return 处理成功返回true，失败返回false
 */
typedef bool (*ipc_packet_handler_t)(ipc_header_t *ipc_hdr, void *arg);

/** @} */

/**
 * @defgroup IPC_Functions IPC函数
 * @{
 */

/**
 * @brief 初始化IPC头部
 * @param outb 输出缓冲区，必须至少有IPC_MAX_PACKET_SIZE字节
 * @param type 消息类型
 * @param status 状态码
 * @param seqno 序列号
 * @return IPC头部指针
 */
ipc_header_t *ipc_create_header(void *outb, uint8_t type, uint32_t status, uint16_t seqno);

/**
 * @brief 初始化IPC流接收上下文
 * @param recv 流接收上下文
 */
void ipc_stream_init(ipc_stream_ctx_t *recv);

/**
 * @brief 获取IPC URL
 * @param ipc_hdr IPC消息头部
 * @param url URL引用
 * @return 获取成功返回true，失败返回false
 */
bool ipc_get_url(const ipc_header_t *ipc_hdr, ipc_url_ref_t *url);

/**
 * @brief 获取IPC数据
 * @param ipc_hdr IPC消息头部
 * @param data 数据引用
 * @return 获取成功返回true，失败返回false
 */
bool ipc_get_data(const ipc_header_t *ipc_hdr, ipc_data_ref_t *data);

/**
 * @brief IPC流输入处理
 * @param recv 流接收上下文
 * @param buf 输入缓冲区
 * @param buf_len 缓冲区长度
 * @param callback 数据包处理回调函数
 * @param arg 回调参数
 * @return 处理成功返回true，失败返回false
 */
bool ipc_stream_feed(ipc_stream_ctx_t *recv, void *buf, size_t buf_len,
                       ipc_packet_handler_t callback, void *arg);

/**
 * @brief IPC数据包输入处理
 * @param buf 输入缓冲区
 * @param buf_len 缓冲区长度
 * @return 处理成功返回IPC头部指针，失败返回NULL
 */
ipc_header_t *ipc_packet_input(void *buf, size_t buf_len);

/**
 * @brief 发送消息
 * @param sock 套接字描述符
 * @param ipc_hdr IPC消息头部
 * @param url URL引用
 * @param data 数据引用
 * @return 发送成功返回true，失败返回false
 */
bool ipc_send_message(ssn_transport_t *transport, ipc_header_t *ipc_hdr, 
    const ipc_url_ref_t *url, const ipc_data_ref_t *data);

/** @} */

/**
 * @defgroup IPC_Helpers IPC辅助函数
 * @{
 */

/**
 * @name 网络字节序转换
 * @{
 */

/**
 * @brief 主机字节序转网络字节序（16位）
 * @param hostshort 主机字节序的16位值
 * @return 网络字节序的16位值
 */
static inline uint16_t ipc_htons(uint16_t hostshort) {
    return htons(hostshort);
}

/**
 * @brief 主机字节序转网络字节序（32位）
 * @param hostlong 主机字节序的32位值
 * @return 网络字节序的32位值
 */
static inline uint32_t ipc_htonl(uint32_t hostlong) {
    return htonl(hostlong);
}

/**
 * @brief 网络字节序转主机字节序（16位）
 * @param netshort 网络字节序的16位值
 * @return 主机字节序的16位值
 */
static inline uint16_t ipc_ntohs(uint16_t netshort) {
    return ntohs(netshort);
}

/**
 * @brief 网络字节序转主机字节序（32位）
 * @param netlong 网络字节序的32位值
 * @return 主机字节序的32位值
 */
static inline uint32_t ipc_ntohl(uint32_t netlong) {
    return ntohl(netlong);
}

/** @} */

/**
 * @name IPC头部操作
 * @{
 */

/**
 * @brief 获取IPC头部序列号
 * @param hdr IPC头部
 * @return 序列号
 */
static inline uint16_t ipc_get_seqno(const ipc_header_t *hdr) {
    return ipc_ntohs(hdr->seqno);
}

/**
 * @brief 获取IPC头部消息类型
 * @param hdr IPC头部
 * @return 消息类型
 */
static inline uint8_t ipc_get_msg_type(const ipc_header_t *hdr) {
    return hdr->msg_type;
}

/**
 * @brief 获取IPC头部状态码
 * @param hdr IPC头部
 * @return 状态码
 */
static inline uint32_t ipc_get_status(const ipc_header_t *hdr) {
    return ipc_ntohl(hdr->status);
}

/**
 * @brief 设置IPC头部状态码
 * @param hdr IPC头部
 * @param status 状态码
 */
static inline void ipc_set_status(ipc_header_t *hdr, uint32_t status) {
    hdr->status = ipc_htonl(status);
}

/**
 * @brief 获取IPC头部URL长度
 * @param hdr IPC头部
 * @return URL长度
 */
static inline uint16_t ipc_get_url_length(const ipc_header_t *hdr) {
    return ipc_ntohs(hdr->url_len);
}

/**
 * @brief 获取IPC头部数据长度
 * @param hdr IPC头部
 * @return 数据长度
 */
static inline uint32_t ipc_get_data_length(const ipc_header_t *hdr) {
    return ipc_ntohl(hdr->data_len);
}

/**
 * @brief 设置IPC头部序列号
 * @param hdr IPC头部
 * @param seqno 序列号
 */
static inline void ipc_set_seqno(ipc_header_t *hdr, uint16_t seqno) {
    hdr->seqno = ipc_htons(seqno);
}

/**
 * @brief 设置IPC头部URL长度
 * @param hdr IPC头部
 * @param url_len URL长度
 */
static inline void ipc_set_url_length(ipc_header_t *hdr, uint16_t url_len) {
    hdr->url_len = ipc_htons(url_len);
}

/**
 * @brief 设置IPC头部数据长度
 * @param hdr IPC头部
 * @param data_len 数据长度
 */
static inline void ipc_set_data_length(ipc_header_t *hdr, uint32_t data_len) {
    hdr->data_len = ipc_htonl(data_len);
}

/** @} */

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* CD_IPC_PROTOCOL_H */
