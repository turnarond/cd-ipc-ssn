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
#include "../ssn_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

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
