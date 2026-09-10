/*
 * ssn_rpc.c - RPC Protocol Module Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "../../ssn_export.h"
#include "../../ssn_error.h"
#include "../../util/ssn_log.h"
#include "../../util/ssn_hash_table.h"
#include "../../ssn_frame.h"
#include "../ssn_protocol.h"
#include "ssn_rpc.h"

#define RPC_MAX_PENDING 256
#define RPC_DEFAULT_TIMEOUT_MS 5000

typedef struct rpc_method_entry {
    char method_name[64];
    ssn_rpc_handler_t handler;
    void *arg;
} rpc_method_entry_t;

static rpc_pending_entry_t *pending_pool_find(ssn_rpc_req_t *req, uint16_t seqno)
{
    for (int i = 0; i < RPC_MAX_PENDING; i++) {
        if (req->pending_pool[i].in_use && req->pending_pool[i].seqno == seqno) {
            return &req->pending_pool[i];
        }
    }
    return NULL;
}

static rpc_pending_entry_t *pending_pool_alloc(ssn_rpc_req_t *req)
{
    for (int i = 0; i < RPC_MAX_PENDING; i++) {
        if (!req->pending_pool[i].in_use) {
            req->pending_pool[i].in_use = true;
            return &req->pending_pool[i];
        }
    }
    return NULL;
}

static void pending_pool_free(ssn_rpc_req_t *req, rpc_pending_entry_t *entry)
{
    if (entry) {
        entry->in_use = false;
    }
}

ssn_rpc_req_t *ssn_rpc_req_create(ssn_rpc_reply_handler_t on_reply, void *arg)
{
    ssn_rpc_req_t *req = (ssn_rpc_req_t *)calloc(1, sizeof(ssn_rpc_req_t));
    if (!req) {
        LOG_ERROR("Failed to allocate RPC requester context");
        return NULL;
    }

    req->base.role = SSN_ROLE_REQ;
    req->base.type = SSN_PROTOCOL_RPC;
    req->base.destroy = ssn_rpc_destroy;   /* 销毁职责由 destroy 回调承载（基类不重复 free） */
    req->on_reply = on_reply;
    req->base.user_data = arg;
    req->next_seqno = 1;

    req->pending_pool = calloc(RPC_MAX_PENDING, sizeof(rpc_pending_entry_t));
    if (!req->pending_pool) {
        LOG_ERROR("Failed to allocate pending pool");
        free(req);
        return NULL;
    }

    LOG_DEBUG("Created RPC requester context");
    return req;
}

ssn_rpc_rep_t *ssn_rpc_rep_create(ssn_rpc_handler_t on_request, void *arg)
{
    ssn_rpc_rep_t *rep = (ssn_rpc_rep_t *)calloc(1, sizeof(ssn_rpc_rep_t));
    if (!rep) {
        LOG_ERROR("Failed to allocate RPC replier context");
        return NULL;
    }

    rep->base.role = SSN_ROLE_REP;
    rep->base.type = SSN_PROTOCOL_RPC;
    rep->base.destroy = ssn_rpc_destroy;
    rep->on_request = on_request;
    rep->base.user_data = arg;

    rep->method_table = ssn_hash_table_create(16);
    if (!rep->method_table) {
        LOG_ERROR("Failed to create method table");
        free(rep);
        return NULL;
    }

    LOG_DEBUG("Created RPC replier context");
    return rep;
}

void ssn_rpc_destroy(ssn_protocol_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }

    if (ctx->type == SSN_PROTOCOL_RPC) {
        if (ctx->role == SSN_ROLE_REQ) {
            ssn_rpc_req_t *req = (ssn_rpc_req_t *)ctx;
            if (req->pending_pool) {
                free(req->pending_pool);
            }
        } else if (ctx->role == SSN_ROLE_REP) {
            ssn_rpc_rep_t *rep = (ssn_rpc_rep_t *)ctx;
            if (rep->method_table) {
                ssn_hash_table_destroy(rep->method_table);
            }
        }
    }

    free(ctx);
}

int ssn_rpc_bind(ssn_protocol_ctx_t *ctx, ssn_transport_t *transport)
{
    if (!ctx || !transport) {
        LOG_ERROR("Invalid arguments for RPC bind");
        return -1;
    }

    ctx->transport = transport;
    LOG_DEBUG("RPC replier bound to transport");
    return 0;
}

int ssn_rpc_connect(ssn_protocol_ctx_t *ctx, ssn_transport_t *transport)
{
    if (!ctx || !transport) {
        LOG_ERROR("Invalid arguments for RPC connect");
        return -1;
    }

    ctx->transport = transport;
    LOG_DEBUG("RPC requester connected to transport");
    return 0;
}

