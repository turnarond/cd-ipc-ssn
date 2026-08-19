/*
 * ipc_node.c - Node abstraction layer implementation
 *
 * This file implements the node abstraction layer, providing a unified interface
 * for both client and server capabilities.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ssn_node.h"
#include "../util/ssn_log.h"
#include "../ssn_global.h"

/**
 * @brief Generate a unique node ID
 * 
 * @param node_type Node type
 * @param node_name Node name
 * @param[out] node_id Buffer to store node ID
 * @param id_size Size of node_id buffer
 * @return true on success, false on failure
 */
static bool generate_node_id(const char *node_type, const char *node_name,
                           char *node_id, size_t id_size)
{
    if (!node_type || !node_name || !node_id || id_size < 32) {
        return false;
    }

    char hostname[256] = "";
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strcpy(hostname, "localhost");
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);

    snprintf(node_id, id_size, "%s_%s_%s_%ld_%ld",
             node_type, node_name, hostname, tv.tv_sec, tv.tv_usec);

    return true;
}

/**
 * @brief Create server address string
 * 
 * @param config Node configuration
 * @param[out] address Buffer to store address
 * @param addr_size Size of address buffer
 * @return true on success, false on failure
 */
static bool create_server_address(const ssn_node_config_t *config,
                                char *address, size_t addr_size)
{
    if (!config || !address || addr_size == 0) {
        return false;
    }

    if (config->listen_port > 0) {
        // TCP address
        snprintf(address, addr_size, "tcp://%s:%u",
                 config->listen_address[0] ? config->listen_address : "0.0.0.0",
                 config->listen_port);
    } else {
        // Unix socket path
        const char *socket_path = config->listen_address[0] ?
            config->listen_address : "/tmp/ipc_node.sock";
        snprintf(address, addr_size, "unix://%s", socket_path);
    }

    return true;
}

/**
 * @brief Get server options from node config
 * 
 * @param config Node configuration
 * @param[out] options Server options
 */
static void get_server_options(const ssn_node_config_t *config,
                             server_options_t *options)
{
    if (!config || !options) {
        return;
    }

    options->send_timeout_ms = config->send_timeout_ms > 0 ? 
        config->send_timeout_ms : 5000;
    options->conn_timeout_ms = config->conn_timeout_ms > 0 ? 
        config->conn_timeout_ms : 3000;
    options->idle_timeout_sec = config->idle_timeout_sec > 0 ? 
        config->idle_timeout_sec : 60;
    options->ifname[0] = '\0';
}

/**
 * @brief Create a new node
 */
ssn_node_t *ssn_node_create(const ssn_node_config_t *config)
{
    if (!config) {
        LOG_ERROR("ssn_node_create: invalid config");
        return NULL;
    }

    // Allocate node memory
    ssn_node_t *node = (ssn_node_t *)calloc(1, sizeof(ssn_node_t));
    if (!node) {
        LOG_ERROR("ssn_node_create: failed to allocate memory");
        return NULL;
    }

    // Initialize mutex
    if (ipc_mutex_init(&node->lock)) {
        LOG_ERROR("ssn_node_create: failed to initialize mutex");
        free(node);
        return NULL;
    }

    // Copy configuration
    memcpy(&node->config, config, sizeof(ssn_node_config_t));

    // Generate node ID if not provided
    if (!config->node_id[0]) {
        if (!generate_node_id(config->node_type[0] ? config->node_type : "unknown",
                             config->node_name[0] ? config->node_name : "node",
                             node->node_id, sizeof(node->node_id))) {
            LOG_ERROR("ssn_node_create: failed to generate node ID");
            ipc_mutex_destroy(node->lock);
            free(node);
            return NULL;
        }
    } else {
        /* snprintf 保证 NUL 终止（strncpy 源≥sizeof-1 时不追加 NUL，后续 LOG 越界读） */
        snprintf(node->node_id, sizeof(node->node_id), "%s", config->node_id);
    }

    // Copy other identity information
    if (config->node_type[0]) {
        snprintf(node->node_type, sizeof(node->node_type), "%s", config->node_type);
    } else {
        strcpy(node->node_type, "unknown");
    }

    if (config->node_name[0]) {
        snprintf(node->node_name, sizeof(node->node_name), "%s", config->node_name);
    } else {
        strcpy(node->node_name, "node");
    }

    // Set capabilities
    node->capabilities = config->capabilities;
    if (node->capabilities == 0) {
        // Default capabilities
        node->capabilities = SSN_NODE_CAP_RPC | SSN_NODE_CAP_PUBSUB | 
                            SSN_NODE_CAP_SERVER | SSN_NODE_CAP_CLIENT;
    }

    // Initialize state
    node->state = SSN_NODE_STATE_STOPPED;
    node->ref_count = 1;
    node->start_time = 0;
    node->last_activity = 0;

    LOG_INFO("ssn_node_create: node created successfully, id=%s, type=%s, name=%s",
             node->node_id, node->node_type, node->node_name);

    return node;
}

/**
 * @brief Start the node
 */
