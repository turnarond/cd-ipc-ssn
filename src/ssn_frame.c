/*
 * IPC protocol - Industrial-grade implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include "ssn_frame.h"
#include "util/ssn_log.h"
#include <errno.h>

/* ------------------ Helper Functions ------------------ */
/* 校验头部（协议层面）并计算包总长度。不校验数据是否完整：
 * 调用方区分「截断等待」（流式接收）与「截断拒绝」（单包输入）。 */
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

    if (out_total_len) {
        *out_total_len = SSN_HEADER_SIZE + url_len + data_len;
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

    // 校验输入（在写头部之前完成，避免截断写头部导致帧头与实际数据错位）
    if (url) {
        /* url_len 写入头部为 uint16_t，超界必须拒绝而非截断 */
        if (url->url_len > 0xFFFF) {
            LOG_ERROR("ssn send message failed: url_len %zu exceeds uint16 limit", url->url_len);
            return false;
        }
        if (!url->url && url->url_len > 0) {
            LOG_ERROR("ssn send message failed: url NULL but url_len > 0");
            return false;
        }
    }
    if (data) {
        /* data_len 写入头部为 uint32_t，超界必须拒绝而非截断 */
        if (data->length > 0xFFFFFFFFULL) {
            LOG_ERROR("ssn send message failed: data length %llu exceeds uint32 limit",
                      (unsigned long long)data->length);
            return false;
        }
        if (!data->data && data->length > 0) {
            LOG_ERROR("ssn send message failed: data NULL but length > 0");
            return false;
        }
    }

    // 计算总长度
    if (url) {
        total_length += url->url_len;
        ssn_set_url_length(ipc_hdr, (uint16_t)url->url_len);
    }

    if (data) {
        total_length += data->length;
        ssn_set_data_length(ipc_hdr, (uint32_t)data->length);
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

    // 发送消息：循环处理部分写入（TCP 大包/慢对端/小 SO_SNDBUF 下 send 可能
    // 只发送部分字节，单次 send 返回后剩余数据必须补发，否则对端收到残缺帧）
    /* EAGAIN 重试受 transport 发送超时约束：慢客户端/满缓冲区不应无限持锁阻塞
     * 调用方（尤其 server 在锁内发送时，无限重试会拖垮整个事件循环——DoS）。
     * 缺陷背景：原实现遇 EAGAIN 用 nanosleep(1ms) 盲重试（最多 send_timeout_ms
     * 次），不检测 socket 可写性——慢对端填满 SO_SNDBUF 后，服务端唯一事件循环
     * 持锁空转最多 5s，期间所有 recv/握手/超时停滞。修复：改用 poll(POLLOUT)
     * 等待可写（一次等待，受剩余超时预算约束），可写后立即重试 send。 */
    int send_timeout_ms = transport->config.send_timeout_ms > 0 ?
                          transport->config.send_timeout_ms : 5000;
    uint64_t deadline_ms = (uint64_t)send_timeout_ms;
    int sock_fd = ssn_transport_get_fd(transport);
    size_t sent = 0;
    while (sent < pos) {
        send_len = ssn_transport_send(transport, buffer + sent, pos - sent);
        if (send_len > 0) {
            sent += (size_t)send_len;
        } else if (send_len < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* 非阻塞发送缓冲区满：poll(POLLOUT) 等待可写（受 deadline 约束），
             * 可写后重试；超时则放弃。比盲 nanosleep 高效且不空转持锁。 */
            if (deadline_ms == 0 || sock_fd < 0) {
                LOG_ERROR("ssn send message failed: send timeout after %d ms",
                          send_timeout_ms);
                return false;
            }
            struct pollfd pfd;
            pfd.fd = sock_fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            int wait_ms = (int)(deadline_ms < 100 ? deadline_ms : 100);
            int pr = poll(&pfd, 1, wait_ms);
            if (pr < 0 && errno != EINTR) {
                LOG_ERROR("ssn send message failed: poll error %s", strerror(errno));
                return false;
            }
            /* 消耗等待预算：poll 超时（pr==0）全额扣除；就绪/中断按等待量扣除
             * （保守取 wait_ms，保证总阻塞不超过 send_timeout_ms） */
            if (deadline_ms <= (uint64_t)wait_ms) {
                deadline_ms = 0;
            } else {
                deadline_ms -= (uint64_t)wait_ms;
            }
            if (pr > 0 && (pfd.revents & (POLLOUT | POLLERR | POLLHUP))) {
                /* 可写（含对端关闭/错误）：再试一次 send；若 POLLERR/POLLHUP
                 * 则下一次 send 会返回错误并走失败分支 */
                continue;
            }
        } else {
            LOG_ERROR("ssn send message failed: send returned %zd (errno %d)",
                      send_len, errno);
            return false;
        }
    }
    LOG_DEBUG("ssn send message success, length is %zu", sent);
    return true;
}

/* 定义处显式 default 可见性（缺陷背景：-O3 + -fvisibility=hidden 下 GCC 对
 * 部分「声明带 default 但被库内调用」的函数在 IPA 路径丢弃可见性，符号残留
 * GLOBAL HIDDEN → 外部链接失败（P1-1）。声明处 SSN_API 对多数函数生效，此处
 * 为 create_header/packet_input 补定义处属性兜底） */
__attribute__((visibility("default"))) ssn_header_t *ssn_create_header(void *outb, uint8_t type, uint32_t status, uint16_t seqno)
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

__attribute__((visibility("default"))) ssn_header_t *ssn_packet_input(void *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return NULL;
    size_t total_len;
    if (!validate_and_get_total_length((const uint8_t*)buf, buf_len, &total_len)) {
        ssn_print_error((const uint8_t*)buf, "Invalid header or truncated", 0, buf_len);
        return NULL;
    }
    if (total_len > buf_len) {
        ssn_print_error((const uint8_t*)buf, "Invalid header or truncated", 0, buf_len);
        return NULL; // Truncated packet
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
