/*
 * ssn_rpc.h - RPC Protocol Module Interface
 *
 * This file defines the RPC protocol interfaces for request-reply pattern.
 */

#ifndef SSN_RPC_H
#define SSN_RPC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../../ssn_export.h"
#include "../../ssn_frame.h"
#include "../ssn_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @note 事件循环归属（Issue #31，v1.1）：生产应用应使用 ssn_client_poll /
 *       ssn_server_poll / ssn_node_poll 驱动事件循环——client/server 层是唯一
 *       权威循环；协议层为「纯状态机 + 编解码」，不拥有循环。ssn_rpc_poll /
 *       ssn_protocol_poll 为协议层独立嵌入/测试模式（单步：至多一次 recv +
 *       调 handle 原语）。协议层无锁，handle 原语假定调用方已串行化。
 *
 * @note pending 混用禁令：本模块的 pending 池（rpc_pending_entry_t，仅服务裸
 *       ssn_rpc_call 场景）与 ssn_client 的 pending 池为两套独立实现，不得在
 *       同一连接上混用（生产路径使用 ssn_client_call 等上层 API）。
 */

/**
 * @defgroup SSN_RPC RPC Protocol
 * @{
 */

/**
 * @brief Pending RPC entry (internal structure)
 */
typedef struct rpc_pending_entry {
    uint16_t seqno;
    uint64_t timeout_ms;
    ssn_rpc_reply_handler_t callback;
    void *arg;
    struct timespec expire_time;
    bool in_use;
} rpc_pending_entry_t;

/**
 * @brief RPC requester context (client side)
 */
typedef struct ssn_rpc_req {
    ssn_protocol_ctx_t base;
    ssn_rpc_reply_handler_t on_reply;
    uint16_t next_seqno;
    rpc_pending_entry_t *pending_pool;
    void *pending_lock;
} ssn_rpc_req_t;

/**
 * @brief RPC replier context (server side)
 */
typedef struct ssn_rpc_rep {
    ssn_protocol_ctx_t base;
    ssn_rpc_handler_t on_request;
    void *method_table;
    size_t method_count;
} ssn_rpc_rep_t;

/**
 * @brief Create RPC requester context (client side)
 *
 * @param on_reply Reply callback function
 * @param arg User argument
 * @return RPC requester context, NULL on failure
 */
SSN_API ssn_rpc_req_t *ssn_rpc_req_create(ssn_rpc_reply_handler_t on_reply, void *arg);

/**
 * @brief Create RPC replier context (server side)
 *
 * @param on_request Request handler callback
 * @param arg User argument
 * @return RPC replier context, NULL on failure
 */
SSN_API ssn_rpc_rep_t *ssn_rpc_rep_create(ssn_rpc_handler_t on_request, void *arg);

/**
 * @brief Destroy RPC context
 *
 * @param ctx RPC context (can be either requester or replier)
 */
SSN_API void ssn_rpc_destroy(ssn_protocol_ctx_t *ctx);

/**
 * @brief Bind RPC replier to transport
 *
 * @param ctx RPC replier context
 * @param transport Transport instance
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_rpc_bind(ssn_protocol_ctx_t *ctx, ssn_transport_t *transport);

/**
 * @brief Connect RPC requester to transport
 *
 * @param ctx RPC requester context
 * @param transport Transport instance
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_rpc_connect(ssn_protocol_ctx_t *ctx, ssn_transport_t *transport);

/**
 * @brief Register RPC method on replier
 *
 * @param ctx RPC replier context
 * @param method_name Method name
 * @param handler Method handler
 * @param arg User argument
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_rpc_register(ssn_rpc_rep_t *ctx, const char *method_name,
                              ssn_rpc_handler_t handler, void *arg);

/**
 * @brief Unregister RPC method
 *
 * @param ctx RPC replier context
 * @param method_name Method name
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_rpc_unregister(ssn_rpc_rep_t *ctx, const char *method_name);

/**
 * @brief Call RPC method (async)
 *
 * @param ctx RPC requester context
 * @param method_name Method name
 * @param data Request data
 * @param data_len Data length
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_rpc_call(ssn_rpc_req_t *ctx, const char *method_name,
                          const void *data, size_t data_len, uint64_t timeout_ms);

/**
 * @brief Send RPC response
 *
 * @param ctx RPC replier context
 * @param seqno Sequence number from request
 * @param status Status code
 * @param data Response data
 * @param data_len Data length
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_rpc_response(ssn_rpc_rep_t *ctx, uint16_t seqno, uint32_t status,
                               const void *data, size_t data_len);

/**
 * @brief Poll RPC for events
 *
 * @param ctx RPC context
 * @param timeout_ms Timeout in milliseconds
 * @return Number of events processed, -1 on error
 */
SSN_API int ssn_rpc_poll(ssn_protocol_ctx_t *ctx, int timeout_ms);

/**
 * @brief 处理 RPC 应答帧（无 I/O、无锁、纯函数式）
 *
 * 事件循环归属（Issue #31 v1.1）：本原语由调用方在其串行化上下文中驱动。
 * 职责：帧校验（msg_type/seqno/status/data 提取）→ 协议池匹配
 * （req->pending_pool，协议层状态机）→ 命中则触发该 pending 的回调并释放槽位。
 *
 * 收编边界：不匹配任何上层（client/server）池——上层池匹配由调用方在自身回调
 * 内完成；协议池在生产路径为空属预期（请求登记于 client 池），本原语仅做帧
 * 校验与状态机更新，不触发回调（返回 0）。
 *
 * @param req RPC 请求端上下文
 * @param hdr 已解帧的头部（须来自 ssn_create_header / ssn_packet_input）
 * @return 1=已分发（命中 pending 并触发回调）；0=帧有效但未命中（非 RPC 应答
 *         类型或无匹配 pending）；-1=参数非法
 */
SSN_API int ssn_rpc_handle_reply(ssn_rpc_req_t *req, const ssn_header_t *hdr);

/**
 * @brief 处理 RPC 请求帧（无 I/O、无锁；仅服务裸协议路径）
 *
 * 帧校验（msg_type/url/seqno/data 提取）→ 触发 rep->on_request(seqno, method,
 * data, len, arg)。方法表由 ssn_rpc_register 维护；现状分发不经方法表查找
 * （与既有 ssn_rpc_poll 行为等价）。应答由调用方显式调 ssn_rpc_response。
 * server 生产路径的方法表归属不变（见规格 v1.1 §3.2），不通过本原语分发。
 *
 * @param rep RPC 应答端上下文
 * @param hdr 已解帧的头部
 * @return 1=已分发；0=帧有效但非 RPC 请求或无 on_request；-1=参数非法
 */
SSN_API int ssn_rpc_handle_request(ssn_rpc_rep_t *rep, const ssn_header_t *hdr);

/**
 * @brief Check if RPC is connected
 *
 * @param ctx RPC context
 * @return true if connected, false otherwise
 */
SSN_API bool ssn_rpc_is_connected(ssn_protocol_ctx_t *ctx);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* SSN_RPC_H */
