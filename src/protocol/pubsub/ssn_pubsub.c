/*
 * ssn_pubsub.c - Publish-Subscribe Protocol Module Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../../ssn_export.h"
#include "../../util/ssn_log.h"
#include "../../util/ssn_hash_table.h"
#include "../../ssn_frame.h"
#include "../ssn_protocol.h"
#include "ssn_pubsub.h"

#define PUBSUB_MAX_TOPICS 256
#define PUBSUB_MAX_SUBSCRIBERS 256

typedef struct pubsub_sub_entry {
    char topic[64];
    bool subscribed;
} pubsub_sub_entry_t;

typedef struct pubsub_subscriber_entry {
    ssn_transport_t *transport;
    bool active;
} pubsub_subscriber_entry_t;

ssn_pubsub_pub_t *ssn_pubsub_pub_create(void)
{
    ssn_pubsub_pub_t *pub = (ssn_pubsub_pub_t *)calloc(1, sizeof(ssn_pubsub_pub_t));
    if (!pub) {
        LOG_ERROR("Failed to allocate publisher context");
        return NULL;
    }

    pub->base.role = SSN_ROLE_PUB;
    pub->base.type = SSN_PROTOCOL_PUBSUB;
    pub->base.destroy = ssn_pubsub_destroy;   /* 销毁职责由 destroy 回调承载 */

    pub->subscriber_table = ssn_hash_table_create(16);
    if (!pub->subscriber_table) {
        LOG_ERROR("Failed to create subscriber table");
        free(pub);
        return NULL;
    }

    LOG_DEBUG("Created publisher context");
    return pub;
}

ssn_pubsub_sub_t *ssn_pubsub_sub_create(
    void (*on_message)(const char *topic, const void *data, size_t data_len, void *arg),
    void *arg)
{
    ssn_pubsub_sub_t *sub = (ssn_pubsub_sub_t *)calloc(1, sizeof(ssn_pubsub_sub_t));
    if (!sub) {
        LOG_ERROR("Failed to allocate subscriber context");
        return NULL;
    }

    sub->base.role = SSN_ROLE_SUB;
    sub->base.type = SSN_PROTOCOL_PUBSUB;
    sub->base.destroy = ssn_pubsub_destroy;   /* 销毁职责由 destroy 回调承载 */
    sub->on_message = on_message;
    sub->user_arg = arg;

    sub->topic_table = ssn_hash_table_create(16);
    if (!sub->topic_table) {
        LOG_ERROR("Failed to create topic table");
        free(sub);
        return NULL;
    }

    LOG_DEBUG("Created subscriber context");
    return sub;
}

void ssn_pubsub_destroy(ssn_protocol_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }

    if (ctx->type == SSN_PROTOCOL_PUBSUB) {
        if (ctx->role == SSN_ROLE_PUB) {
            ssn_pubsub_pub_t *pub = (ssn_pubsub_pub_t *)ctx;
            if (pub->subscriber_table) {
                ssn_hash_table_destroy(pub->subscriber_table);
            }
        } else if (ctx->role == SSN_ROLE_SUB) {
            ssn_pubsub_sub_t *sub = (ssn_pubsub_sub_t *)ctx;
            if (sub->topic_table) {
                ssn_hash_table_destroy(sub->topic_table);
            }
        }
    }

    free(ctx);
}

int ssn_pubsub_pub_bind(ssn_pubsub_pub_t *ctx, ssn_transport_t *transport)
{
    if (!ctx || !transport) {
        LOG_ERROR("Invalid arguments for pub bind");
        return -1;
    }

    ctx->base.transport = transport;
    LOG_DEBUG("Publisher bound to transport");
    return 0;
}

int ssn_pubsub_sub_connect(ssn_pubsub_sub_t *ctx, ssn_transport_t *transport)
{
    if (!ctx || !transport) {
        LOG_ERROR("Invalid arguments for sub connect");
        return -1;
    }

    ctx->base.transport = transport;
    LOG_DEBUG("Subscriber connected to transport");
    return 0;
}