int ssn_rpc_register(ssn_rpc_rep_t *rep, const char *method_name,
                     ssn_rpc_handler_t handler, void *arg)
{
    if (!rep || !method_name || !handler) {
        LOG_ERROR("Invalid arguments for RPC register");
        return -1;
    }

    rpc_method_entry_t *entry = (rpc_method_entry_t *)malloc(sizeof(rpc_method_entry_t));
    if (!entry) {
        LOG_ERROR("Failed to allocate method entry");
        return -1;
    }

    snprintf(entry->method_name, sizeof(entry->method_name), "%s", method_name);
    entry->handler = handler;
    entry->arg = arg;

    /* 字符串键 API：按内容哈希 + 表内复制 key（缺陷背景：原按指针比较，
     * 内容相同地址不同的字符串注销静默失败、重复注册旧 entry 泄漏） */
    if (!ssn_hash_table_set_str(rep->method_table, method_name, entry)) {
        LOG_ERROR("Failed to register method (hash set): %s", method_name);
        free(entry);
        return -1;
    }
    rep->method_count++;

    LOG_DEBUG("Registered RPC method: %s", method_name);
    return 0;
}

int ssn_rpc_unregister(ssn_rpc_rep_t *rep, const char *method_name)
{
    if (!rep || !method_name) {
        return -1;
    }

    rpc_method_entry_t *entry = (rpc_method_entry_t *)ssn_hash_table_get_str(rep->method_table, method_name);
    if (entry) {
        ssn_hash_table_remove_str(rep->method_table, method_name);
        free(entry);
        rep->method_count--;
        return 0;
    }

    return -1;
}

int ssn_rpc_call(ssn_rpc_req_t *req, const char *method_name,
                  const void *data, size_t data_len, uint64_t timeout_ms)
{
    if (!req || !method_name) {
        LOG_ERROR("Invalid arguments for RPC call");
        return -1;
    }

    if (!req->base.transport) {
        LOG_ERROR("RPC requester not connected");
        return -1;
    }

    rpc_pending_entry_t *pending = pending_pool_alloc(req);
    if (!pending) {
        LOG_ERROR("No pending slot available");
        return -1;
    }

    /* seqno 分配：跳过仍在途的序号（缺陷背景：uint16_t 回绕后复用旧 seqno，
     * 延迟应答会被错配到新请求）。64K 次调用内必有空闲序号（pending 仅 256 槽） */
    uint16_t seqno = req->next_seqno++;
    for (int i = 0; i < RPC_MAX_PENDING + 1; i++) {
        if (!pending_pool_find(req, seqno)) {
            break;
        }
        seqno = req->next_seqno++;
        if (i == RPC_MAX_PENDING) {
            LOG_ERROR("No free seqno available (all in-flight)");
            pending_pool_free(req, pending);
            return -1;
        }
    }
    pending->seqno = seqno;
    pending->timeout_ms = timeout_ms ? timeout_ms : RPC_DEFAULT_TIMEOUT_MS;
    pending->callback = req->on_reply;
    pending->arg = req->base.user_data;

    clock_gettime(CLOCK_MONOTONIC, &pending->expire_time);
    pending->expire_time.tv_sec += pending->timeout_ms / 1000;
    pending->expire_time.tv_nsec += (pending->timeout_ms % 1000) * 1000000;
    if (pending->expire_time.tv_nsec >= 1000000000) {
        pending->expire_time.tv_sec++;
        pending->expire_time.tv_nsec -= 1000000000;
    }

    uint8_t sendbuf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *header = ssn_create_header(sendbuf, SSN_MSG_TYPE_RPC_REQUEST, 0, pending->seqno);

    ssn_url_ref_t url_ref = {
        .url = (char *)method_name,
        .url_len = (uint16_t)strlen(method_name)
    };

    ssn_data_ref_t data_ref = {
        .data = (void *)data,
        .length = (uint32_t)data_len
    };

    if (!ssn_send_message(req->base.transport, header, &url_ref, &data_ref)) {
        LOG_ERROR("Failed to send RPC request");
        pending_pool_free(req, pending);
        return -1;
    }

    LOG_DEBUG("RPC call sent: method=%s, seqno=%u", method_name, pending->seqno);
    return 0;
}

int ssn_rpc_response(ssn_rpc_rep_t *rep, uint16_t seqno, uint32_t status,
                      const void *data, size_t data_len)
{
    if (!rep || !rep->base.transport) {
        LOG_ERROR("Invalid arguments for RPC response");
        return -1;
    }

    /* 应答复用 SSN_MSG_TYPE_RPC_REQUEST + status（缺陷背景：原用私有宏
     * RPC_MSG_TYPE_RPC_RESPONSE(0x10)，与 client/server 主路径（0x01+status）
     * 两套体系不一致，跨实现组合 RPC 应答永远无法匹配） */
    uint8_t sendbuf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *header = ssn_create_header(sendbuf, SSN_MSG_TYPE_RPC_REQUEST, status, seqno);

    ssn_data_ref_t data_ref = {
        .data = (void *)data,
        .length = (uint32_t)data_len
    };

    if (!ssn_send_message(rep->base.transport, header, NULL, &data_ref)) {
        LOG_ERROR("Failed to send RPC response");
        return -1;
    }

    LOG_DEBUG("RPC response sent: seqno=%u, status=%u", seqno, status);
    return 0;
}

