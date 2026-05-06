/*
 * ssn_msg.h - Message Protocol Module Interface
 */

#ifndef SSN_MSG_H
#define SSN_MSG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../../ssn_export.h"
#include "../ssn_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup SSN_MSG Message Protocol
 * @{
 */

/**
 * @brief Message sender context (client side)
 */
typedef struct ssn_msg_send {
    ssn_protocol_ctx_t base;
    uint16_t next_seqno;
} ssn_msg_send_t;

/**
 * @brief Message receiver context (server side)
 */
typedef struct ssn_msg_recv {
    ssn_protocol_ctx_t base;
    void (*on_message)(const void *data, size_t data_len, void *arg);
    void *user_arg;
} ssn_msg_recv_t;

/**
 * @brief Create message sender context
 * @return Sender context, NULL on failure
 */
SSN_API ssn_msg_send_t *ssn_msg_send_create(void);

/**
 * @brief Create message receiver context
 * @param on_message Message callback
 * @param arg User argument
 * @return Receiver context, NULL on failure
 */
SSN_API ssn_msg_recv_t *ssn_msg_recv_create(
    void (*on_message)(const void *data, size_t data_len, void *arg),
    void *arg);

/**
 * @brief Destroy message context
 * @param ctx Protocol context
 */
SSN_API void ssn_msg_destroy(ssn_protocol_ctx_t *ctx);

/**
 * @brief Bind receiver to transport
 * @param ctx Receiver context
 * @param transport Transport instance
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_msg_recv_bind(ssn_msg_recv_t *ctx, ssn_transport_t *transport);

/**
 * @brief Connect sender to transport
 * @param ctx Sender context
 * @param transport Transport instance
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_msg_send_connect(ssn_msg_send_t *ctx, ssn_transport_t *transport);

/**
 * @brief Send a message
 * @param ctx Sender context
 * @param data Message data
 * @param data_len Data length
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_msg_send(
    ssn_msg_send_t *ctx,
    const void *data,
    size_t data_len);

/**
 * @brief Poll for message events
 * @param ctx Protocol context
 * @param timeout_ms Timeout in milliseconds
 * @return Number of events processed, -1 on error
 */
SSN_API int ssn_msg_poll(ssn_protocol_ctx_t *ctx, int timeout_ms);

/**
 * @brief Check if message context is connected
 * @param ctx Protocol context
 * @return true if connected, false otherwise
 */
SSN_API bool ssn_msg_is_connected(ssn_protocol_ctx_t *ctx);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* SSN_MSG_H */
