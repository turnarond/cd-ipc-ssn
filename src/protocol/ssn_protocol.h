/*
 * ssn_protocol.h - Protocol Layer Base Interface
 *
 * This file defines the common protocol types, structures, and interfaces
 * for the modular protocol layer architecture.
 */

#ifndef SSN_PROTOCOL_H
#define SSN_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

#include "../ssn_export.h"
#include "../transports/ssn_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup SSN_Protocol Protocol Layer
 * @{
 */

/**
 * @brief Protocol type enumeration
 */
typedef enum {
    SSN_PROTOCOL_RPC = 0,       /**< RPC request-reply protocol */
    SSN_PROTOCOL_PUBSUB,        /**< Publish-subscribe protocol */
    SSN_PROTOCOL_MSG            /**< Point-to-point message protocol */
} ssn_protocol_type_t;

/**
 * @brief Protocol role enumeration
 */
typedef enum {
    SSN_ROLE_REQ = 0,           /**< RPC requester (client) */
    SSN_ROLE_REP,               /**< RPC replier (server) */
    SSN_ROLE_SEND,              /**< Message sender (client) */
    SSN_ROLE_RECV,              /**< Message receiver (server) */
    SSN_ROLE_PUB,               /**< Publisher (server) */
    SSN_ROLE_SUB                /**< Subscriber (client) */
} ssn_role_t;

/**
 * @brief Protocol context base structure
 */
typedef struct ssn_protocol_ctx {
    ssn_protocol_type_t type;       /**< Protocol type */
    ssn_role_t role;               /**< Protocol role */
    ssn_transport_t *transport;    /**< Underlying transport */
    void *user_data;              /**< User data */
    void (*destroy)(struct ssn_protocol_ctx *ctx); /**< Destroy function */
} ssn_protocol_ctx_t;

/**
 * @brief RPC pending request structure
 */
typedef struct ssn_rpc_pending {
    uint16_t index;                /**< Pending index */
    uint16_t seqno;               /**< Request sequence number */
    uint32_t timeout_ms;           /**< Timeout value */
    void *callback;                /**< Callback function */
    void *arg;                    /**< Callback argument */
    struct timespec expire_time;    /**< Expire time */
} ssn_rpc_pending_t;

/**
 * @brief RPC method handler type
 */
typedef void (*ssn_rpc_handler_t)(uint16_t seqno, const char *method,
                                   const void *data, size_t data_len,
                                   void *arg);

/**
 * @brief RPC reply handler type
 */
typedef void (*ssn_rpc_reply_handler_t)(uint16_t seqno, uint32_t status,
                                         const void *data, size_t data_len,
                                         void *arg);

/**
 * @brief PubSub message handler type
 */
typedef void (*ssn_pubsub_msg_handler_t)(const char *topic,
                                         const void *data, size_t data_len,
                                         void *arg);

/**
 * @brief MSG message handler type
 */
typedef void (*ssn_msg_handler_t)(const char *queue,
                                   const void *data, size_t data_len,
                                   void *arg);

/**
 * @brief Create protocol context by role
 *
 * @param role Protocol role
 * @param callback Callback function (for roles that need callbacks)
 * @param arg User argument
 * @return Protocol context, NULL on failure
 */
SSN_API ssn_protocol_ctx_t *ssn_protocol_create(ssn_role_t role,
                                                  void *callback,
                                                  void *arg);

/**
 * @brief Destroy protocol context
 *
 * @param ctx Protocol context
 */
SSN_API void ssn_protocol_destroy(ssn_protocol_ctx_t *ctx);

/**
 * @brief Get protocol type
 *
 * @param ctx Protocol context
 * @return Protocol type
 */
SSN_API ssn_protocol_type_t ssn_protocol_get_type(ssn_protocol_ctx_t *ctx);

/**
 * @brief Get protocol role
 *
 * @param ctx Protocol context
 * @return Protocol role
 */
SSN_API ssn_role_t ssn_protocol_get_role(ssn_protocol_ctx_t *ctx);

/**
 * @brief Bind protocol to transport (for server roles)
 *
 * @param ctx Protocol context
 * @param transport Transport instance
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_protocol_bind(ssn_protocol_ctx_t *ctx, ssn_transport_t *transport);

/**
 * @brief Connect protocol to transport (for client roles)
 *
 * @param ctx Protocol context
 * @param transport Transport instance
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_protocol_connect(ssn_protocol_ctx_t *ctx, ssn_transport_t *transport);

/**
 * @brief Poll protocol for events
 *
 * @param ctx Protocol context
 * @param timeout_ms Timeout in milliseconds
 * @return Number of events processed, -1 on error
 */
SSN_API int ssn_protocol_poll(ssn_protocol_ctx_t *ctx, int timeout_ms);

/**
 * @brief Run protocol event loop
 *
 * @param ctx Protocol context
 */
SSN_API void ssn_protocol_run(ssn_protocol_ctx_t *ctx);

/**
 * @brief Check if protocol is connected
 *
 * @param ctx Protocol context
 * @return true if connected, false otherwise
 */
SSN_API bool ssn_protocol_is_connected(ssn_protocol_ctx_t *ctx);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* SSN_PROTOCOL_H */
