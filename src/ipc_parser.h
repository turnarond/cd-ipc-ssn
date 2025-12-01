/*
 * IPC parser
*/

#ifndef IPC_PARSER_H
#define IPC_PARSER_H

/* IPC header length */
#define IPC_HDR_LENGTH  sizeof(ipc_header_t)

/* IPC max packet length (32 bits align) */
#define IPC_MAX_PACKET_LENGTH 131072

/* IPC max payload length */
#define IPC_MAX_DATA_LENGTH  (IPC_MAX_PACKET_LENGTH - IPC_HDR_LENGTH)

/* IPC magic and version */
#define IPC_MAGIC    0x9
#define IPC_VERSION  0x1

/* IPC header types */
#define IPC_TYPE_SERVINFO     0x00
#define IPC_TYPE_RPC          0x01
#define IPC_TYPE_SUBSCRIBE    0x02
#define IPC_TYPE_UNSUBSCRIBE  0x03
#define IPC_TYPE_PUBLISH      0x04
#define IPC_TYPE_DATAGRAM     0x05
#define IPC_FLAG_REPLY        0xfc
#define IPC_TYPE_NOOP         0xfe
#define IPC_TYPE_PINGECHO     0xff

/* IPC status code */
#define IPC_STATUS_SUCCESS        0
#define IPC_STATUS_PASSWORD       1
#define IPC_STATUS_ARGUMENTS      2
#define IPC_STATUS_INVALID_URL    3
#define IPC_STATUS_NO_RESPONDING  4
#define IPC_STATUS_NO_PERMISSIONS 5
#define IPC_STATUS_NO_MEMORY      6

/* Headers */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
/* IPC header (multibyte members use network byte order) */
typedef struct __attribute__((packed)) {
    uint8_t magic;          // 魔数
    uint8_t version;        // 协议版本号
    uint8_t type;           // 消息类型
    uint8_t status;         // 状态码（响应时使用）
    uint16_t url_len;       // URL 长度
    uint16_t seqno;         // 序列号（用于匹配请求/响应）
    uint32_t data_len;      // 数据长度
} ipc_header_t;

/* IPC receiver state and buffer */
typedef struct {
    uint32_t cur_len;
    uint32_t total_len;
    uint8_t buffer[IPC_MAX_PACKET_LENGTH];
} ipc_recv_t;

/* IPC url */
typedef struct {
    char *url;
    size_t url_len;
} ipc_url_t;

/* IPC payload */
typedef struct {
    void *data;
    size_t data_len;
} ipc_payload_t;

/* IPC packet input callback */
typedef bool (*ipc_input_callback_t)(void *arg, ipc_header_t *ipc_hdr);

/* Initialize IPC header (`outb` must have at least IPC_MAX_PACKET_LENGTH bytes) */
ipc_header_t *ipc_parser_init_header(void *outb, uint8_t type, uint8_t status, uint16_t seqno);

/* Initialize IPC receiver */
void ipc_parser_init_recv(ipc_recv_t *recv);

/* Get IPC url */
bool ipc_parser_get_url(const ipc_header_t *ipc_hdr, ipc_url_t *url);

/* Get IPC payload */
bool ipc_parser_get_payload(const ipc_header_t *ipc_hdr, ipc_payload_t *payload);

/* IPC input */
bool ipc_parser_input(ipc_recv_t *recv, void *buf, size_t buf_len,
                       ipc_input_callback_t callback, void *arg);

/* IPC packet input */
ipc_header_t *ipc_parser_packet_input(void *buf, size_t buf_len);

/* IPC header set seqno */
#define ipc_parser_set_seqno(ipc_hdr, seqno) ((ipc_hdr)->seqno = htons(seqno))

/* IPC header get seqno */
#define ipc_parser_get_seqno(ipc_hdr) ntohs((ipc_hdr)->seqno)

/* IPC header get type */
#define ipc_parser_get_type(ipc_hdr)  ((ipc_hdr)->type)

/* IPC header get status */
#define ipc_parser_get_status(ipc_hdr)  ((ipc_hdr)->status)

/* IPC header get url length */
#define ipc_parser_get_url_len(ipc_hdr)  ntohs((ipc_hdr)->url_len)

/* IPC header get data length */
#define ipc_parser_get_data_len(ipc_hdr)  ntohl((ipc_hdr)->data_len)

#ifdef __cplusplus
}
#endif

#endif /* IPC_PARSER_H */
/*
 * end
 */
