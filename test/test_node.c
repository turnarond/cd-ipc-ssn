/*
 * test_node.c - Unit tests for node abstraction layer
 *
 * This file contains unit tests for the node abstraction layer, testing
 * node lifecycle management and communication functionality.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "../src/node/ssn_node.h"
#include "../src/util/ssn_log.h"

#define TEST_TIMEOUT 5000
#define TEST_SERVER_ADDRESS "127.0.0.1:8888"
#define TEST_PEER_ADDRESS "tcp://127.0.0.1:8888"

static bool g_test_success = false;
static int g_message_count = 0;
static int g_connection_count = 0;

/*
 * Background server poller - runs ssn_node_poll in a thread so the server
 * can accept connections and handle handshakes while the client connects.
 */
static volatile int g_server_running = 0;

static void *server_poll_thread(void *arg)
{
    ssn_node_t *node = (ssn_node_t *)arg;
    while (g_server_running) {
        ssn_node_poll(node, 100);
    }
    return NULL;
}

/**
 * @brief Test message handler
 */
static void test_message_handler(ssn_client_t *client, ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg)
{
    (void)client;
    (void)arg;
    
    if (url && data) {
        LOG_INFO("Test message received: topic=%s, data=%.*s", 
                 url->url, (int)data->length, (const char*)data->data);
        g_message_count++;
        if (strncmp((const char*)data->data, "test message", data->length) == 0) {
            g_test_success = true;
        }
    }
}

/**
 * @brief Test connection handler
 */
static void test_connect_handler(ssn_server_t *server, ssn_peer_id_t id, bool connect, void *arg)
{
    (void)server;
    (void)id;
    (void)arg;
    
    if (connect) {
        LOG_INFO("Test client connected");
        g_connection_count++;
    } else {
        LOG_INFO("Test client disconnected");
    }
}

/**
 * @brief Test RPC handler
 */
static void test_rpc_handler(ssn_server_t *server, ssn_peer_id_t id, ssn_header_t *ipc_hdr, 
                           ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg)
{
    (void)server;
    (void)ipc_hdr;
    (void)url;
    (void)arg;
    
    LOG_INFO("Test RPC request received: id=%u, data=%.*s", 
             id, (int)data->length, (const char*)data->data);
    
    // Send response
    ssn_data_ref_t response = {
        .data = "RPC response",
        .length = 13
    };
    
    ssn_server_response(server, id, 0, ssn_get_seqno(ipc_hdr), &response);
}

/**
 * @brief Test RPC reply handler
 */
static void test_rpc_reply_handler(ssn_client_t *client, ssn_header_t *ipc_hdr, 
                                 ssn_data_ref_t *data, void *arg)
{
    (void)client;
    (void)ipc_hdr;
    (void)arg;
    
    if (data) {
        LOG_INFO("Test RPC response received: %.*s", 
                 (int)data->length, (const char*)data->data);
        if (strncmp((const char*)data->data, "RPC response", data->length) == 0) {
            g_test_success = true;
        }
    }
}

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
 * @brief Test 3: Node publish/subscribe
 */
