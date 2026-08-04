/*
 * node_comm.c - Node communication example
 *
 * This example demonstrates how nodes can communicate with each other using the node abstraction layer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "node/ssn_node.h"
#include "util/ssn_log.h"

#define NODE_A_ADDRESS "tcp://127.0.0.1:8890"

static bool g_message_received = false;
static bool g_reply_received = false;

static volatile int g_server_running = 0;

/**
 * @brief Background server poller
 *
 * Runs ssn_node_poll in a thread so the server node can accept connections
 * and process messages while the client connects.
 */
static void *server_poll_thread(void *arg)
{
    ssn_node_t *node = (ssn_node_t *)arg;
    while (g_server_running) {
        ssn_node_poll(node, 100);
    }
    return NULL;
}

/**
 * @brief Node A message handler
 *
 * @param server IPC server instance
 * @param id Client ID
 * @param url URL reference
 * @param data Data reference
 * @param arg User argument
 */
static void node_a_message_handler(ssn_server_t *server, ssn_peer_id_t id,
                                   ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg)
{
    (void)url;
    (void)arg;

    LOG_INFO("Node A received message: %.*s",
             (int)data->length, (const char*)data->data);

    g_message_received = true;

    // Send reply back to Node B
    ssn_data_ref_t reply_data = {
        .data = "Hello from Node A!",
        .length = 17
    };

    ssn_url_ref_t reply_url = {
        .url = "/reply",
        .url_len = 6
    };

    // Send the reply directly via the server (the node API must not be
    // re-entered from the callback context)
    ssn_server_message(server, id, &reply_url, &reply_data);
    LOG_INFO("Node A sent reply: Hello from Node A!");
}

/**
 * @brief Node B message handler
 *
 * @param client IPC client instance
 * @param url URL reference
 * @param data Data reference
 * @param arg User argument
 */
static void node_b_message_handler(ssn_client_t *client, ssn_url_ref_t *url,
                                 ssn_data_ref_t *data, void *arg)
{
    (void)client;
    (void)url;
    (void)arg;

    LOG_INFO("Node B received reply: %.*s",
             (int)data->length, (const char*)data->data);

    g_reply_received = true;
}

/**
 * @brief Test node-to-node communication
 */
static bool test_node_communication(void)
{
    LOG_INFO("Test: Node-to-node communication");

    // Create Node A (server node)
    ssn_node_config_t node_a_config = {
        .node_type = "server",
        .node_name = "NodeA",
        .listen_address = "127.0.0.1",
        .listen_port = 8890,
        .capabilities = SSN_NODE_CAP_SERVER | SSN_NODE_CAP_CLIENT |
                       SSN_NODE_CAP_RPC | SSN_NODE_CAP_PUBSUB
    };

    ssn_node_t *node_a = ssn_node_create(&node_a_config);
    if (!node_a) {
        LOG_ERROR("Failed to create Node A");
        return false;
    }

    LOG_INFO("Node A created: id=%s, type=%s, name=%s",
             node_a->node_id, node_a->node_type, node_a->node_name);

    // Start Node A
    if (!ssn_node_start(node_a)) {
        LOG_ERROR("Failed to start Node A");
        ssn_node_destroy(node_a);
        return false;
    }

    LOG_INFO("Node A started");

    // Set message handler for Node A
    ssn_node_set_message_handler(node_a, node_a_message_handler, node_a);

    // Start background server poller (the server is poll-driven)
    pthread_t server_tid;
    g_server_running = 1;
    if (pthread_create(&server_tid, NULL, server_poll_thread, node_a) != 0) {
        LOG_ERROR("Failed to create server poll thread");
        ssn_node_destroy(node_a);
        return false;
    }

    // Create Node B (client node)
    ssn_node_config_t node_b_config = {
        .node_type = "client",
        .node_name = "NodeB",
        .capabilities = SSN_NODE_CAP_CLIENT | SSN_NODE_CAP_RPC | SSN_NODE_CAP_PUBSUB
    };

    ssn_node_t *node_b = ssn_node_create(&node_b_config);
    if (!node_b) {
        LOG_ERROR("Failed to create Node B");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(node_a);
        return false;
    }

    LOG_INFO("Node B created: id=%s, type=%s, name=%s",
             node_b->node_id, node_b->node_type, node_b->node_name);

    // Start Node B
    if (!ssn_node_start(node_b)) {
        LOG_ERROR("Failed to start Node B");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(node_a);
        ssn_node_destroy(node_b);
        return false;
    }

    LOG_INFO("Node B started");

    // Send message from Node B to Node A
    ssn_data_ref_t message_data = {
        .data = "Hello from Node B!",
        .length = 17
    };

    ssn_url_ref_t message_url = {
        .url = "/message",
        .url_len = 8
    };

    LOG_INFO("Node B sending message to Node A: Hello from Node B!");
    if (!ssn_node_send_to_peer(node_b, NODE_A_ADDRESS, &message_url, &message_data)) {
        LOG_ERROR("Failed to send message from Node B to Node A");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(node_a);
        ssn_node_destroy(node_b);
        return false;
    }

    // Set message handler for Node B (after its client was created by the send)
    ssn_node_set_client_message_handler(node_b, node_b_message_handler, node_b);

    // Wait for messages to be processed
    LOG_INFO("Waiting for communication to complete...");
    int timeout = 10; // 10 seconds
    while (timeout > 0 && (!g_message_received || !g_reply_received)) {
        // Poll for events
        ssn_node_poll(node_a, 100);
        ssn_node_poll(node_b, 100);
        sleep(1);
        timeout--;
    }

    // Check if communication completed
    if (!g_message_received) {
        LOG_ERROR("Node A did not receive message");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(node_a);
        ssn_node_destroy(node_b);
        return false;
    }

    if (!g_reply_received) {
        LOG_ERROR("Node B did not receive reply");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(node_a);
        ssn_node_destroy(node_b);
        return false;
    }

    // Stop background server poller
    g_server_running = 0;
    pthread_join(server_tid, NULL);

    // Stop nodes
    ssn_node_stop(node_a);
    ssn_node_stop(node_b);

    // Destroy nodes
    ssn_node_destroy(node_a);
    ssn_node_destroy(node_b);

    LOG_INFO("Communication test completed successfully");
    return true;
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Node communication example started");

    // Run test
    bool test = test_node_communication();

    if (test) {
        LOG_INFO("\nAll communication tests completed successfully!");
        return 0;
    } else {
        LOG_ERROR("\nCommunication test failed!");
        return 1;
    }
}
