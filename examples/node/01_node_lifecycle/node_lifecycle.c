/*
 * node_lifecycle.c - Node lifecycle management example
 *
 * This example demonstrates the lifecycle management of nodes in the node abstraction layer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "node/ssn_node.h"
#include "util/ssn_log.h"

/**
 * @brief Test basic node lifecycle
 */
static bool test_basic_lifecycle(void)
{
    LOG_INFO("Test 1: Basic node lifecycle");

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
 * @brief Test minimal configuration
 */
static bool test_minimal_config(void)
{
    LOG_INFO("\nTest 2: Minimal configuration");

    // Create node with minimal configuration
    ssn_node_config_t config = {
        .node_type = "minimal",
        .node_name = "minimal"
        // Other fields will use defaults
    };

    // Create node
    ssn_node_t *node = ssn_node_create(&config);
    if (!node) {
        LOG_ERROR("Failed to create node with minimal configuration");
        return false;
    }

    LOG_INFO("Node created with minimal config: id=%s, type=%s, name=%s", 
             node->node_id, node->node_type, node->node_name);

    // Check default capabilities
    uint32_t capabilities = ssn_node_get_capabilities(node);
    LOG_INFO("Default capabilities: 0x%04x", capabilities);

    // Destroy node
    ssn_node_destroy(node);
    LOG_INFO("Minimal config node destroyed successfully");

    return true;
}

/**
 * @brief Test server-only node
 */
static bool test_server_only(void)
{
    LOG_INFO("\nTest 3: Server-only node");

    // Create server-only node
    ssn_node_config_t config = {
        .node_type = "server",
        .node_name = "server-only",
        .listen_address = "127.0.0.1",
        .listen_port = 8889,
        .capabilities = SSN_NODE_CAP_SERVER | SSN_NODE_CAP_RPC
    };

    // Create node
    ssn_node_t *node = ssn_node_create(&config);
    if (!node) {
        LOG_ERROR("Failed to create server-only node");
        return false;
    }

    LOG_INFO("Server-only node created: id=%s, type=%s, name=%s", 
             node->node_id, node->node_type, node->node_name);

    // Start node
    if (!ssn_node_start(node)) {
        LOG_ERROR("Failed to start server-only node");
        ssn_node_destroy(node);
        return false;
    }

    LOG_INFO("Server-only node started successfully");

    // Stop node
    if (!ssn_node_stop(node)) {
        LOG_ERROR("Failed to stop server-only node");
        ssn_node_destroy(node);
        return false;
    }

    // Destroy node
    ssn_node_destroy(node);
    LOG_INFO("Server-only node destroyed successfully");

    return true;
}

/**
 * @brief Test client-only node
 */
static bool test_client_only(void)
{
    LOG_INFO("\nTest 4: Client-only node");

    // Create client-only node
    ssn_node_config_t config = {
        .node_type = "client",
        .node_name = "client-only",
        .capabilities = SSN_NODE_CAP_CLIENT | SSN_NODE_CAP_RPC
    };

    // Create node
    ssn_node_t *node = ssn_node_create(&config);
    if (!node) {
        LOG_ERROR("Failed to create client-only node");
        return false;
    }

    LOG_INFO("Client-only node created: id=%s, type=%s, name=%s", 
             node->node_id, node->node_type, node->node_name);

    // Destroy node
    ssn_node_destroy(node);
    LOG_INFO("Client-only node destroyed successfully");

    return true;
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Node lifecycle example started");

    // Run tests
    bool test1 = test_basic_lifecycle();
    bool test2 = test_minimal_config();
    bool test3 = test_server_only();
    bool test4 = test_client_only();

    if (test1 && test2 && test3 && test4) {
        LOG_INFO("\nAll lifecycle tests completed successfully!");
        return 0;
    } else {
        LOG_ERROR("\nSome lifecycle tests failed!");
        return 1;
    }
}
