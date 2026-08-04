/*
 * node_basic.c - Node abstraction layer basic example
 *
 * This example demonstrates the basic functionality of the node abstraction layer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "node/ssn_node.h"
#include "util/ssn_log.h"

/**
 * @brief Test node creation and destruction
 */
static bool test_node_creation(void)
{
    LOG_INFO("Node creation and destruction test");

    // Create node configuration
    ssn_node_config_t config = {
        .node_type = "test",
        .node_name = "basic-node",
        .listen_address = "127.0.0.1",
        .listen_port = 8888,
        .capabilities = SSN_NODE_CAP_RPC | SSN_NODE_CAP_PUBSUB |
                       SSN_NODE_CAP_SERVER | SSN_NODE_CAP_CLIENT
    };

    // Create node
    ssn_node_t *node = ssn_node_create(&config);
    if (!node) {
        LOG_ERROR("Failed to create node");
        return false;
    }

    LOG_INFO("Node created successfully: id=%s, type=%s, name=%s",
             node->node_id, node->node_type, node->node_name);

    // Check node state
    ssn_node_state_t state = ssn_node_get_state(node);
    LOG_INFO("Node state: %s",
             state == SSN_NODE_STATE_STOPPED ? "STOPPED" :
             state == SSN_NODE_STATE_ACTIVE ? "ACTIVE" : "ERROR");

    // Check node capabilities
    uint32_t capabilities = ssn_node_get_capabilities(node);
    LOG_INFO("Node capabilities: 0x%04x (%s%s%s%s)",
             capabilities,
             (capabilities & SSN_NODE_CAP_SERVER) ? "SERVER|" : "",
             (capabilities & SSN_NODE_CAP_CLIENT) ? "CLIENT|" : "",
             (capabilities & SSN_NODE_CAP_RPC) ? "RPC|" : "",
             (capabilities & SSN_NODE_CAP_PUBSUB) ? "PUBSUB" : "");

    // Start node
    if (!ssn_node_start(node)) {
        LOG_ERROR("Failed to start node");
        ssn_node_destroy(node);
        return false;
    }

    LOG_INFO("Node started successfully");

    // Check node state after start
    state = ssn_node_get_state(node);
    LOG_INFO("Node state: %s",
             state == SSN_NODE_STATE_STOPPED ? "STOPPED" :
             state == SSN_NODE_STATE_ACTIVE ? "ACTIVE" : "ERROR");

    // Get node statistics
    int active_connections = -1;
    uint64_t total_messages = 0;
    if (ssn_node_get_stats(node, &active_connections, &total_messages)) {
        LOG_INFO("Node statistics: active_connections=%d, total_messages=%llu",
                 active_connections, total_messages);
    }

    // Stop node
    if (!ssn_node_stop(node)) {
        LOG_ERROR("Failed to stop node");
        ssn_node_destroy(node);
        return false;
    }

    LOG_INFO("Node stopped successfully");

    // Check node state after stop
    state = ssn_node_get_state(node);
    LOG_INFO("Node state: %s",
             state == SSN_NODE_STATE_STOPPED ? "STOPPED" :
             state == SSN_NODE_STATE_ACTIVE ? "ACTIVE" : "ERROR");

    // Destroy node
    ssn_node_destroy(node);
    LOG_INFO("Node destroyed successfully");

    return true;
}

/**
 * @brief Test node with minimal configuration
 */
static bool test_minimal_config(void)
{
    LOG_INFO("\nMinimal configuration test");

    // Create node with minimal configuration
    ssn_node_config_t config = {
        .node_type = "minimal",
        .node_name = "minimal-node"
        // Other fields will use defaults
    };

    // Create node
    ssn_node_t *node = ssn_node_create(&config);
    if (!node) {
        LOG_ERROR("Failed to create node with minimal configuration");
        return false;
    }

    LOG_INFO("Node created with minimal configuration: id=%s", node->node_id);

    // Destroy node
    ssn_node_destroy(node);
    LOG_INFO("Minimal configuration node destroyed successfully");

    return true;
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Starting node basic example...");

    // Run tests
    bool test1 = test_node_creation();
    bool test2 = test_minimal_config();

    if (test1 && test2) {
        LOG_INFO("\nAll tests passed!");
        return 0;
    } else {
        LOG_ERROR("\nSome tests failed!");
        return 1;
    }
}
