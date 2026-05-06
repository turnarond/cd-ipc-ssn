/*
 * ssn_protocol.c - Protocol Layer Base Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../ssn_export.h"
#include "rpc/ssn_rpc.h"
#include "pubsub/ssn_pubsub.h"
#include "msg/ssn_msg.h"
#include "../util/ssn_log.h"
#include "ssn_protocol.h"

#define SSN_PROTOCOL_VERSION 0x02

ssn_protocol_ctx_t *ssn_protocol_create(ssn_role_t role, void *callback, void *arg)
{
    ssn_protocol_ctx_t *ctx = (ssn_protocol_ctx_t *)calloc(1, sizeof(ssn_protocol_ctx_t));
    if (!ctx) {
        LOG_ERROR("Failed to allocate protocol context");
        return NULL;
    }

    ctx->role = role;
    ctx->user_data = arg;

    switch (role) {
    case SSN_ROLE_REQ:
        ctx->type = SSN_PROTOCOL_RPC;
        break;
    case SSN_ROLE_REP:
        ctx->type = SSN_PROTOCOL_RPC;
        break;
    case SSN_ROLE_SEND:
        ctx->type = SSN_PROTOCOL_MSG;
        break;
    case SSN_ROLE_RECV:
        ctx->type = SSN_PROTOCOL_MSG;
        break;
    case SSN_ROLE_PUB:
        ctx->type = SSN_PROTOCOL_PUBSUB;
        break;
    case SSN_ROLE_SUB:
        ctx->type = SSN_PROTOCOL_PUBSUB;
        break;
    default:
        LOG_ERROR("Invalid protocol role: %d", role);
        free(ctx);
        return NULL;
    }

    LOG_DEBUG("Created protocol context: type=%d, role=%d", ctx->type, ctx->role);
    return ctx;
}

void ssn_protocol_destroy(ssn_protocol_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }

    if (ctx->destroy) {
        ctx->destroy(ctx);
    }

    free(ctx);
}

ssn_protocol_type_t ssn_protocol_get_type(ssn_protocol_ctx_t *ctx)
{
    if (!ctx) {
        return SSN_PROTOCOL_RPC;
    }
    return ctx->type;
}

ssn_role_t ssn_protocol_get_role(ssn_protocol_ctx_t *ctx)
{
    if (!ctx) {
        return SSN_ROLE_REQ;
    }
    return ctx->role;
}

int ssn_protocol_bind(ssn_protocol_ctx_t *ctx, ssn_transport_t *transport)
{
    if (!ctx || !transport) {
        LOG_ERROR("Invalid arguments for protocol bind");
        return -1;
    }

    ctx->transport = transport;
    LOG_DEBUG("Protocol bound to transport");
    return 0;
}

int ssn_protocol_connect(ssn_protocol_ctx_t *ctx, ssn_transport_t *transport)
{
    if (!ctx || !transport) {
        LOG_ERROR("Invalid arguments for protocol connect");
        return -1;
    }

    ctx->transport = transport;
    LOG_DEBUG("Protocol connected to transport");
    return 0;
}

int ssn_protocol_poll(ssn_protocol_ctx_t *ctx, int timeout_ms)
{
    if (!ctx || !ctx->transport) {
        return -1;
    }

    /* 根据协议类型委托给专门的 poll 函数 */
    switch (ctx->type) {
    case SSN_PROTOCOL_RPC:
        return ssn_rpc_poll(ctx, timeout_ms);
    case SSN_PROTOCOL_PUBSUB:
        return ssn_pubsub_poll(ctx, timeout_ms);
    case SSN_PROTOCOL_MSG:
        return ssn_msg_poll(ctx, timeout_ms);
    default:
        /* 未知协议类型: 仅接收丢弃 */
        return ssn_transport_recv(ctx->transport, NULL, 0, timeout_ms);
    }
}

void ssn_protocol_run(ssn_protocol_ctx_t *ctx)
{
    if (!ctx || !ctx->transport) {
        LOG_ERROR("Invalid protocol context or transport");
        return;
    }

    LOG_DEBUG("Starting protocol run loop");

    while (ssn_protocol_is_connected(ctx)) {
        int ret = ssn_protocol_poll(ctx, 100);
        if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("Protocol poll failed: %s", strerror(errno));
            break;
        }
    }

    LOG_DEBUG("Protocol run loop exited");
}

bool ssn_protocol_is_connected(ssn_protocol_ctx_t *ctx)
{
    if (!ctx || !ctx->transport) {
        return false;
    }

    return ssn_transport_is_connected(ctx->transport);
}
