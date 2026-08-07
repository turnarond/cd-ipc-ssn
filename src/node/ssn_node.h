/*
 * ipc_node.h - Node abstraction layer for cd-ipc-ssn
 *
 * This file defines the core data structures and interfaces for the node abstraction layer,
 * which provides a unified interface for both client and server capabilities.
 */

#ifndef SSN_NODE_H
#define SSN_NODE_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "../ssn_export.h"
#include "../ssn_client.h"
#include "../ssn_server.h"
#include "../ssn_frame.h"
#include "../ssn_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup IPC_Node Node Abstraction
 * @{*/

/**
 * @name Node State
 * @{*/

/**
 * @brief Node state enumeration
 * 
 * Simplified state machine for node lifecycle management:
 * - STOPPED: Node is initialized but not running
 * - ACTIVE: Node is running and ready for communication
 * - ERROR: Node encountered an error
 */
typedef enum {
    SSN_NODE_STATE_STOPPED = 0,  /**< Node is stopped */
    SSN_NODE_STATE_ACTIVE,       /**< Node is active and running */
    SSN_NODE_STATE_ERROR         /**< Node encountered an error */
} ssn_node_state_t;

/** @}*/

/**
 * @name Node Capabilities
 * @{*/

/**
 * @brief Node capability bitmask
 * 
 * Each bit represents a capability that the node supports.
 */
typedef enum {
    SSN_NODE_CAP_RPC        = 0x0001,  /**< Supports RPC */
    SSN_NODE_CAP_PUBSUB     = 0x0002,  /**< Supports publish/subscribe */
    SSN_NODE_CAP_SERVER     = 0x0004,  /**< Supports server functionality */
    SSN_NODE_CAP_CLIENT     = 0x0008,  /**< Supports client functionality */
    SSN_NODE_CAP_DISCOVERY  = 0x0010,  /**< Supports node discovery */
    SSN_NODE_CAP_QOS        = 0x0020   /**< Supports QoS */
} ssn_node_capability_t;

/** @}*/

/**
 * @name Node Configuration
 * @{*/

/**
 * @brief Node configuration structure
 * 
 * Contains all configuration parameters for a node.
 */
typedef struct {
    // Basic configuration
    char node_id[64];                    /**< Node ID (auto-generated if empty) */
    char node_type[32];                  /**< Node type */
    char node_name[64];                  /**< Node name */
    
    // Network configuration
    char listen_address[256];            /**< Listen address */
    uint16_t listen_port;                /**< Listen port */
    
    // Capabilities
    uint32_t capabilities;               /**< Node capabilities (bitmask) */
    
    // Performance configuration
    uint32_t max_connections;            /**< Maximum connections */
    uint32_t send_buffer_size;           /**< Send buffer size */
    uint32_t recv_buffer_size;           /**< Receive buffer size */
    
    // Timeout configuration
    uint32_t send_timeout_ms;            /**< Send timeout */
    uint32_t conn_timeout_ms;            /**< Connection timeout */
    uint32_t idle_timeout_sec;           /**< Idle timeout */
} ssn_node_config_t;

/** @}*/

/**
 * @name Node Structure
 * @{*/

/**
 * @brief Node instance structure
 * 
 * Represents a node with both client and server capabilities.
 */
typedef struct ssn_node {
    // Identity
    char node_id[64];                    /**< Node ID */
    char node_type[32];                  /**< Node type */
    char node_name[64];                  /**< Node name */
    
    // State
    ssn_node_state_t state;              /**< Node state */
    int ref_count;                       /**< Reference count */
    time_t start_time;                   /**< Start time */
    time_t last_activity;                /**< Last activity time */
    
    // Capabilities
    uint32_t capabilities;               /**< Node capabilities */
    
    // Core components
    ssn_client_t *client;                /**< Client instance */
    ssn_server_t *server;                /**< Server instance */
    
    // Synchronization
    ipc_mutex_t *lock;                   /**< Node lock */
    
    // Configuration
    ssn_node_config_t config;            /**< Node configuration */
} ssn_node_t;

/** @}*/

/**
 * @name Node API
 * @{*/

/**
 * @brief Create a new node
 * 
 * @param config Node configuration
 * @return Node instance, or NULL on failure
 */
SSN_API ssn_node_t *ssn_node_create(const ssn_node_config_t *config);

/**
 * @brief Start the node
 * 
 * @param node Node instance
 * @return true on success, false on failure
 */
SSN_API bool ssn_node_start(ssn_node_t *node);

/**
 * @brief Stop the node
 * 
 * @param node Node instance
 * @return true on success, false on failure
 */
SSN_API bool ssn_node_stop(ssn_node_t *node);

/**
 * @brief Destroy the node
 * 
 * @param node Node instance
 */
SSN_API void ssn_node_destroy(ssn_node_t *node);

/**
 * @brief Get node state
 * 
 * @param node Node instance
 * @return Node state
 */
SSN_API ssn_node_state_t ssn_node_get_state(ssn_node_t *node);

/**
 * @brief Get node capabilities
 * 
 * @param node Node instance
 * @return Node capabilities bitmask
 */
SSN_API uint32_t ssn_node_get_capabilities(ssn_node_t *node);

/**
 * @brief Get client handle from node
 *
 * Caller must hold node->lock.
 *
 * @param node Node instance
 * @return Client instance, or NULL if not available
 */
ssn_client_t *ssn_node_get_client(ssn_node_t *node);

/**
 * @brief Get server handle from node
 *
 * Caller must hold node->lock.
 *
 * @param node Node instance
 * @return Server instance, or NULL if not available
 */
ssn_server_t *ssn_node_get_server(ssn_node_t *node);

/**
 * @brief Send message to a peer
 * 
 * @param node Node instance
 * @param peer_address Peer address
 * @param url URL reference
 * @param data Data reference
 * @return true on success, false on failure
 */
SSN_API bool ssn_node_send_to_peer(ssn_node_t *node, const char *peer_address,
                          const ssn_url_ref_t *url, const ssn_data_ref_t *data);

/**
 * @brief Publish message to all subscribers
 * 
 * @param node Node instance
 * @param url URL reference
 * @param data Data reference
 * @return true on success, false on failure
 */
SSN_API bool ssn_node_publish(ssn_node_t *node, const ssn_url_ref_t *url,
                     const ssn_data_ref_t *data);

/**
 * @brief Subscribe to a topic
 *
 * @param node Node instance
 * @param peer_address Peer address to connect to
 * @param url URL reference
 * @param callback Message handler callback
 * @param arg Callback argument
 * @param timeout_ms Timeout in milliseconds
 * @return true on success, false on failure
 */
SSN_API bool ssn_node_subscribe(ssn_node_t *node, const char *peer_address,
                       const ssn_url_ref_t *url,
                       ssn_client_msg_handler_t callback, void *arg,
                       uint64_t timeout_ms);

/**
 * @brief Unsubscribe from a topic
 *
 * @param node Node instance
 * @param url URL reference
 * @param timeout_ms Timeout in milliseconds
 * @return true on success, false on failure
 */
SSN_API bool ssn_node_unsubscribe(ssn_node_t *node, const ssn_url_ref_t *url,
                         uint64_t timeout_ms);

/**
 * @brief Make RPC call to a peer
 * 
 * @param node Node instance
 * @param peer_address Peer address
 * @param url URL reference
 * @param data Data reference
 * @param callback RPC reply handler
 * @param arg Callback argument
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_node_rpc_call(ssn_node_t *node, const char *peer_address,
                     const ssn_url_ref_t *url, const ssn_data_ref_t *data,
                     ssn_client_rpcreply_handler_t callback, void *arg,
                     uint64_t timeout_ms);

/**
 * @brief Add RPC method to the node
 * 
 * @param node Node instance
 * @param url URL reference
 * @param callback RPC handler
 * @param arg Callback argument
 * @return true on success, false on failure
 */
SSN_API bool ssn_node_add_rpc_method(ssn_node_t *node, const ssn_url_ref_t *url,
                            ssn_server_rpc_handler_t callback, void *arg);

/**
 * @brief Remove RPC method from the node
 * 
 * @param node Node instance
 * @param url URL reference
 */
SSN_API void ssn_node_remove_rpc_method(ssn_node_t *node, const ssn_url_ref_t *url);

/**
 * @brief Set connection handler for the node
 * 
 * @param node Node instance
 * @param callback Connection handler
 * @param arg Callback argument
 */
SSN_API void ssn_node_set_connect_handler(ssn_node_t *node,
                                 ssn_on_connect_t callback, void *arg);

/**
 * @brief Set message handler for the node
 * 
 * @param node Node instance
 * @param callback Message handler
 * @param arg Callback argument
 */
SSN_API void ssn_node_set_message_handler(ssn_node_t *node,
                                 ssn_server_msg_handler_t callback, void *arg);

/**
 * @brief Set client message handler for the node
 * 
 * @param node Node instance
 * @param callback Client message handler
 * @param arg Callback argument
 */
SSN_API void ssn_node_set_client_message_handler(ssn_node_t *node,
                                       ssn_client_msg_handler_t callback, void *arg);

/**
 * @brief Poll for node events
 * 
 * @param node Node instance
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on failure
 */
SSN_API int ssn_node_poll(ssn_node_t *node, uint64_t timeout_ms);

/**
 * @brief Run node event loop
 * 
 * @param node Node instance
 */
SSN_API void ssn_node_run(ssn_node_t *node);

/**
 * @brief Get node statistics
 * 
 * @param node Node instance
 * @param[out] active_connections Number of active connections
 * @param[out] total_messages Number of total messages
 * @return true on success, false on failure
 */
SSN_API bool ssn_node_get_stats(ssn_node_t *node, int *active_connections,
                       uint64_t *total_messages);

/** @}*/

/** @}*/

#ifdef __cplusplus
}
#endif

#endif /* SSN_NODE_H */