static bool test_node_pubsub(void)
{
    LOG_INFO("=== Test 3: Node publish/subscribe ===");
    
    // Create server node
    ssn_node_config_t server_config = {
        .node_type = "server",
        .node_name = "pubsub-server",
        .listen_address = "127.0.0.1",
        .listen_port = 8888,
        .capabilities = SSN_NODE_CAP_SERVER | SSN_NODE_CAP_PUBSUB
    };
    
    ssn_node_t *server_node = ssn_node_create(&server_config);
    if (!server_node) {
        LOG_ERROR("Failed to create server node");
        return false;
    }
    
    if (!ssn_node_start(server_node)) {
        LOG_ERROR("Failed to start server node");
        ssn_node_destroy(server_node);
        return false;
    }

    // Create client node
    ssn_node_config_t client_config = {
        .node_type = "client",
        .node_name = "pubsub-client",
        .capabilities = SSN_NODE_CAP_CLIENT | SSN_NODE_CAP_PUBSUB
    };

    ssn_node_t *client_node = ssn_node_create(&client_config);
    if (!client_node) {
        LOG_ERROR("Failed to create client node");
        ssn_node_stop(server_node);
        ssn_node_destroy(server_node);
        return false;
    }

    if (!ssn_node_start(client_node)) {
        LOG_ERROR("Failed to start client node");
        ssn_node_destroy(client_node);
        ssn_node_stop(server_node);
        ssn_node_destroy(server_node);
        return false;
    }

    // Subscribe to topic
    ssn_url_ref_t topic = {
        .url = "/test/topic",
        .url_len = 11
    };

    g_message_count = 0;
    g_test_success = false;

    // Run server poller temporarily for handshake + subscribe
    pthread_t server_tid;
    g_server_running = 1;
    pthread_create(&server_tid, NULL, server_poll_thread, server_node);

    bool subscribe_ok = ssn_node_subscribe(client_node, TEST_PEER_ADDRESS, &topic,
                                           test_message_handler, NULL, TEST_TIMEOUT);

    // Give server poller time to process the subscribe message
    usleep(200000);

    // Stop server poller before publish (publish is synchronous)
    g_server_running = 0;
    pthread_join(server_tid, NULL);

    if (!subscribe_ok) {
        LOG_ERROR("Failed to subscribe to topic");
        ssn_node_stop(client_node);
        ssn_node_destroy(client_node);
        ssn_node_stop(server_node);
        ssn_node_destroy(server_node);
        return false;
    }

    LOG_INFO("Subscribed to topic /test/topic");

    // Publish message (synchronous, no server poller needed)
    ssn_data_ref_t message = {
        .data = "test message",
        .length = 12
    };

    if (!ssn_node_publish(server_node, &topic, &message)) {
        LOG_ERROR("Failed to publish message");
        ssn_node_stop(client_node);
        ssn_node_destroy(client_node);
        ssn_node_stop(server_node);
        ssn_node_destroy(server_node);
        return false;
    }

    LOG_INFO("Published message to topic /test/topic");

    // Poll client for delivered message
    for (int i = 0; i < 10; i++) {
        ssn_node_poll(client_node, 100);
        if (g_message_count > 0) break;
        usleep(100000);
    }

    if (!g_test_success) {
        LOG_ERROR("Message not received");
        ssn_node_stop(client_node);
        ssn_node_destroy(client_node);
        ssn_node_stop(server_node);
        ssn_node_destroy(server_node);
        return false;
    }

    LOG_INFO("Message received successfully");

    // Cleanup
    ssn_node_stop(client_node);
    ssn_node_destroy(client_node);
    ssn_node_stop(server_node);
    ssn_node_destroy(server_node);

    return true;
}

/**
 * @brief Test 4: Node RPC
 */
