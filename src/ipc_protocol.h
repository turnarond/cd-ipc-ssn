/*
 * IPC protocol
*/

#ifndef IPC_PROTOCOL_H
#define IPC_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef _WIN32
#include <winsock.h>
#else
#include <arpa/inet.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------ Constants ------------------ */

#define IPC_HEADER_SIZE     sizeof(ipc_header_t)
#define IPC_MAX_PACKET_SIZE (131072U)          // 128 KiB, 32-bit aligned
#define IPC_MAX_PAYLOAD_SIZE  (IPC_MAX_PACKET_SIZE - IPC_HEADER_SIZE)

#define IPC_MAGIC_BYTE          (0x09U)
#define IPC_PROTOCOL_VERSION    (0x01U)

/* Message Types */
#define IPC_MSG_TYPE_SERVICE_INFO     (0x00U)
#define IPC_MSG_TYPE_RPC_REQUEST      (0x01U)
#define IPC_MSG_TYPE_SUBSCRIBE        (0x02U)
#define IPC_MSG_TYPE_UNSUBSCRIBE      (0x03U)
#define IPC_MSG_TYPE_PUBLISH          (0x04U)
#define IPC_MSG_TYPE_DATAGRAM         (0x05U)
#define IPC_MSG_TYPE_REPLY_FLAG       (0xFCU)  // Not a standalone type; used as flag in replies
#define IPC_MSG_TYPE_PING_ECHO        (0xFFU)

/* Status Codes */
#define IPC_STATUS_OK                 (0U)
#define IPC_STATUS_AUTH_FAILED        (1U)
#define IPC_STATUS_INVALID_ARGS       (2U)
#define IPC_STATUS_BAD_URL            (3U)
#define IPC_STATUS_TIMEOUT            (4U)
#define IPC_STATUS_ACCESS_DENIED      (5U)
#define IPC_STATUS_OUT_OF_MEMORY      (6U)

/* ------------------ Structures ------------------ */
/**
 * IPC packet header (packed, network byte order for multi-byte fields)
 */
typedef struct __attribute__((packed)) {
    uint8_t magic;          // 魔数
    uint8_t version;        // 协议版本号
    uint8_t msg_type;       // 消息类型
    uint8_t status;         // 状态码（响应时使用）
    uint16_t url_len;       // URL 长度
    uint16_t seqno;         // 序列号（用于匹配请求/响应）
    uint32_t data_len;      // 数据长度
} ipc_header_t;

/**
 * Streaming receiver context for handling partial packets
 */
typedef struct {
    uint32_t cur_len;
    uint32_t total_len;
    uint8_t buffer[IPC_MAX_PACKET_SIZE];
} ipc_stream_ctx_t;

/* IPC url */
typedef struct {
    char *url;
    size_t url_len;
} ipc_url_ref_t;

/* IPC payload */
typedef struct {
    void *data;
    size_t length;
} ipc_payload_ref_t;

/* IPC packet input callback */
typedef bool (*ipc_packet_handler_t)(ipc_header_t *ipc_hdr, void *arg);

/* Initialize IPC header (`outb` must have at least IPC_MAX_PACKET_SIZE bytes) */
ipc_header_t *ipc_create_header(void *outb, uint8_t type, uint8_t status, uint16_t seqno);

/* Initialize IPC receiver */
void ipc_stream_init(ipc_stream_ctx_t *recv);

/* Get IPC url */
bool ipc_get_url(const ipc_header_t *ipc_hdr, ipc_url_ref_t *url);

/* Get IPC payload */
bool ipc_get_payload(const ipc_header_t *ipc_hdr, ipc_payload_ref_t *payload);

/* IPC input */
bool ipc_stream_feed(ipc_stream_ctx_t *recv, void *buf, size_t buf_len,
                       ipc_packet_handler_t callback, void *arg);

/* IPC packet input */
ipc_header_t *ipc_packet_input(void *buf, size_t buf_len);

/* ------------------ Inline Helpers ------------------ */

/* IPC header get seqno */
static inline uint16_t ipc_get_seqno(const ipc_header_t *hdr) {
    return ntohs(hdr->seqno);
}

static inline uint8_t ipc_get_msg_type(const ipc_header_t *hdr) {
    return hdr->msg_type;
}

static inline uint8_t ipc_get_status(const ipc_header_t *hdr) {
    return hdr->status;
}

static inline uint16_t ipc_get_url_length(const ipc_header_t *hdr) {
    return ntohs(hdr->url_len);
}

static inline uint32_t ipc_get_payload_length(const ipc_header_t *hdr) {
    return ntohl(hdr->data_len);
}

#ifdef __cplusplus
}
#endif

#endif /* IPC_PROTOCOL_H */
/*
 * end
 */
