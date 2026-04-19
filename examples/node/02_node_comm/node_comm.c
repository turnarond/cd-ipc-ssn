/*
 * node_comm.c - Node communication example
 *
 * This example demonstrates how nodes can communicate with each other using the node abstraction layer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "node/ipc_node.h"
#include "util/ssn_log.h"

#define NODE_A_ADDRESS "127.0.0.1:8890"

static bool g_message_received = false;
static bool g_reply_received = false;

/**
 * @brief Node A message handler
 * 
 * @param server IPC server instance
 * @param id Client ID
 * @param url URL reference
 * @param data Data reference
 * @param arg User argument
 */
static void node_a_message_handler(ipc_server_t *server, cli_id_t id,
                                   ipc_url_ref_t *url, ipc_data_ref_t *data, void *arg)
{
    (void)server;
    (void)id;
    (void)url;
    ipc_node_t *node = (ipc_node_t *)arg;

    LOG_INFO("Node A received message: %.*s", 
             (int)data->length, (const char*)data->data);

    g_message_received = true;

    // Send reply back to Node B
    ipc_data_ref_t reply_data = {
        .data = "Hello from Node A!",
        .length = 17
    };

    ipc_url_ref_t reply_url = {
        .url = "/reply",
        .url_len = 6
    };

    // Use the client to send reply
    ipc_client_t *client = ipc_node_get_client(node);
    if (client) {
        ipc_client_message(client, &reply_url, &reply_data);
        LOG_INFO("Node A sent reply: Hello from Node A!");
    }
}

/**
 * @brief Node B message handler
 * 
 * @param client IPC client instance
 * @param url URL reference
 * @param data Data reference
 * @param arg User argument
 */
static void node_b_message_handler(ipc_client_t *client, ipc_url_ref_t *url, 
                                 ipc_data_ref_t *data, void *arg)
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
    ipc_node_config_t node_a_config = {
        .node_type = "server",
        .node_name = "NodeA",
        .listen_address = "127.0.0.1",
        .listen_port = 8890,
        .capabilities = IPC_NODE_CAP_SERVER | IPC_NODE_CAP_CLIENT | 
                       IPC_NODE_CAP_RPC | IPC_NODE_CAP_PUBSUB
    };

    ipc_node_t *node_a = ipc_node_create(&node_a_config);
    if (!node_a) {
        LOG_ERROR("Failed to create Node A");
        return false;
    }

    LOG_INFO("Node A created: id=%s, type=%s, name=%s", 
             node_a->node_id, node_a->node_type, node_a->node_name);

    // Set message handler for Node A
    ipc_node_set_message_handler(node_a, node_a_message_handler, node_a);

    // Start Node A
    if (!ipc_node_start(node_a)) {
        LOG_ERROR("Failed to start Node A");
        ipc_node_destroy(node_a);
        return false;
    }

    LOG_INFO("Node A started");

    // Create Node B (client node)
    ipc_node_config_t node_b_config = {
        .node_type = "client",
        .node_name = "NodeB",
        .capabilities = IPC_NODE_CAP_CLIENT | IPC_NODE_CAP_RPC | IPC_NODE_CAP_PUBSUB
    };

    ipc_node_t *node_b = ipc_node_create(&node_b_config);
    if (!node_b) {
        LOG_ERROR("Failed to create Node B");
        ipc_node_destroy(node_a);
        return false;
    }

    LOG_INFO("Node B created: id=%s, type=%s, name=%s", 
             node_b->node_id, node_b->node_type, node_b->node_name);

    // Start Node B
    if (!ipc_node_start(node_b)) {
        LOG_ERROR("Failed to start Node B");
        ipc_node_destroy(node_a);
        ipc_node_destroy(node_b);
        return false;
    }

    LOG_INFO("Node B started");

    // Set message handler for Node B
    ipc_node_set_client_message_handler(node_b, node_b_message_handler, node_b);

    // Send message from Node B to Node A
    ipc_data_ref_t message_data = {
        .data = "Hello from Node B!",
        .length = 17
    };

    ipc_url_ref_t message_url = {
        .url = "/message",
        .url_len = 8
    };

    LOG_INFO("Node B sending message to Node A: Hello from Node B!");
    if (!ipc_node_send_to_peer(node_b, NODE_A_ADDRESS, &message_url, &message_data)) {
        LOG_ERROR("Failed to send message from Node B to Node A");
        ipc_node_destroy(node_a);
        ipc_node_destroy(node_b);
        return false;
    }

    // Wait for messages to be processed
    LOG_INFO("Waiting for communication to complete...");
    int timeout = 10; // 10 seconds
    while (timeout > 0 && (!g_message_received || !g_reply_received)) {
        // Poll for events
        ipc_node_poll(node_a, 100);
        ipc_node_poll(node_b, 100);
        sleep(1);
        timeout--;
    }

    // Check if communication completed
    if (!g_message_received) {
        LOG_ERROR("Node A did not receive message");
        ipc_node_destroy(node_a);
        ipc_node_destroy(node_b);
        return false;
    }

    if (!g_reply_received) {
        LOG_ERROR("Node B did not receive reply");
        ipc_node_destroy(node_a);
        ipc_node_destroy(node_b);
        return false;
    }

    // Stop nodes
    ipc_node_stop(node_a);
    ipc_node_stop(node_b);

    // Destroy nodes
    ipc_node_destroy(node_a);
    ipc_node_destroy(node_b);

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
