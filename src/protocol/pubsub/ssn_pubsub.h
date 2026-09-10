/*
 * ssn_pubsub.h - Publish-Subscribe Protocol Module Interface
 */

#ifndef SSN_PUBSUB_H
#define SSN_PUBSUB_H

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
 * @defgroup SSN_PUBSUB Publish-Subscribe Protocol
 * @{
 */

/**
 * @brief Publisher context (server side)
 */
typedef struct ssn_pubsub_pub {
    ssn_protocol_ctx_t base;
    void *subscriber_table;
    size_t subscriber_count;
} ssn_pubsub_pub_t;

/**
 * @brief Subscriber context (client side)
 */
typedef struct ssn_pubsub_sub {
    ssn_protocol_ctx_t base;
    void *topic_table;
    size_t topic_count;
    void (*on_message)(const char *topic, const void *data, size_t data_len, void *arg);
    void *user_arg;
} ssn_pubsub_sub_t;

/**
 * @brief Create publisher context
 * @return Publisher context, NULL on failure
 */
SSN_API ssn_pubsub_pub_t *ssn_pubsub_pub_create(void);

/**
 * @brief Create subscriber context
 * @param on_message Message callback
 * @param arg User argument
 * @return Subscriber context, NULL on failure
 */
SSN_API ssn_pubsub_sub_t *ssn_pubsub_sub_create(
    void (*on_message)(const char *topic, const void *data, size_t data_len, void *arg),
    void *arg);

/**
 * @brief Destroy PubSub context
 * @param ctx Protocol context
 */
SSN_API void ssn_pubsub_destroy(ssn_protocol_ctx_t *ctx);

/**
 * @brief Bind publisher to transport
 * @param ctx Publisher context
 * @param transport Transport instance
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_pubsub_pub_bind(ssn_pubsub_pub_t *ctx, ssn_transport_t *transport);

/**
 * @brief Connect subscriber to transport
 * @param ctx Subscriber context
 * @param transport Transport instance
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_pubsub_sub_connect(ssn_pubsub_sub_t *ctx, ssn_transport_t *transport);

/**
 * @brief Subscribe to a topic
 * @param ctx Subscriber context
 * @param topic Topic name
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_pubsub_sub_subscribe(ssn_pubsub_sub_t *ctx, const char *topic);

/**
 * @brief Unsubscribe from a topic
 * @param ctx Subscriber context
 * @param topic Topic name
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_pubsub_sub_unsubscribe(ssn_pubsub_sub_t *ctx, const char *topic);

/**
 * @brief Publish a message to all subscribers of a topic
 * @param ctx Publisher context
 * @param topic Topic name
 * @param data Message data
 * @param data_len Data length
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_pubsub_pub_publish(
    ssn_pubsub_pub_t *ctx,
    const char *topic,
    const void *data,
    size_t data_len);

/**
 * @brief Poll PubSub for events
 * @param ctx Protocol context
 * @param timeout_ms Timeout in milliseconds
 * @return Number of events processed, -1 on error
 */
SSN_API int ssn_pubsub_poll(ssn_protocol_ctx_t *ctx, int timeout_ms);

/**
 * @brief 处理 PUBLISH 消息帧（无 I/O、无锁、纯函数式）
 *
 * 帧校验（msg_type/topic/data 提取）→ 触发 sub->on_message(topic, data,
 * len, arg)。事件循环归属与收编边界见 ssn_protocol.h @note（Issue #31 v1.1：
 * 本原语不匹配上层 per-URL 订阅表，订阅匹配由上层在自身回调内完成）。
 *
 * @param sub 订阅端上下文
 * @param hdr 已解帧的头部
 * @return 1=已分发；0=帧有效但非 PUBLISH 或无 on_message；-1=参数非法
 */
SSN_API int ssn_pubsub_handle_message(ssn_pubsub_sub_t *sub, const ssn_header_t *hdr);

/**
 * @brief Check if PubSub is connected
 * @param ctx Protocol context
 * @return true if connected, false otherwise
 */
SSN_API bool ssn_pubsub_is_connected(ssn_protocol_ctx_t *ctx);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* SSN_PUBSUB_H */
