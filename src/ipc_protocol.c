/*
 * IPC protocol - Industrial-grade implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "ipc_protocol.h"
#include "util/ipc_log.h"

/* ------------------ Helper Functions ------------------ */
static bool validate_and_get_total_length(const uint8_t *buf, size_t buf_size, size_t *out_total_len)
{
    if (buf_size < IPC_HEADER_SIZE) {
        return false;
    }

    ipc_header_t header;
    memcpy(&header, buf, sizeof(header));

    if (header.magic != IPC_MAGIC_BYTE || header.version != IPC_PROTOCOL_VERSION) {
        return false;
    }

    uint16_t url_len = ipc_ntohs(header.url_len);
    uint32_t data_len = ipc_ntohl(header.data_len);

    // Prevent overflow
    if ((uint64_t)url_len + data_len > IPC_MAX_PAYLOAD_SIZE) {
        return false;
    }

    size_t total_length = IPC_HEADER_SIZE + url_len + data_len;
    if (total_length > buf_size) {
        return false; // Truncated packet
    }

    if (out_total_len) {
        *out_total_len = total_length;
    }
    return true;
}

static void ipc_print_error(const uint8_t *buffer, const char *info, size_t offset, size_t len)
{
    if (len > IPC_HEADER_SIZE) len = IPC_HEADER_SIZE;
    fprintf(stderr, "IPC input header error: %s at offset %zu\n", info, offset);
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
bool ipc_send_message(int sock, ipc_header_t *ipc_hdr, 
    const ipc_url_ref_t *url, const ipc_data_ref_t *data)
{
    ssize_t send_len;
    uint64_t total_length = (uint64_t)IPC_HEADER_SIZE;

    // 计算总长度
    if (url) {
        total_length += url->url_len;
        ipc_set_url_length(ipc_hdr, (uint16_t)url->url_len);
    }

    if (data) {
        total_length += data->length;
        ipc_set_data_length(ipc_hdr, data->length);
    }

    // 检查长度是否有效
    if (total_length > IPC_MAX_PACKET_SIZE || total_length < IPC_HEADER_SIZE) {
        LOG_ERROR("ipc send message failed: length %lu invalid", total_length);
        return false;
    }

    // 准备发送数据
    struct iovec iov[3] = {
        {
            .iov_base = (void*)ipc_hdr,
            .iov_len = sizeof(ipc_header_t)
        }
    };

    struct msghdr msg = {0};
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    
    // 添加URL数据
    if (url) {
        iov[msg.msg_iovlen].iov_base = url->url;
        iov[msg.msg_iovlen].iov_len = url->url_len;
        msg.msg_iovlen++;
    }
    
    // 添加实际数据
    if (data) {
        if (data->data) {
            iov[msg.msg_iovlen].iov_base = data->data;
            iov[msg.msg_iovlen].iov_len = data->length;
            msg.msg_iovlen++;
        }
    }

    // 发送消息
    send_len = sendmsg(sock, &msg, 0);

    if (send_len < 0) {
        LOG_ERROR("ipc send message failed, errno %d", errno);
        return false;
    }
    LOG_DEBUG("ipc send message success, length is %lu", send_len);
    return true;
}

ipc_header_t *ipc_create_header(void *outb, uint8_t type, uint32_t status, uint16_t seqno)
{
    if (!outb) return NULL;

    ipc_header_t *hdr = (ipc_header_t *)outb;
    hdr->magic = IPC_MAGIC_BYTE;
    hdr->version = IPC_PROTOCOL_VERSION;
    hdr->msg_type = type;
    ipc_set_status(hdr, status);
    hdr->url_len = 0;
    ipc_set_seqno(hdr, seqno);
    hdr->data_len = 0;
    return hdr;
}

void ipc_stream_init(ipc_stream_ctx_t *recv)
{
    if (!recv) return;
    recv->cur_len = 0;
    recv->total_len = 0;
}

bool ipc_set_url(ipc_header_t *ipc_hdr, const ipc_url_ref_t *url)
{
    if (!ipc_hdr || !url) return false;
    if (ipc_hdr->magic != IPC_MAGIC_BYTE || ipc_hdr->version != IPC_PROTOCOL_VERSION) 
        return false;
    if (ntohl(ipc_hdr->data_len) != 0) return false; // data already set

    if (url->url_len > IPC_MAX_PAYLOAD_SIZE) return false;

    ipc_set_url_length(ipc_hdr, (uint16_t)url->url_len);
    if (url->url_len > 0) {
        memcpy((char*)(ipc_hdr + 1), url->url, url->url_len);
    }
    return true;
}

bool ipc_get_url(const ipc_header_t *ipc_hdr, ipc_url_ref_t *url)
{
    if (!ipc_hdr || !url) return false;
    if (ipc_hdr->magic != IPC_MAGIC_BYTE || ipc_hdr->version != IPC_PROTOCOL_VERSION) 
        return false;

    url->url_len = ipc_get_url_length(ipc_hdr);
    url->url = (url->url_len > 0) ? (char*)(ipc_hdr + 1) : NULL;
    return true;
}

bool ipc_get_data(const ipc_header_t *ipc_hdr, ipc_data_ref_t *data)
{
    if (!ipc_hdr || !data) return false;
    if (ipc_hdr->magic != IPC_MAGIC_BYTE || ipc_hdr->version != IPC_PROTOCOL_VERSION) 
        return false;

    data->length = ipc_get_data_length(ipc_hdr);
    if (data->length == 0) {
        data->data = NULL;
    } else {
        data->data = (char*)(ipc_hdr + 1) + ipc_get_url_length(ipc_hdr);
    }
    return true;
}

ipc_header_t *ipc_packet_input(void *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return NULL;
    size_t total_len;
    if (!validate_and_get_total_length((const uint8_t*)buf, buf_len, &total_len)) {
        ipc_print_error((const uint8_t*)buf, "Invalid header or truncated", 0, buf_len);
        return NULL;
    }
    return (ipc_header_t*)buf;
}

/* ------------------ Stream Parser (Sticky Packet Handling) ------------------ */

bool ipc_stream_feed(ipc_stream_ctx_t *recv, void *buf, size_t buf_len, ipc_packet_handler_t callback, void *arg)
{
    if (!recv || !callback) return false;
    if (!buf || buf_len == 0) return true; // nothing to do

    const uint8_t *input_data = (const uint8_t *)buf;
    size_t consumed_bytes = 0;

    // 处理输入数据
    while (consumed_bytes < buf_len) {
        // 检查缓冲区空间
        size_t space_left = IPC_MAX_PACKET_SIZE - recv->cur_len;
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
            if (recv->cur_len < IPC_HEADER_SIZE) break;

            // 验证头部并获取总长度
            size_t total_length;
            if (!validate_and_get_total_length(recv->buffer, recv->cur_len, &total_length)) {
                // 头部无效或被截断
                ipc_print_error(recv->buffer, "Invalid packet in stream", 0, recv->cur_len);
                recv->cur_len = 0;
                recv->total_len = 0;
                return false;
            }

            // 检查是否有足够的数据读取整个数据包
            if (recv->cur_len < total_length) break; // 数据不足

            // 有完整的数据包
            ipc_header_t *header = (ipc_header_t *)recv->buffer;
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
