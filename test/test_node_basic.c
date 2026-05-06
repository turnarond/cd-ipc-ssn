/*
 * test_node_basic.c - Basic node abstraction layer tests
 *
 * This file contains basic tests for the node abstraction layer,
 * testing node lifecycle management without complex communication.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/node/ssn_node.h"
#include "../src/util/ssn_log.h"

/**
 * @brief Test 1: Node creation and destruction
 */
static bool test_node_creation(void)
{
    LOG_INFO("=== Test 1: Node creation and destruction ===");
    
    // Create node configuration
    ssn_node_config_t config = {
        .node_type = "test",
        .node_name = "test-node",
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
    if (ssn_node_get_state(node) != SSN_NODE_STATE_STOPPED) {
        LOG_ERROR("Node state is not STOPPED");
        ssn_node_destroy(node);
        return false;
    }
    
    // Check node capabilities
    uint32_t capabilities = ssn_node_get_capabilities(node);
    if (!(capabilities & SSN_NODE_CAP_SERVER) || 
        !(capabilities & SSN_NODE_CAP_CLIENT)) {
        LOG_ERROR("Node capabilities incorrect");
        ssn_node_destroy(node);
        return false;
    }
    
    // Destroy node
    ssn_node_destroy(node);
    LOG_INFO("Node destroyed successfully");
    
    return true;
}

/**
 * @brief Test 2: Node start and stop
 */
static bool test_node_start_stop(void)
{
    LOG_INFO("=== Test 2: Node start and stop ===");
    
    // Create node configuration
    ssn_node_config_t config = {
        .node_type = "test",
        .node_name = "test-node",
        .listen_address = "127.0.0.1",
        .listen_port = 8888,
        .capabilities = SSN_NODE_CAP_SERVER
    };
    
    // Create node
    ssn_node_t *node = ssn_node_create(&config);
    if (!node) {
        LOG_ERROR("Failed to create node");
        return false;
    }
    
    // Start node
    if (!ssn_node_start(node)) {
        LOG_ERROR("Failed to start node");
        ssn_node_destroy(node);
        return false;
    }
    
    LOG_INFO("Node started successfully");
    
    // Check node state
    if (ssn_node_get_state(node) != SSN_NODE_STATE_ACTIVE) {
        LOG_ERROR("Node state is not ACTIVE");
        ssn_node_stop(node);
        ssn_node_destroy(node);
        return false;
    }
    
    // Stop node
    if (!ssn_node_stop(node)) {
        LOG_ERROR("Failed to stop node");
        ssn_node_destroy(node);
        return false;
    }
    
    LOG_INFO("Node stopped successfully");
    
    // Check node state
    if (ssn_node_get_state(node) != SSN_NODE_STATE_STOPPED) {
        LOG_ERROR("Node state is not STOPPED");
        ssn_node_destroy(node);
        return false;
    }
    
    // Destroy node
    ssn_node_destroy(node);
    LOG_INFO("Node destroyed successfully");
    
    return true;
}

/**
 * @brief Test 3: Node statistics
 */
static bool test_node_stats(void)
{
    LOG_INFO("=== Test 3: Node statistics ===");
    
    // Create node
    ssn_node_config_t config = {
        .node_type = "test",
        .node_name = "stats-node",
        .listen_address = "127.0.0.1",
        .listen_port = 8888,
        .capabilities = SSN_NODE_CAP_SERVER
    };
    
    ssn_node_t *node = ssn_node_create(&config);
    if (!node) {
        LOG_ERROR("Failed to create node");
        return false;
    }
    
    if (!ssn_node_start(node)) {
        LOG_ERROR("Failed to start node");
        ssn_node_destroy(node);
        return false;
    }
    
    // Get statistics
    int active_connections = -1;
    uint64_t total_messages = 0;
    
    if (!ssn_node_get_stats(node, &active_connections, &total_messages)) {
        LOG_ERROR("Failed to get node statistics");
        ssn_node_stop(node);
        ssn_node_destroy(node);
        return false;
    }
    
    LOG_INFO("Node statistics: active_connections=%d, total_messages=%llu", 
             active_connections, total_messages);
    
    // Cleanup
    ssn_node_stop(node);
    ssn_node_destroy(node);
    
    return true;
}

int main(void)
{
    LOG_INFO("Starting basic node abstraction layer tests...");
    
    // Set log level
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);
    
    bool tests[] = {
        test_node_creation(),
        test_node_start_stop(),
        test_node_stats()
    };
    
    int test_count = sizeof(tests) / sizeof(tests[0]);
    int passed = 0;
    
    for (int i = 0; i < test_count; i++) {
        if (tests[i]) {
            passed++;
            LOG_INFO("Test %d: PASSED", i + 1);
        } else {
            LOG_ERROR("Test %d: FAILED", i + 1);
        }
    }
    
    LOG_INFO("Test results: %d/%d passed", passed, test_count);
    
    if (passed == test_count) {
        LOG_INFO("All tests passed! Node abstraction layer is working correctly.");
        return 0;
    } else {
        LOG_ERROR("Some tests failed. Please check the implementation.");
        return 1;
    }
}