static bool test_node_rpc(void)
{
    LOG_INFO("=== Test 4: Node RPC ===");
    
    // Create server node
    ssn_node_config_t server_config = {
        .node_type = "server",
        .node_name = "rpc-server",
        .listen_address = "127.0.0.1",
        .listen_port = 8888,
        .capabilities = SSN_NODE_CAP_SERVER | SSN_NODE_CAP_RPC
    };
    
    ssn_node_t *server_node = ssn_node_create(&server_config);
    if (!server_node) {
        LOG_ERROR("Failed to create server node");
        return false;
    }
    
    if (!ssn_node_start(server_node)) {
        LOG_ERROR("Failed to start server node");
        ssn_node_destroy(server_node);
        return false;
    }

    // Add RPC method
    ssn_url_ref_t method = {
        .url = "/test/rpc",
        .url_len = 9
    };

    if (!ssn_node_add_rpc_method(server_node, &method, test_rpc_handler, NULL)) {
        LOG_ERROR("Failed to add RPC method");
        ssn_node_stop(server_node);
        ssn_node_destroy(server_node);
        return false;
    }

    LOG_INFO("Added RPC method /test/rpc");

    // Start server poller in background thread
    pthread_t server_tid;
    g_server_running = 1;
    pthread_create(&server_tid, NULL, server_poll_thread, server_node);

    // Create client node
    ssn_node_config_t client_config = {
        .node_type = "client",
        .node_name = "rpc-client",
        .capabilities = SSN_NODE_CAP_CLIENT | SSN_NODE_CAP_RPC
    };

    ssn_node_t *client_node = ssn_node_create(&client_config);
    if (!client_node) {
        LOG_ERROR("Failed to create client node");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_stop(server_node);
        ssn_node_destroy(server_node);
        return false;
    }

    if (!ssn_node_start(client_node)) {
        LOG_ERROR("Failed to start client node");
        ssn_node_destroy(client_node);
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_stop(server_node);
        ssn_node_destroy(server_node);
        return false;
    }

    // Make RPC call
    g_test_success = false;

    ssn_data_ref_t request = {
        .data = "RPC request",
        .length = 12
    };

    int result = ssn_node_rpc_call(client_node, TEST_PEER_ADDRESS, &method, &request,
                                  test_rpc_reply_handler, NULL, TEST_TIMEOUT);

    // Stop server poller
    g_server_running = 0;
    pthread_join(server_tid, NULL);

    if (result < 0) {
        LOG_ERROR("Failed to make RPC call");
        ssn_node_stop(client_node);
        ssn_node_destroy(client_node);
        ssn_node_stop(server_node);
        ssn_node_destroy(server_node);
        return false;
    }

    LOG_INFO("Made RPC call to /test/rpc");

    // Wait for response
    sleep(1);

    // Poll for events
    for (int i = 0; i < 5; i++) {
        ssn_node_poll(client_node, 100);
        ssn_node_poll(server_node, 100);
        if (g_test_success) {
            break;
        }
        usleep(100000);
    }

    if (!g_test_success) {
        LOG_ERROR("RPC response not received");
        ssn_node_stop(client_node);
        ssn_node_destroy(client_node);
        ssn_node_stop(server_node);
        ssn_node_destroy(server_node);
        return false;
    }

    LOG_INFO("RPC response received successfully");

    // Cleanup
    ssn_node_stop(client_node);
    ssn_node_destroy(client_node);
    ssn_node_stop(server_node);
    ssn_node_destroy(server_node);

    return true;
}

/**
 * @brief Test 5: Node statistics
 */
static bool test_node_stats(void)
{
    LOG_INFO("=== Test 5: Node statistics ===");
    
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

/**
 * @brief Test 6: Destroy active node without explicit stop (deadlock regression)
 */
static bool test_node_destroy_active(void)
{
    LOG_INFO("=== Test 6: Destroy active node without stop (deadlock regression) ===");

    // Create node configuration
    ssn_node_config_t config = {
        .node_type = "test",
        .node_name = "destroy-active",
        .listen_address = "127.0.0.1",
        .listen_port = 8890,
        .capabilities = SSN_NODE_CAP_SERVER
    };

    // Create node
    ssn_node_t *node = ssn_node_create(&config);
    if (!node) {
        LOG_ERROR("Failed to create node");
        return false;
    }

    // Start node (server role)
    if (!ssn_node_start(node)) {
        LOG_ERROR("Failed to start node");
        ssn_node_destroy(node);
        return false;
    }

    // Destroy without stop: self-deadlock before fix (hang), must complete quickly
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    ssn_node_destroy(node);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    if (elapsed >= 5.0) {
        LOG_ERROR("Destroy of active node too slow (%.3f s), possible deadlock", elapsed);
        return false;
    }

    LOG_INFO("Destroyed active node successfully in %.3f s", elapsed);

    return true;
}

int main(void)
{
    LOG_INFO("Starting node abstraction layer tests...");

    // Set log level
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    bool tests[] = {
        test_node_creation(),
        test_node_start_stop(),
        test_node_pubsub(),
        test_node_rpc(),
        test_node_stats(),
        test_node_destroy_active()
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
