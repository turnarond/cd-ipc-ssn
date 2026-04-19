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

#include "ipc_node.h"
#include "../util/ssn_log.h"
#include "../cd_ipc_global.h"

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
static bool create_server_address(const ipc_node_config_t *config,
                                char *address, size_t addr_size)
{
    if (!config || !address || addr_size == 0) {
        return false;
    }

    if (config->listen_port > 0) {
        // TCP address
        snprintf(address, addr_size, "%s:%u",
                 config->listen_address[0] ? config->listen_address : "0.0.0.0",
                 config->listen_port);
    } else {
        // Unix socket path
        const char *socket_path = config->listen_address[0] ? 
            config->listen_address : "/tmp/ipc_node.sock";
        snprintf(address, addr_size, "%s", socket_path);
    }

    return true;
}

/**
 * @brief Get server options from node config
 * 
 * @param config Node configuration
 * @param[out] options Server options
 */
static void get_server_options(const ipc_node_config_t *config,
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
ipc_node_t *ipc_node_create(const ipc_node_config_t *config)
{
    if (!config) {
        LOG_ERROR("ipc_node_create: invalid config");
        return NULL;
    }

    // Allocate node memory
    ipc_node_t *node = (ipc_node_t *)calloc(1, sizeof(ipc_node_t));
    if (!node) {
        LOG_ERROR("ipc_node_create: failed to allocate memory");
        return NULL;
    }

    // Initialize mutex
    if (ipc_mutex_init(&node->lock)) {
        LOG_ERROR("ipc_node_create: failed to initialize mutex");
        free(node);
        return NULL;
    }

    // Copy configuration
    memcpy(&node->config, config, sizeof(ipc_node_config_t));

    // Generate node ID if not provided
    if (!config->node_id[0]) {
        if (!generate_node_id(config->node_type[0] ? config->node_type : "unknown",
                             config->node_name[0] ? config->node_name : "node",
                             node->node_id, sizeof(node->node_id))) {
            LOG_ERROR("ipc_node_create: failed to generate node ID");
            ipc_mutex_destroy(node->lock);
            free(node);
            return NULL;
        }
    } else {
        strncpy(node->node_id, config->node_id, sizeof(node->node_id) - 1);
    }

    // Copy other identity information
    if (config->node_type[0]) {
        strncpy(node->node_type, config->node_type, sizeof(node->node_type) - 1);
    } else {
        strcpy(node->node_type, "unknown");
    }

    if (config->node_name[0]) {
        strncpy(node->node_name, config->node_name, sizeof(node->node_name) - 1);
    } else {
        strcpy(node->node_name, "node");
    }

    // Set capabilities
    node->capabilities = config->capabilities;
    if (node->capabilities == 0) {
        // Default capabilities
        node->capabilities = IPC_NODE_CAP_RPC | IPC_NODE_CAP_PUBSUB | 
                            IPC_NODE_CAP_SERVER | IPC_NODE_CAP_CLIENT;
    }

    // Initialize state
    node->state = IPC_NODE_STATE_STOPPED;
    node->ref_count = 1;
    node->start_time = 0;
    node->last_activity = 0;

    LOG_INFO("ipc_node_create: node created successfully, id=%s, type=%s, name=%s",
             node->node_id, node->node_type, node->node_name);

    return node;
}

/**
 * @brief Start the node
 */
bool ipc_node_start(ipc_node_t *node)
{
    if (!node) {
        LOG_ERROR("ipc_node_start: invalid node");
        return false;
    }

    ipc_mutex_lock(node->lock);

    if (node->state == IPC_NODE_STATE_ACTIVE) {
        LOG_WARN("ipc_node_start: node is already active");
        ipc_mutex_unlock(node->lock);
        return true;
    }

    if (node->state == IPC_NODE_STATE_ERROR) {
        LOG_ERROR("ipc_node_start: node is in error state");
        ipc_mutex_unlock(node->lock);
        return false;
    }

    // Start server if server capability is enabled
    if (node->capabilities & IPC_NODE_CAP_SERVER) {
        char server_address[512];
        if (!create_server_address(&node->config, server_address, sizeof(server_address))) {
            LOG_ERROR("ipc_node_start: failed to create server address");
            node->state = IPC_NODE_STATE_ERROR;
            ipc_mutex_unlock(node->lock);
            return false;
        }

        server_options_t options;
        get_server_options(&node->config, &options);

        node->server = ipc_server_create_with_options(server_address, &options);
        if (!node->server) {
            LOG_ERROR("ipc_node_start: failed to create server");
            node->state = IPC_NODE_STATE_ERROR;
            ipc_mutex_unlock(node->lock);
            return false;
        }

        if (!ipc_server_start(node->server)) {
            LOG_ERROR("ipc_node_start: failed to start server");
            ipc_server_destroy(node->server);
            node->server = NULL;
            node->state = IPC_NODE_STATE_ERROR;
            ipc_mutex_unlock(node->lock);
            return false;
        }

        LOG_INFO("ipc_node_start: server started on %s", server_address);
    }

    // Start client if client capability is enabled
    if (node->capabilities & IPC_NODE_CAP_CLIENT) {
        // Client will be created on demand when needed
        // This allows the node to act as a client to other nodes
    }

    // Update state
    node->state = IPC_NODE_STATE_ACTIVE;
    node->start_time = time(NULL);
    node->last_activity = node->start_time;

    LOG_INFO("ipc_node_start: node started successfully, id=%s", node->node_id);

    ipc_mutex_unlock(node->lock);
    return true;
}

/**
 * @brief Stop the node
 */
bool ipc_node_stop(ipc_node_t *node)
{
    if (!node) {
        LOG_ERROR("ipc_node_stop: invalid node");
        return false;
    }

    ipc_mutex_lock(node->lock);

    if (node->state == IPC_NODE_STATE_STOPPED) {
        LOG_WARN("ipc_node_stop: node is already stopped");
        ipc_mutex_unlock(node->lock);
        return true;
    }

    // Stop server
    if (node->server) {
        ipc_server_destroy(node->server);
        node->server = NULL;
        LOG_INFO("ipc_node_stop: server stopped");
    }

    // Stop client
    if (node->client) {
        ipc_client_close(node->client);
        node->client = NULL;
        LOG_INFO("ipc_node_stop: client stopped");
    }

    // Update state
    node->state = IPC_NODE_STATE_STOPPED;
    node->last_activity = time(NULL);

    LOG_INFO("ipc_node_stop: node stopped successfully, id=%s", node->node_id);

    ipc_mutex_unlock(node->lock);
    return true;
}

/**
 * @brief Destroy the node
 */
void ipc_node_destroy(ipc_node_t *node)
{
    if (!node) {
        return;
    }

    ipc_mutex_lock(node->lock);

    // Check reference count
    if (--node->ref_count > 0) {
        LOG_WARN("ipc_node_destroy: node has %d references, deferring destruction", 
                 node->ref_count);
        ipc_mutex_unlock(node->lock);
        return;
    }

    // Stop if still running
    if (node->state == IPC_NODE_STATE_ACTIVE) {
        ipc_node_stop(node);
    }

    // Cleanup resources
    if (node->lock) {
        ipc_mutex_unlock(node->lock);
        ipc_mutex_destroy(node->lock);
    }

    LOG_INFO("ipc_node_destroy: node destroyed, id=%s", node->node_id);

    free(node);
}

/**
 * @brief Get node state
 */
ipc_node_state_t ipc_node_get_state(ipc_node_t *node)
{
    if (!node) {
        return IPC_NODE_STATE_ERROR;
    }

    ipc_mutex_lock(node->lock);
    ipc_node_state_t state = node->state;
    ipc_mutex_unlock(node->lock);

    return state;
}

/**
 * @brief Get node capabilities
 */
uint32_t ipc_node_get_capabilities(ipc_node_t *node)
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
ipc_client_t *ipc_node_get_client(ipc_node_t *node)
{
    if (!node) {
        return NULL;
    }

    ipc_mutex_lock(node->lock);

    // Create client if not exists and client capability is enabled
    if (!node->client && (node->capabilities & IPC_NODE_CAP_CLIENT)) {
        // Create client with default message handler
        node->client = ipc_client_create(NULL, NULL);
        if (!node->client) {
            LOG_ERROR("ipc_node_get_client: failed to create client");
            ipc_mutex_unlock(node->lock);
            return NULL;
        }
        LOG_INFO("ipc_node_get_client: client created");
    }

    ipc_client_t *client = node->client;
    ipc_mutex_unlock(node->lock);

    return client;
}

/**
 * @brief Get server handle from node
 */
ipc_server_t *ipc_node_get_server(ipc_node_t *node)
{
    if (!node) {
        return NULL;
    }

    ipc_mutex_lock(node->lock);
    ipc_server_t *server = node->server;
    ipc_mutex_unlock(node->lock);

    return server;
}

/**
 * @brief Set connection handler for the node
 */
void ipc_node_set_connect_handler(ipc_node_t *node, ipc_on_connect_t callback, void *arg)
{
    if (!node) {
        return;
    }

    ipc_mutex_lock(node->lock);
    if (node->server) {
        ipc_server_set_connect_handler(node->server, callback, arg);
    }
    ipc_mutex_unlock(node->lock);
}

/**
 * @brief Set message handler for the node
 */
void ipc_node_set_message_handler(ipc_node_t *node, ipc_server_msg_handler_t callback, void *arg)
{
    if (!node) {
        return;
    }

    ipc_mutex_lock(node->lock);
    if (node->server) {
        ipc_server_set_message_handler(node->server, callback, arg);
    }
    // Client uses different callback type, handled separately
    ipc_mutex_unlock(node->lock);
}

/**
 * @brief Set client message handler for the node
 */
void ipc_node_set_client_message_handler(ipc_node_t *node, ipc_client_msg_handler_t callback, void *arg)
{
    if (!node) {
        return;
    }

    ipc_mutex_lock(node->lock);
    if (node->client) {
        ipc_client_set_on_message(node->client, callback, arg);
    }
    ipc_mutex_unlock(node->lock);
}

/**
 * @brief Add RPC method to the node
 */
bool ipc_node_add_rpc_method(ipc_node_t *node, const ipc_url_ref_t *url, ipc_server_rpc_handler_t callback, void *arg)
{
    if (!node || !url || !callback) {
        return false;
    }

    ipc_mutex_lock(node->lock);
    if (!node->server) {
        ipc_mutex_unlock(node->lock);
        return false;
    }

    bool result = ipc_server_add_method(node->server, url, callback, arg);
    ipc_mutex_unlock(node->lock);

    return result;
}

/**
 * @brief Remove RPC method from the node
 */
void ipc_node_remove_rpc_method(ipc_node_t *node, const ipc_url_ref_t *url)
{
    if (!node || !url) {
        return;
    }

    ipc_mutex_lock(node->lock);
    if (node->server) {
        ipc_server_remove_method(node->server, url);
    }
    ipc_mutex_unlock(node->lock);
}

/**
 * @brief Poll for node events
 */
int ipc_node_poll(ipc_node_t *node, uint64_t timeout_ms)
{
    if (!node) {
        return -1;
    }

    ipc_mutex_lock(node->lock);
    
    int result = 0;
    
    // Poll server events
    if (node->server) {
        result = ipc_server_poll(node->server, timeout_ms);
    }
    
    // Poll client events
    if (node->client) {
        int client_result = ipc_client_poll(node->client, timeout_ms);
        if (client_result < 0) {
            result = client_result;
        }
    }
    
    ipc_mutex_unlock(node->lock);
    return result;
}

/**
 * @brief Run node event loop
 */
void ipc_node_run(ipc_node_t *node)
{
    if (!node) {
        return;
    }

    while (true) {
        if (ipc_node_poll(node, 100) < 0) {
            break;
        }
    }
}

/**
 * @brief Get node statistics
 */
bool ipc_node_get_stats(ipc_node_t *node, int *active_connections, uint64_t *total_messages)
{
    if (!node) {
        return false;
    }

    ipc_mutex_lock(node->lock);
    
    if (active_connections) {
        *active_connections = 0;
        if (node->server) {
            *active_connections = ipc_server_peer_count(node->server);
        }
    }
    
    if (total_messages) {
        *total_messages = 0;
        // TODO: Implement message counting
    }
    
    ipc_mutex_unlock(node->lock);
    return true;
}
