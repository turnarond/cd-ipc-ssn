/*
 * ipc_node_comm.c - Node communication interfaces implementation
 *
 * This file implements the communication interfaces for the node abstraction layer,
 * including message sending, publish/subscribe, and RPC functionality.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ssn_node.h"
#include "../util/ssn_log.h"

/* 记录 node 客户端当前连接的对端地址（单连接模型：client 一次只连一个 peer）。
 * 缺陷背景：connect_to_peer 只查 is_connect 不比较地址，先连 A 再向 B 发送时
 * 消息仍走 A 连接（订阅/发送串台）。该字段由持有 node->lock 的路径读写。 */
static const char *peer_address_of(ssn_node_t *node);

/**
 * @brief Connect client to peer（调用方须已持有 node->lock）
 * 
 * 缺陷背景：原实现持 node->lock 执行阻塞 ssn_client_connect（最长 3s），形成
 * 锁序 node->lock→client->lock，与 client 超时回调（持 client->lock 调 node
 * API）的反向锁序构成死锁。修复：本函数只做「快照 + ref 保活 + 地址校验」，
 * 实际连接由调用方在解锁后执行。
 * 
 * @param node Node instance
 * @param peer_address Peer address
 * @param[out] out_client 返回保活后的 client（调用方须在解锁后 ssn_client_unref）
 * @return true on success（client 已连接或已可连接），false on failure
 */
static bool connect_to_peer(ssn_node_t *node, const char *peer_address,
                            ssn_client_t **out_client)
{
    if (!node || !peer_address || !out_client) {
        return false;
    }

    /* Get or create client（须在锁内：可能创建节点 client） */
    ssn_client_t *client = ssn_node_get_client(node);
    if (!client) {
        return false;
    }

    /* 地址校验：单连接模型下，已连其他 peer 时拒绝（避免消息串台） */
    if (ssn_client_is_connect(client)) {
        const char *cur = peer_address_of(node);
        if (cur && strcmp(cur, peer_address) != 0) {
            LOG_ERROR("connect_to_peer: client already connected to '%s', "
                      "cannot send to different peer '%s' (single-connection model)",
                      cur, peer_address);
            return false;
        }
        *out_client = client;
        ssn_client_ref(client);   /* 保活到解锁后使用 */
        return true;
    }

    *out_client = client;
    ssn_client_ref(client);       /* 保活到解锁后使用 */
    return true;
}

/**
 * @brief 获取 node 客户端当前连接地址（调用方须已持有 node->lock）
 */
static const char *peer_address_of(ssn_node_t *node)
{
    if (!node || !node->client) {
        return NULL;
    }
    return node->peer_address[0] ? node->peer_address : NULL;
}

/**
 * @brief Send message to a peer
 */
bool ssn_node_send_to_peer(ssn_node_t *node, const char *peer_address,
                          const ssn_url_ref_t *url, const ssn_data_ref_t *data)
{
    if (!node || !peer_address || !url) {
        LOG_ERROR("ssn_node_send_to_peer: invalid parameters");
        return false;
    }

    /* 锁内快照 + ref 保活，解锁后执行阻塞连接与发送（消除 node↔client 锁序死锁） */
    ssn_client_t *client = NULL;
    ipc_mutex_lock(node->lock);
    if (!connect_to_peer(node, peer_address, &client)) {
        ipc_mutex_unlock(node->lock);
        return false;
    }
    ipc_mutex_unlock(node->lock);

    if (!ssn_client_is_connect(client)) {
        struct timespec timeout = { .tv_sec = 3, .tv_nsec = 0 };
        if (!ssn_client_connect(client, peer_address, &timeout)) {
            LOG_ERROR("ssn_node_send_to_peer: failed to connect to %s", peer_address);
            ssn_client_unref(client);
            return false;
        }
        ipc_mutex_lock(node->lock);
        snprintf(node->peer_address, sizeof(node->peer_address), "%s", peer_address);
        ipc_mutex_unlock(node->lock);
    }

    /* Send message */
    int result = ssn_client_message(client, url, data);
    if (result < 0) {
        LOG_ERROR("ssn_node_send_to_peer: failed to send message");
        ssn_client_unref(client);
        return false;
    }

    ipc_mutex_lock(node->lock);
    node->last_activity = time(NULL);
    ipc_mutex_unlock(node->lock);
    ssn_client_unref(client);

    LOG_DEBUG("ssn_node_send_to_peer: message sent to %s", peer_address);
    return true;
}

/**
 * @brief Publish message to all subscribers
 */
bool ssn_node_publish(ssn_node_t *node, const ssn_url_ref_t *url,
                     const ssn_data_ref_t *data)
{
    if (!node || !url) {
        LOG_ERROR("ssn_node_publish: invalid parameters");
        return false;
    }

    ipc_mutex_lock(node->lock);

    if (!node->server) {
        LOG_ERROR("ssn_node_publish: server not available");
        ipc_mutex_unlock(node->lock);
        return false;
    }

    // Publish message
    int result = ssn_server_publish(node->server, url, data);
    if (result < 0) {
        LOG_ERROR("ssn_node_publish: failed to publish message");
        ipc_mutex_unlock(node->lock);
        return false;
    }

    node->last_activity = time(NULL);
    ipc_mutex_unlock(node->lock);

    LOG_DEBUG("ssn_node_publish: message published to topic %s", url->url);
    return true;
}