int ssn_pubsub_sub_subscribe(ssn_pubsub_sub_t *ctx, const char *topic)
{
    if (!ctx || !topic) {
        LOG_ERROR("Invalid arguments for subscribe");
        return -1;
    }

    if (!ctx->base.transport) {
        LOG_ERROR("Subscriber not connected");
        return -1;
    }

    pubsub_sub_entry_t *entry = (pubsub_sub_entry_t *)malloc(sizeof(pubsub_sub_entry_t));
    if (!entry) {
        LOG_ERROR("Failed to allocate subscription entry");
        return -1;
    }

    strncpy(entry->topic, topic, sizeof(entry->topic) - 1);
    entry->topic[sizeof(entry->topic) - 1] = '\0';
    entry->subscribed = true;

    /* 字符串键 API：内容哈希 + 表内复制 key（缺陷背景：原按指针比较） */
    if (!ssn_hash_table_set_str(ctx->topic_table, topic, entry)) {
        LOG_ERROR("Failed to subscribe (hash set): %s", topic);
        free(entry);
        return -1;
    }
    ctx->topic_count++;

    uint8_t sendbuf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *header = ssn_create_header(sendbuf, SSN_MSG_TYPE_SUBSCRIBE, 0, 0);

    ssn_url_ref_t url_ref = {
        .url = (char *)topic,
        .url_len = (uint16_t)strlen(topic)
    };

    if (!ssn_send_message(ctx->base.transport, header, &url_ref, NULL)) {
        LOG_ERROR("Failed to send subscribe message");
        ssn_hash_table_remove_str(ctx->topic_table, topic);
        free(entry);
        ctx->topic_count--;
        return -1;
    }

    LOG_DEBUG("Subscribed to topic: %s", topic);
    return 0;
}

int ssn_pubsub_sub_unsubscribe(ssn_pubsub_sub_t *ctx, const char *topic)
{
    if (!ctx || !topic) {
        return -1;
    }

    pubsub_sub_entry_t *entry = (pubsub_sub_entry_t *)ssn_hash_table_get_str(ctx->topic_table, topic);
    if (entry) {
        if (ctx->base.transport) {
            uint8_t sendbuf[SSN_MAX_PACKET_SIZE];
            ssn_header_t *header = ssn_create_header(sendbuf, SSN_MSG_TYPE_UNSUBSCRIBE, 0, 0);

            ssn_url_ref_t url_ref = {
                .url = (char *)topic,
                .url_len = (uint16_t)strlen(topic)
            };

            ssn_send_message(ctx->base.transport, header, &url_ref, NULL);
        }

        ssn_hash_table_remove_str(ctx->topic_table, topic);
        free(entry);
        ctx->topic_count--;

        LOG_DEBUG("Unsubscribed from topic: %s", topic);
        return 0;
    }

    return -1;
}

int ssn_pubsub_pub_publish(
    ssn_pubsub_pub_t *ctx,
    const char *topic,
    const void *data,
    size_t data_len)
{
    if (!ctx || !topic) {
        LOG_ERROR("Invalid arguments for publish");
        return -1;
    }

    if (!ctx->base.transport) {
        LOG_ERROR("Publisher not bound");
        return -1;
    }

    uint8_t sendbuf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *header = ssn_create_header(sendbuf, SSN_MSG_TYPE_PUBLISH, 0, 0);

    ssn_url_ref_t url_ref = {
        .url = (char *)topic,
        .url_len = (uint16_t)strlen(topic)
    };

    ssn_data_ref_t data_ref = {
        .data = (void *)data,
        .length = (uint32_t)data_len
    };

    if (!ssn_send_message(ctx->base.transport, header, &url_ref, &data_ref)) {
        LOG_ERROR("Failed to send publish message");
        return -1;
    }

    LOG_DEBUG("Published message to topic: %s, data_len=%zu", topic, data_len);
    return 0;
}

int ssn_pubsub_handle_message(ssn_pubsub_sub_t *sub, const ssn_header_t *hdr)
{
    if (!sub || !hdr) {
        LOG_ERROR("Invalid arguments for pubsub handle_message");
        return -1;
    }

    if (hdr->msg_type != SSN_MSG_TYPE_PUBLISH) {
        return 0;
    }
    if (!sub->on_message) {
        return 0;
    }

    ssn_url_ref_t url_ref;
    ssn_data_ref_t data_ref;
    ssn_get_url(hdr, &url_ref);
    ssn_get_data(hdr, &data_ref);
    sub->on_message(url_ref.url, data_ref.data,
                    data_ref.length, sub->user_arg);
    return 1;
}

int ssn_pubsub_poll(ssn_protocol_ctx_t *ctx, int timeout_ms)
{
    if (!ctx || !ctx->transport) {
        return -1;
    }

    uint8_t buf[SSN_MAX_PACKET_SIZE];
    int len = ssn_transport_recv(ctx->transport, buf, SSN_MAX_PACKET_SIZE, timeout_ms);
    if (len <= 0) return len;

    ssn_header_t *hdr = ssn_packet_input(buf, len);
    if (!hdr) return -1;

    if (ctx->role == SSN_ROLE_SUB) {
        /* 收编到 handle 原语（Issue #31 v1.1）：帧校验 + 回调触发单份化 */
        ssn_pubsub_handle_message((ssn_pubsub_sub_t *)ctx, hdr);
    }

    return 1;
}

bool ssn_pubsub_is_connected(ssn_protocol_ctx_t *ctx)
{
    if (!ctx || !ctx->transport) {
        return false;
    }

    return ssn_transport_is_connected(ctx->transport);
}
