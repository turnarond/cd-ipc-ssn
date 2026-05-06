/*
 * IPC protocol - Industrial-grade implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "ssn_frame.h"
#include "util/ssn_log.h"
#include <errno.h>

/* ------------------ Helper Functions ------------------ */
static bool validate_and_get_total_length(const uint8_t *buf, size_t buf_size, size_t *out_total_len)
{
    if (buf_size < SSN_HEADER_SIZE) {
        return false;
    }

    ssn_header_t header;
    memcpy(&header, buf, sizeof(header));

    if (header.magic != SSN_MAGIC_BYTE || header.version != SSN_PROTOCOL_VERSION) {
        return false;
    }

    uint16_t url_len = ssn_ntohs(header.url_len);
    uint32_t data_len = ssn_ntohl(header.data_len);

    // Prevent overflow
    if ((uint64_t)url_len + data_len > SSN_MAX_PAYLOAD_SIZE) {
        return false;
    }

    size_t total_length = SSN_HEADER_SIZE + url_len + data_len;
    if (total_length > buf_size) {
        return false; // Truncated packet
    }

    if (out_total_len) {
        *out_total_len = total_length;
    }
    return true;
}

static void ssn_print_error(const uint8_t *buffer, const char *info, size_t offset, size_t len)
{
    if (len > SSN_HEADER_SIZE) len = SSN_HEADER_SIZE;
    fprintf(stderr, "SSN input header error: %s at offset %zu\n", info, offset);
    fprintf(stderr, "Header bytes: ");
    for (size_t i = 0; i < len; i++) {
        fprintf(stderr, "%02x ", buffer[i]);
    }
    fprintf(stderr, "\n");
}

/* ------------------ Public API ------------------ */

/**
 * @brief 发送消息
 * 
 * @param sock 套接字描述符
 * @param ipc_hdr IPC消息头部
 * @param url URL引用
 * @param data 数据引用
 * @return 发送成功返回true，失败返回false
 */
bool ssn_send_message(ssn_transport_t *transport, ssn_header_t *ipc_hdr, 
    const ssn_url_ref_t *url, const ssn_data_ref_t *data)
{
    ssize_t send_len;
    uint64_t total_length = (uint64_t)SSN_HEADER_SIZE;

    // 计算总长度
    if (url) {
        total_length += url->url_len;
        ssn_set_url_length(ipc_hdr, (uint16_t)url->url_len);
    }

    if (data) {
        total_length += data->length;
        ssn_set_data_length(ipc_hdr, data->length);
    }

    // 检查长度是否有效
    if (total_length > SSN_MAX_PACKET_SIZE || total_length < SSN_HEADER_SIZE) {
        LOG_ERROR("ssn send message failed: length %lu invalid", total_length);
        return false;
    }

    // 准备发送数据
    uint8_t *buffer = (uint8_t *)ipc_hdr;
    size_t pos = sizeof(ssn_header_t);

    // 添加URL数据
    if (url) {
        memcpy(buffer + pos, url->url, url->url_len);
        pos += url->url_len;
    }
    
    // 添加实际数据
    if (data && data->data) {
        memcpy(buffer + pos, data->data, data->length);
        pos += data->length;
    }

    // 发送消息
    send_len = ssn_transport_send(transport, buffer, pos);

    if (send_len < 0) {
        LOG_ERROR("ssn send message failed");
        return false;
    }
    LOG_DEBUG("ssn send message success, length is %lu", send_len);
    return true;
}

ssn_header_t *ssn_create_header(void *outb, uint8_t type, uint32_t status, uint16_t seqno)
{
    if (!outb) return NULL;

    ssn_header_t *hdr = (ssn_header_t *)outb;
    hdr->magic = SSN_MAGIC_BYTE;
    hdr->version = SSN_PROTOCOL_VERSION;
    hdr->msg_type = type;
    ssn_set_status(hdr, status);
    hdr->url_len = 0;
    ssn_set_seqno(hdr, seqno);
    hdr->data_len = 0;
    return hdr;
}

void ssn_stream_init(ssn_stream_ctx_t *recv)
{
    if (!recv) return;
    recv->cur_len = 0;
    recv->total_len = 0;
}