int ssn_rpc_handle_reply(ssn_rpc_req_t *req, const ssn_header_t *hdr)
{
    if (!req || !hdr) {
        LOG_ERROR("Invalid arguments for RPC handle_reply");
        return -1;
    }

    /* 帧校验：应答复用 SSN_MSG_TYPE_RPC_REQUEST（Issue #19 统一）；非应答帧
     * 不属于本原语（返回 0 不触发回调，由上层类型路由兜底） */
    if (hdr->msg_type != SSN_MSG_TYPE_RPC_REQUEST) {
        return 0;
    }

    uint16_t seqno = ssn_get_seqno(hdr);
    rpc_pending_entry_t *pending = pending_pool_find(req, seqno);
    if (!pending) {
        /* 无匹配：协议池在生产路径为空属预期（请求登记于 client 池），
         * 上层池匹配兜底——本原语仅完成帧校验与状态机检查 */
        return 0;
    }

    if (pending->callback) {
        ssn_data_ref_t data_ref;
        ssn_get_data(hdr, &data_ref);
        pending->callback(seqno, ssn_get_status(hdr),
                          data_ref.data, data_ref.length, pending->arg);
    }
    pending_pool_free(req, pending);
    return 1;
}

int ssn_rpc_handle_request(ssn_rpc_rep_t *rep, const ssn_header_t *hdr)
{
    if (!rep || !hdr) {
        LOG_ERROR("Invalid arguments for RPC handle_request");
        return -1;
    }

    if (hdr->msg_type != SSN_MSG_TYPE_RPC_REQUEST) {
        return 0;
    }
    if (!rep->on_request) {
        return 0;
    }

    ssn_url_ref_t url_ref;
    ssn_data_ref_t data_ref;
    ssn_get_url(hdr, &url_ref);
    ssn_get_data(hdr, &data_ref);
    rep->on_request(ssn_get_seqno(hdr), url_ref.url,
                    data_ref.data, data_ref.length, rep->base.user_data);
    return 1;
}

int ssn_rpc_poll(ssn_protocol_ctx_t *ctx, int timeout_ms)
{
    if (!ctx || !ctx->transport) {
        return -1;
    }

    /* RPC 请求端：先清理已过期 pending 槽（缺陷背景：expire_time 只算不用，
     * 无应答时槽位永久占用，256 槽耗尽后 RPC 永久不可用；超时回调亦不触发） */
    if (ctx->role == SSN_ROLE_REQ) {
        ssn_rpc_req_t *req = (ssn_rpc_req_t *)ctx;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        for (int i = 0; i < RPC_MAX_PENDING; i++) {
            rpc_pending_entry_t *entry = &req->pending_pool[i];
            if (!entry->in_use) continue;
            if (now.tv_sec > entry->expire_time.tv_sec ||
                (now.tv_sec == entry->expire_time.tv_sec &&
                 now.tv_nsec >= entry->expire_time.tv_nsec)) {
                /* 超时：触发回调（status=SSN_ECODE_TIMEOUT，data=NULL）后释放槽位 */
                if (entry->callback) {
                    entry->callback(entry->seqno, SSN_ECODE_TIMEOUT,
                                    NULL, 0, entry->arg);
                }
                pending_pool_free(req, entry);
            }
        }
    }

    uint8_t buf[SSN_MAX_PACKET_SIZE];
    int len = ssn_transport_recv(ctx->transport, buf, SSN_MAX_PACKET_SIZE, timeout_ms);
    if (len <= 0) return len;

    ssn_header_t *hdr = ssn_packet_input(buf, len);
    if (!hdr) return -1;

    /* 根据角色分发：收编到 handle 原语（Issue #31 v1.1）——帧校验/协议
     * 状态机更新/回调触发单份化；返回语义与重构前完全一致（REQ 非应答
     * 类型返回 0，其余返回 1） */
    if (ctx->role == SSN_ROLE_REQ) {
        if (hdr->msg_type != SSN_MSG_TYPE_RPC_REQUEST) {
            return 0;
        }
        ssn_rpc_handle_reply((ssn_rpc_req_t *)ctx, hdr);
    } else if (ctx->role == SSN_ROLE_REP) {
        ssn_rpc_handle_request((ssn_rpc_rep_t *)ctx, hdr);
    }

    return 1;
}

bool ssn_rpc_is_connected(ssn_protocol_ctx_t *ctx)
{
    if (!ctx || !ctx->transport) {
        return false;
    }

    return ssn_transport_is_connected(ctx->transport);
}
