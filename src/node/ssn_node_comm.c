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

/**
 * @brief Connect client to peer
 * 
 * @param node Node instance
 * @param peer_address Peer address
 * @return true on success, false on failure
 */
static bool connect_to_peer(ssn_node_t *node, const char *peer_address)
{
    if (!node || !peer_address) {
        return false;
    }

    // Get or create client
    ssn_client_t *client = ssn_node_get_client(node);
    if (!client) {
        return false;
    }

    // Check if already connected
    if (ssn_client_is_connect(client)) {
        return true;
    }

    // Connect to peer
    struct timespec timeout = {
        .tv_sec = 3,
        .tv_nsec = 0
    };

    if (!ssn_client_connect(client, peer_address, &timeout)) {
        LOG_ERROR("connect_to_peer: failed to connect to %s", peer_address);
        return false;
    }

    LOG_INFO("connect_to_peer: connected to %s", peer_address);
    return true;
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

    ipc_mutex_lock(node->lock);

    // Connect to peer if not already connected
    if (!connect_to_peer(node, peer_address)) {
        ipc_mutex_unlock(node->lock);
        return false;
    }

    // Send message
    int result = ssn_client_message(node->client, url, data);
    if (result < 0) {
        LOG_ERROR("ssn_node_send_to_peer: failed to send message");
        ipc_mutex_unlock(node->lock);
        return false;
    }

    node->last_activity = time(NULL);
    ipc_mutex_unlock(node->lock);

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

    ipc_mutex_lock(node->lock);

    // Connect to peer if not already connected
    if (!connect_to_peer(node, peer_address)) {
        ipc_mutex_unlock(node->lock);
        return false;
    }

    // Set message handler if provided
    if (callback) {
        ssn_client_set_on_message(node->client, callback, arg);
    }

    // Subscribe to topic
    bool result = ssn_client_subscribe(node->client, url, NULL, NULL, timeout_ms);
    if (!result) {
        LOG_ERROR("ssn_node_subscribe: failed to subscribe to topic %s", url->url);
        ipc_mutex_unlock(node->lock);
        return false;
    }

    node->last_activity = time(NULL);
    ipc_mutex_unlock(node->lock);

    LOG_DEBUG("ssn_node_subscribe: subscribed to topic %s", url->url);
    return true;
}

/**
 * @brief Unsubscribe from a topic
 */
bool ssn_node_unsubscribe(ssn_node_t *node, const ssn_url_ref_t *url,
                         ssn_client_result_handler_t callback, void *arg,
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
    bool result = ssn_client_unsubscribe(node->client, url, callback, arg, timeout_ms);
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

    ipc_mutex_lock(node->lock);

    // Connect to peer if not already connected
    if (!connect_to_peer(node, peer_address)) {
        ipc_mutex_unlock(node->lock);
        return -1;
    }

    // Make RPC call
    int result = ssn_client_call(node->client, url, data, callback, arg, timeout_ms);
    if (result < 0) {
        LOG_ERROR("ssn_node_rpc_call: failed to make RPC call to %s", peer_address);
        ipc_mutex_unlock(node->lock);
        return -1;
    }

    node->last_activity = time(NULL);
    ipc_mutex_unlock(node->lock);

    LOG_DEBUG("ssn_node_rpc_call: RPC call made to %s, method %s", 
             peer_address, url->url);
    return 0;
}