bool ssn_node_start(ssn_node_t *node)
{
    if (!node) {
        LOG_ERROR("ssn_node_start: invalid node");
        return false;
    }

    ipc_mutex_lock(node->lock);

    if (node->state == SSN_NODE_STATE_ACTIVE) {
        LOG_WARN("ssn_node_start: node is already active");
        ipc_mutex_unlock(node->lock);
        return true;
    }

    if (node->state == SSN_NODE_STATE_ERROR) {
        LOG_ERROR("ssn_node_start: node is in error state");
        ipc_mutex_unlock(node->lock);
        return false;
    }

    // Start server if server capability is enabled
    if (node->capabilities & SSN_NODE_CAP_SERVER) {
        char server_address[512];
        if (!create_server_address(&node->config, server_address, sizeof(server_address))) {
            LOG_ERROR("ssn_node_start: failed to create server address");
            node->state = SSN_NODE_STATE_ERROR;
            ipc_mutex_unlock(node->lock);
            return false;
        }

        server_options_t options;
        get_server_options(&node->config, &options);

        node->server = ssn_server_create_with_options(server_address, &options);
        if (!node->server) {
            LOG_ERROR("ssn_node_start: failed to create server");
            node->state = SSN_NODE_STATE_ERROR;
            ipc_mutex_unlock(node->lock);
            return false;
        }

        if (!ssn_server_start(node->server)) {
            LOG_ERROR("ssn_node_start: failed to start server");
            ssn_server_destroy(node->server);
            node->server = NULL;
            node->state = SSN_NODE_STATE_ERROR;
            ipc_mutex_unlock(node->lock);
            return false;
        }

        LOG_INFO("ssn_node_start: server started on %s", server_address);
    }

    // Start client if client capability is enabled
    if (node->capabilities & SSN_NODE_CAP_CLIENT) {
        // Client will be created on demand when needed
        // This allows the node to act as a client to other nodes
    }

    // Update state
    node->state = SSN_NODE_STATE_ACTIVE;
    node->start_time = time(NULL);
    node->last_activity = node->start_time;

    LOG_INFO("ssn_node_start: node started successfully, id=%s", node->node_id);

    ipc_mutex_unlock(node->lock);
    return true;
}

/**
 * @brief Stop the node
 */
bool ssn_node_stop(ssn_node_t *node)
{
    if (!node) {
        LOG_ERROR("ssn_node_stop: invalid node");
        return false;
    }

    ipc_mutex_lock(node->lock);

    if (node->state == SSN_NODE_STATE_STOPPED) {
        LOG_WARN("ssn_node_stop: node is already stopped");
        ipc_mutex_unlock(node->lock);
        return true;
    }

    // Stop server
    if (node->server) {
        ssn_server_destroy(node->server);
        node->server = NULL;
        LOG_INFO("ssn_node_stop: server stopped");
    }

    // Stop client
    if (node->client) {
        ssn_client_close(node->client);
        node->client = NULL;
        LOG_INFO("ssn_node_stop: client stopped");
    }

    // Update state
    node->state = SSN_NODE_STATE_STOPPED;
    node->last_activity = time(NULL);

    LOG_INFO("ssn_node_stop: node stopped successfully, id=%s", node->node_id);

    ipc_mutex_unlock(node->lock);
    return true;
}

/**
 * @brief Destroy the node
 */
void ssn_node_destroy(ssn_node_t *node)
{
    if (!node) {
        return;
    }

    ipc_mutex_lock(node->lock);

    // Check reference count
    if (--node->ref_count > 0) {
        LOG_WARN("ssn_node_destroy: node has %d references, deferring destruction", 
                 node->ref_count);
        ipc_mutex_unlock(node->lock);
        return;
    }

    // Stop if still running. Do not hold the lock while calling
    // ssn_node_stop: it re-acquires node->lock, and this mutex is not
    // recursive, so holding the lock here would self-deadlock.
    if (node->state == SSN_NODE_STATE_ACTIVE) {
        ipc_mutex_unlock(node->lock);
        ssn_node_stop(node);
        ipc_mutex_lock(node->lock);
    }

    // Cleanup resources
    if (node->lock) {
        ipc_mutex_unlock(node->lock);
        ipc_mutex_destroy(node->lock);
    }

    LOG_INFO("ssn_node_destroy: node destroyed, id=%s", node->node_id);

    free(node);
}

/**
 * @brief Get node state
 */
ssn_node_state_t ssn_node_get_state(ssn_node_t *node)
{
    if (!node) {
        return SSN_NODE_STATE_ERROR;
    }

    ipc_mutex_lock(node->lock);
    ssn_node_state_t state = node->state;
    ipc_mutex_unlock(node->lock);

    return state;
}

/**
 * @brief Get node capabilities
 */
uint32_t ssn_node_get_capabilities(ssn_node_t *node)
{
    if (!node) {
        return 0;
    }

    ipc_mutex_lock(node->lock);
    uint32_t capabilities = node->capabilities;
    ipc_mutex_unlock(node->lock);

    return capabilities;
}

