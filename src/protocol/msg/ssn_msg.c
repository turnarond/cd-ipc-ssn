/*
 * ssn_msg.c - Message Protocol Module Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../../ssn_export.h"
#include "../../util/ssn_log.h"
#include "../../ssn_frame.h"
#include "../ssn_protocol.h"
#include "ssn_msg.h"

ssn_msg_send_t *ssn_msg_send_create(void)
{
    ssn_msg_send_t *send = (ssn_msg_send_t *)calloc(1, sizeof(ssn_msg_send_t));
    if (!send) {
        LOG_ERROR("Failed to allocate message sender context");
        return NULL;
    }

    send->base.role = SSN_ROLE_SEND;
    send->base.type = SSN_PROTOCOL_MSG;
    send->base.destroy = ssn_msg_destroy;   /* 销毁职责由 destroy 回调承载 */
    send->next_seqno = 1;

    LOG_DEBUG("Created message sender context");
    return send;
}

ssn_msg_recv_t *ssn_msg_recv_create(
    void (*on_message)(const void *data, size_t data_len, void *arg),
    void *arg)
{
    ssn_msg_recv_t *recv = (ssn_msg_recv_t *)calloc(1, sizeof(ssn_msg_recv_t));
    if (!recv) {
        LOG_ERROR("Failed to allocate message receiver context");
        return NULL;
    }

    recv->base.role = SSN_ROLE_RECV;
    recv->base.type = SSN_PROTOCOL_MSG;
    recv->base.destroy = ssn_msg_destroy;   /* 销毁职责由 destroy 回调承载 */
    recv->on_message = on_message;
    recv->user_arg = arg;

    LOG_DEBUG("Created message receiver context");
    return recv;
}

void ssn_msg_destroy(ssn_protocol_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }

    free(ctx);
}

int ssn_msg_recv_bind(ssn_msg_recv_t *ctx, ssn_transport_t *transport)
{
    if (!ctx || !transport) {
        LOG_ERROR("Invalid arguments for msg recv bind");
        return -1;
    }

    ctx->base.transport = transport;
    LOG_DEBUG("Message receiver bound to transport");
    return 0;
}

int ssn_msg_send_connect(ssn_msg_send_t *ctx, ssn_transport_t *transport)
{
    if (!ctx || !transport) {
        LOG_ERROR("Invalid arguments for msg send connect");
        return -1;
    }

    ctx->base.transport = transport;
    LOG_DEBUG("Message sender connected to transport");
    return 0;
}

int ssn_msg_send(
    ssn_msg_send_t *ctx,
    const void *data,
    size_t data_len)
{
    if (!ctx) {
        LOG_ERROR("Invalid arguments for msg send");
        return -1;
    }

    if (!ctx->base.transport) {
        LOG_ERROR("Message sender not connected");
        return -1;
    }

    uint8_t sendbuf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *header = ssn_create_header(sendbuf, SSN_MSG_TYPE_MESSAGE, 0, ctx->next_seqno++);

    ssn_data_ref_t data_ref = {
        .data = (void *)data,
        .length = (uint32_t)data_len
    };

    if (!ssn_send_message(ctx->base.transport, header, NULL, &data_ref)) {
        LOG_ERROR("Failed to send message");
        return -1;
    }

    LOG_DEBUG("Message sent: seqno=%u, data_len=%zu", ctx->next_seqno - 1, data_len);
    return 0;
}

int ssn_msg_handle_data(ssn_msg_recv_t *recv, const ssn_header_t *hdr)
{
    if (!recv || !hdr) {
        LOG_ERROR("Invalid arguments for msg handle_data");
        return -1;
    }

    if (hdr->msg_type != SSN_MSG_TYPE_MESSAGE) {
        return 0;
    }
    if (!recv->on_message) {
        return 0;
    }

    ssn_data_ref_t data_ref;
    ssn_get_data(hdr, &data_ref);
    recv->on_message(data_ref.data, data_ref.length, recv->user_arg);
    return 1;
}

int ssn_msg_poll(ssn_protocol_ctx_t *ctx, int timeout_ms)
{
    if (!ctx || !ctx->transport) {
        return -1;
    }

    uint8_t buf[SSN_MAX_PACKET_SIZE];
    int len = ssn_transport_recv(ctx->transport, buf, SSN_MAX_PACKET_SIZE, timeout_ms);
    if (len <= 0) return len;

    ssn_header_t *hdr = ssn_packet_input(buf, len);
    if (!hdr) return -1;

    if (ctx->role == SSN_ROLE_RECV) {
        /* 收编到 handle 原语（Issue #31 v1.1）：帧校验 + 回调触发单份化 */
        ssn_msg_handle_data((ssn_msg_recv_t *)ctx, hdr);
    }

    return 1;
}

bool ssn_msg_is_connected(ssn_protocol_ctx_t *ctx)
{
    if (!ctx || !ctx->transport) {
        return false;
    }

    return ssn_transport_is_connected(ctx->transport);
}