/**
 * @brief Subscribe to a topic
 */
bool ssn_node_subscribe(ssn_node_t *node, const char *peer_address,
                       const ssn_url_ref_t *url,
                       ssn_client_msg_handler_t callback, void *arg,
                       uint64_t timeout_ms)
{
    if (!node || !peer_address || !url || !callback) {
        LOG_ERROR("ssn_node_subscribe: invalid parameters");
        return false;
    }

    /* 锁内快照 + ref 保活，解锁后执行阻塞连接与订阅（消除锁序死锁） */
    ssn_client_t *client = NULL;
    ipc_mutex_lock(node->lock);
    if (!connect_to_peer(node, peer_address, &client)) {
        ipc_mutex_unlock(node->lock);
        return false;
    }
    ipc_mutex_unlock(node->lock);

    if (!ssn_client_is_connect(client)) {
        struct timespec timeout = { .tv_sec = 3, .tv_nsec = 0 };
        if (!ssn_client_connect(client, peer_address, &timeout)) {
            LOG_ERROR("ssn_node_subscribe: failed to connect to %s", peer_address);
            ssn_client_unref(client);
            return false;
        }
        ipc_mutex_lock(node->lock);
        snprintf(node->peer_address, sizeof(node->peer_address), "%s", peer_address);
        ipc_mutex_unlock(node->lock);
    }

    /* Subscribe to topic with per-URL handler */
    bool result = ssn_client_subscribe(client, url, callback, arg, timeout_ms);
    if (!result) {
        LOG_ERROR("ssn_node_subscribe: failed to subscribe to topic %s", url->url);
        ssn_client_unref(client);
        return false;
    }

    ipc_mutex_lock(node->lock);
    node->last_activity = time(NULL);
    ipc_mutex_unlock(node->lock);
    ssn_client_unref(client);

    LOG_DEBUG("ssn_node_subscribe: subscribed to topic %s", url->url);
    return true;
}

/**
 * @brief Unsubscribe from a topic
 */
bool ssn_node_unsubscribe(ssn_node_t *node, const ssn_url_ref_t *url,
                         uint64_t timeout_ms)
{
    if (!node || !url) {
        LOG_ERROR("ssn_node_unsubscribe: invalid parameters");
        return false;
    }

    ipc_mutex_lock(node->lock);

    if (!node->client) {
        LOG_ERROR("ssn_node_unsubscribe: client not available");
        ipc_mutex_unlock(node->lock);
        return false;
    }

    // Unsubscribe from topic
    bool result = ssn_client_unsubscribe(node->client, url, timeout_ms);
    if (!result) {
        LOG_ERROR("ssn_node_unsubscribe: failed to unsubscribe from topic %s", url->url);
        ipc_mutex_unlock(node->lock);
        return false;
    }

    node->last_activity = time(NULL);
    ipc_mutex_unlock(node->lock);

    LOG_DEBUG("ssn_node_unsubscribe: unsubscribed from topic %s", url->url);
    return true;
}

/**
 * @brief Make RPC call to a peer
 */
int ssn_node_rpc_call(ssn_node_t *node, const char *peer_address,
                     const ssn_url_ref_t *url, const ssn_data_ref_t *data,
                     ssn_client_rpcreply_handler_t callback, void *arg,
                     uint64_t timeout_ms)
{
    if (!node || !peer_address || !url) {
        LOG_ERROR("ssn_node_rpc_call: invalid parameters");
        return -1;
    }

    /* 锁内快照 + ref 保活，解锁后执行阻塞连接与 RPC（消除锁序死锁） */
    ssn_client_t *client = NULL;
    ipc_mutex_lock(node->lock);
    if (!connect_to_peer(node, peer_address, &client)) {
        ipc_mutex_unlock(node->lock);
        return -1;
    }
    ipc_mutex_unlock(node->lock);

    if (!ssn_client_is_connect(client)) {
        struct timespec timeout = { .tv_sec = 3, .tv_nsec = 0 };
        if (!ssn_client_connect(client, peer_address, &timeout)) {
            LOG_ERROR("ssn_node_rpc_call: failed to connect to %s", peer_address);
            ssn_client_unref(client);
            return -1;
        }
        ipc_mutex_lock(node->lock);
        snprintf(node->peer_address, sizeof(node->peer_address), "%s", peer_address);
        ipc_mutex_unlock(node->lock);
    }

    /* Make RPC call */
    int result = ssn_client_call(client, url, data, callback, arg, timeout_ms);
    if (result < 0) {
        LOG_ERROR("ssn_node_rpc_call: failed to make RPC call to %s", peer_address);
        ssn_client_unref(client);
        return -1;
    }

    ipc_mutex_lock(node->lock);
    node->last_activity = time(NULL);
    ipc_mutex_unlock(node->lock);
    ssn_client_unref(client);

    LOG_DEBUG("ssn_node_rpc_call: RPC call made to %s, method %s", 
             peer_address, url->url);
    return 0;
}