/**
 * @brief Get client handle from node
 */
ssn_client_t *ssn_node_get_client(ssn_node_t *node)
{
    if (!node) {
        return NULL;
    }

    // Create client if not exists and client capability is enabled
    if (!node->client && (node->capabilities & SSN_NODE_CAP_CLIENT)) {
        node->client = ssn_client_create();
        if (!node->client) {
            LOG_ERROR("ssn_node_get_client: failed to create client");
            return NULL;
        }
        LOG_INFO("ssn_node_get_client: client created");
    }

    return node->client;
}

/**
 * @brief Get server handle from node
 */
ssn_server_t *ssn_node_get_server(ssn_node_t *node)
{
    if (!node) {
        return NULL;
    }

    return node->server;
}

/**
 * @brief Set connection handler for the node
 */
void ssn_node_set_connect_handler(ssn_node_t *node, ssn_on_connect_t callback, void *arg)
{
    if (!node) {
        return;
    }

    ipc_mutex_lock(node->lock);
    if (node->server) {
        ssn_server_set_connect_handler(node->server, callback, arg);
    }
    ipc_mutex_unlock(node->lock);
}

/**
 * @brief Set message handler for the node
 */
void ssn_node_set_message_handler(ssn_node_t *node, ssn_server_msg_handler_t callback, void *arg)
{
    if (!node) {
        return;
    }

    ipc_mutex_lock(node->lock);
    if (node->server) {
        ssn_server_set_message_handler(node->server, callback, arg);
    }
    // Client uses different callback type, handled separately
    ipc_mutex_unlock(node->lock);
}

/**
 * @brief Set client message handler for the node
 */
void ssn_node_set_client_message_handler(ssn_node_t *node, ssn_client_msg_handler_t callback, void *arg)
{
    if (!node) {
        return;
    }

    ipc_mutex_lock(node->lock);
    if (node->client) {
        ssn_client_set_on_message(node->client, callback, arg);
    }
    ipc_mutex_unlock(node->lock);
}


/**
 * @brief Add RPC method to the node
 */
bool ssn_node_add_rpc_method(ssn_node_t *node, const ssn_url_ref_t *url, ssn_server_rpc_handler_t callback, void *arg)
{
    if (!node || !url || !callback) {
        return false;
    }

    ipc_mutex_lock(node->lock);
    if (!node->server) {
        ipc_mutex_unlock(node->lock);
        return false;
    }

    bool result = ssn_server_add_method(node->server, url, callback, arg);
    ipc_mutex_unlock(node->lock);

    return result;
}

/**
 * @brief Remove RPC method from the node
 */
void ssn_node_remove_rpc_method(ssn_node_t *node, const ssn_url_ref_t *url)
{
    if (!node || !url) {
        return;
    }

    ipc_mutex_lock(node->lock);
    if (node->server) {
        ssn_server_remove_method(node->server, url);
    }
    ipc_mutex_unlock(node->lock);
}

/**
 * @brief Poll for node events
 */
int ssn_node_poll(ssn_node_t *node, uint64_t timeout_ms)
{
    if (!node) {
        return -1;
    }

    /* 缺陷背景：原实现持 node->lock 调用 ssn_server_poll/ssn_client_poll，二者
     * 内部触发用户回调（onmsg/RPC handler/应答回调），回调内调用任何 node API
     * （publish/rpc_call/subscribe 等）都需取同一把非递归锁 → 自锁死锁。
     * 修复：锁内仅快照 server/client 指针（并对 client 引用计数保活），
     * 解锁后再调用 poll，回调在锁外执行。 */
    ssn_server_t *server = NULL;
    ssn_client_t *client = NULL;

    ipc_mutex_lock(node->lock);
    server = node->server;
    client = node->client;
    if (client) {
        ssn_client_ref(client);
    }
    ipc_mutex_unlock(node->lock);

    int result = 0;

    /* Poll server events */
    if (server) {
        result = ssn_server_poll(server, (int)timeout_ms);
    }

    /* Poll client events */
    if (client) {
        int client_result = ssn_client_poll(client, timeout_ms);
        if (client_result < 0) {
            result = client_result;
        }
        ssn_client_unref(client);
    }

    return result;
}

/**
 * @brief Run node event loop
 */
void ssn_node_run(ssn_node_t *node)
{
    if (!node) {
        return;
    }

    while (true) {
        if (ssn_node_poll(node, 100) < 0) {
            break;
        }
    }
}

/**
 * @brief Get node statistics
 */
bool ssn_node_get_stats(ssn_node_t *node, int *active_connections, uint64_t *total_messages)
{
    if (!node) {
        return false;
    }

    ipc_mutex_lock(node->lock);
    
    if (active_connections) {
        *active_connections = 0;
        if (node->server) {
            *active_connections = ssn_server_peer_count(node->server);
        }
    }
    
    if (total_messages) {
        *total_messages = 0;
        // TODO: Implement message counting
    }
    
    ipc_mutex_unlock(node->lock);
    return true;
}