bool ssn_set_url(ssn_header_t *ipc_hdr, const ssn_url_ref_t *url)
{
    if (!ipc_hdr || !url) return false;
    if (ipc_hdr->magic != SSN_MAGIC_BYTE || ipc_hdr->version != SSN_PROTOCOL_VERSION) 
        return false;
    if (ntohl(ipc_hdr->data_len) != 0) return false; // data already set

    if (url->url_len > SSN_MAX_PAYLOAD_SIZE) return false;

    ssn_set_url_length(ipc_hdr, (uint16_t)url->url_len);
    if (url->url_len > 0) {
        memcpy((char*)(ipc_hdr + 1), url->url, url->url_len);
    }
    return true;
}

bool ssn_get_url(const ssn_header_t *ipc_hdr, ssn_url_ref_t *url)
{
    if (!ipc_hdr || !url) return false;
    if (ipc_hdr->magic != SSN_MAGIC_BYTE || ipc_hdr->version != SSN_PROTOCOL_VERSION) 
        return false;

    url->url_len = ssn_get_url_length(ipc_hdr);
    url->url = (url->url_len > 0) ? (char*)(ipc_hdr + 1) : NULL;
    return true;
}

bool ssn_get_data(const ssn_header_t *ipc_hdr, ssn_data_ref_t *data)
{
    if (!ipc_hdr || !data) return false;
    if (ipc_hdr->magic != SSN_MAGIC_BYTE || ipc_hdr->version != SSN_PROTOCOL_VERSION) 
        return false;

    data->length = ssn_get_data_length(ipc_hdr);
    if (data->length == 0) {
        data->data = NULL;
    } else {
        data->data = (char*)(ipc_hdr + 1) + ssn_get_url_length(ipc_hdr);
    }
    return true;
}

ssn_header_t *ssn_packet_input(void *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return NULL;
    size_t total_len;
    if (!validate_and_get_total_length((const uint8_t*)buf, buf_len, &total_len)) {
        ssn_print_error((const uint8_t*)buf, "Invalid header or truncated", 0, buf_len);
        return NULL;
    }
    return (ssn_header_t*)buf;
}

/* ------------------ Stream Parser (Sticky Packet Handling) ------------------ */

bool ssn_stream_feed(ssn_stream_ctx_t *recv, void *buf, size_t buf_len, ssn_packet_handler_t callback, void *arg)
{
    if (!recv || !callback) return false;
    if (!buf || buf_len == 0) return true; // nothing to do

    const uint8_t *input_data = (const uint8_t *)buf;
    size_t consumed_bytes = 0;

    // 处理输入数据
    while (consumed_bytes < buf_len) {
        // 检查缓冲区空间
        size_t space_left = SSN_MAX_PACKET_SIZE - recv->cur_len;
        if (space_left == 0) {
            // 缓冲区已满但没有完整数据包，视为格式错误的流
            recv->cur_len = 0;
            recv->total_len = 0;
            return false;
        }

        // 复制数据到缓冲区
        size_t bytes_to_copy = (buf_len - consumed_bytes) < space_left ? (buf_len - consumed_bytes) : space_left;
        memcpy(recv->buffer + recv->cur_len, input_data + consumed_bytes, bytes_to_copy);
        recv->cur_len += bytes_to_copy;
        consumed_bytes += bytes_to_copy;

        // 尝试提取尽可能多的完整数据包
        while (1) {
            // 检查是否有足够的数据读取头部
            if (recv->cur_len < SSN_HEADER_SIZE) break;

            // 验证头部并获取总长度
            size_t total_length;
            if (!validate_and_get_total_length(recv->buffer, recv->cur_len, &total_length)) {
                // 头部无效或被截断
                ssn_print_error(recv->buffer, "Invalid packet in stream", 0, recv->cur_len);
                recv->cur_len = 0;
                recv->total_len = 0;
                return false;
            }

            // 检查是否有足够的数据读取整个数据包
            if (recv->cur_len < total_length) break; // 数据不足

            // 有完整的数据包
            ssn_header_t *header = (ssn_header_t *)recv->buffer;
            if (!callback(header, arg)) {
                // 回调请求停止
                recv->cur_len = 0;
                recv->total_len = 0;
                return true;
            }

            // 移除已处理的数据包
            memmove(recv->buffer, recv->buffer + total_length, recv->cur_len - total_length);
            recv->cur_len -= total_length;
        }
    }

    return true;
}
