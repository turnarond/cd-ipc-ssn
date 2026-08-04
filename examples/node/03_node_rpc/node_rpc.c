/*
 * node_rpc.c - Node RPC example
 *
 * This example demonstrates how to use RPC (Remote Procedure Call) between nodes in the node abstraction layer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "node/ssn_node.h"
#include "util/ssn_log.h"

#define SERVER_ADDRESS "tcp://127.0.0.1:8891"

static bool g_rpc_complete = false;

static volatile int g_server_running = 0;

/**
 * @brief Background server poller
 *
 * Runs ssn_node_poll in a thread so the server node can accept connections
 * and handle the client handshake while the client connects.
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
 * @brief Add RPC method handler
 * 
 * @param server IPC server instance
 * @param id Client ID
 * @param hdr IPC header
 * @param url URL reference
 * @param data Data reference
 * @param arg User argument
 */
static void add_handler(ssn_server_t *server, ssn_peer_id_t id, ssn_header_t *hdr, 
                       ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg)
{
    (void)url;
    (void)arg;

    LOG_INFO("Server received RPC call: /math/add with %.*s", 
             (int)data->length, (const char*)data->data);

    // Parse parameters (simplified JSON parsing)
    int a = 0, b = 0;
    sscanf((const char*)data->data, "{\"a\": %d, \"b\": %d}", &a, &b);

    // Calculate result
    int result = a + b;

    // Prepare response
    char response[64];
    snprintf(response, sizeof(response), "%d", result);

    ssn_data_ref_t resp_data = {
        .data = response,
        .length = strlen(response)
    };

    // Send response
    ssn_server_response(server, id, 0, ssn_get_seqno(hdr), &resp_data);

    LOG_INFO("Server sent RPC response: %d", result);
}

/**
 * @brief Subtract RPC method handler
 * 
 * @param server IPC server instance
 * @param id Client ID
 * @param hdr IPC header
 * @param url URL reference
 * @param data Data reference
 * @param arg User argument
 */
static void subtract_handler(ssn_server_t *server, ssn_peer_id_t id, ssn_header_t *hdr, 
                          ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg)
{
    (void)url;
    (void)arg;

    LOG_INFO("Server received RPC call: /math/subtract with %.*s", 
             (int)data->length, (const char*)data->data);

    // Parse parameters
    int a = 0, b = 0;
    sscanf((const char*)data->data, "{\"a\": %d, \"b\": %d}", &a, &b);

    // Calculate result
    int result = a - b;

    // Prepare response
    char response[64];
    snprintf(response, sizeof(response), "%d", result);

    ssn_data_ref_t resp_data = {
        .data = response,
        .length = strlen(response)
    };

    // Send response
    ssn_server_response(server, id, 0, ssn_get_seqno(hdr), &resp_data);

    LOG_INFO("Server sent RPC response: %d", result);
}

/**
 * @brief RPC reply handler
 * 
 * @param client IPC client instance
 * @param hdr IPC header
 * @param data Data reference
 * @param arg User argument
 */
static void rpc_reply_handler(ssn_client_t *client, ssn_header_t *hdr, 
                            ssn_data_ref_t *data, void *arg)
{
    (void)client;
    (void)hdr;
    (void)arg;

    if (data) {
        LOG_INFO("Client received RPC response: %.*s", 
                 (int)data->length, (const char*)data->data);
    } else {
        LOG_ERROR("Client received RPC error");
    }

    g_rpc_complete = true;
}

/**
 * @brief Test node-to-node RPC
 */
static bool test_node_rpc(void)
{
    LOG_INFO("Test: Node-to-node RPC");

    // Create server node
    ssn_node_config_t server_config = {
        .node_type = "server",
        .node_name = "ServerNode",
        .listen_address = "127.0.0.1",
        .listen_port = 8891,
        .capabilities = SSN_NODE_CAP_SERVER | SSN_NODE_CAP_RPC
    };

    ssn_node_t *server_node = ssn_node_create(&server_config);
    if (!server_node) {
        LOG_ERROR("Failed to create server node");
        return false;
    }

    LOG_INFO("Server node created: id=%s, type=%s, name=%s", 
             server_node->node_id, server_node->node_type, server_node->node_name);

    // Start server node
    if (!ssn_node_start(server_node)) {
        LOG_ERROR("Failed to start server node");
        ssn_node_destroy(server_node);
        return false;
    }

    LOG_INFO("Server node started");

    // Register RPC methods
    ssn_url_ref_t add_url = {.url = "/math/add", .url_len = 9};
    ssn_url_ref_t subtract_url = {.url = "/math/subtract", .url_len = 14};

    if (!ssn_node_add_rpc_method(server_node, &add_url, add_handler, NULL)) {
        LOG_ERROR("Failed to register add method");
        ssn_node_destroy(server_node);
        return false;
    }
    LOG_INFO("RPC method /math/add registered");

    if (!ssn_node_add_rpc_method(server_node, &subtract_url, subtract_handler, NULL)) {
        LOG_ERROR("Failed to register subtract method");
        ssn_node_destroy(server_node);
        return false;
    }
    LOG_INFO("RPC method /math/subtract registered");

    // Start background server poller (the server is poll-driven)
    pthread_t server_tid;
    g_server_running = 1;
    if (pthread_create(&server_tid, NULL, server_poll_thread, server_node) != 0) {
        LOG_ERROR("Failed to create server poll thread");
        ssn_node_destroy(server_node);
        return false;
    }

    // Create client node
    ssn_node_config_t client_config = {
        .node_type = "client",
        .node_name = "ClientNode",
        .capabilities = SSN_NODE_CAP_CLIENT | SSN_NODE_CAP_RPC
    };

    ssn_node_t *client_node = ssn_node_create(&client_config);
    if (!client_node) {
        LOG_ERROR("Failed to create client node");
        ssn_node_destroy(server_node);
        return false;
    }

    LOG_INFO("Client node created: id=%s, type=%s, name=%s", 
             client_node->node_id, client_node->node_type, client_node->node_name);

    // Start client node
    if (!ssn_node_start(client_node)) {
        LOG_ERROR("Failed to start client node");
        ssn_node_destroy(server_node);
        ssn_node_destroy(client_node);
        return false;
    }

    LOG_INFO("Client node started");

    // Test add method
    LOG_INFO("Client calling /math/add with {\"a\": 5, \"b\": 3}");
    ssn_data_ref_t add_data = {
        .data = "{\"a\": 5, \"b\": 3}",
        .length = 16
    };

    ssn_url_ref_t add_url_ref = {
        .url = "/math/add",
        .url_len = 9
    };

    g_rpc_complete = false;
    if (ssn_node_rpc_call(client_node, SERVER_ADDRESS, &add_url_ref, &add_data,
                         rpc_reply_handler, NULL, 5000) < 0) {
        LOG_ERROR("Failed to make add RPC call");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(server_node);
        ssn_node_destroy(client_node);
        return false;
    }

    // Wait for response
    int timeout = 5;
    while (timeout > 0 && !g_rpc_complete) {
        ssn_node_poll(client_node, 100);
        ssn_node_poll(server_node, 100);
        sleep(1);
        timeout--;
    }

    if (!g_rpc_complete) {
        LOG_ERROR("Add RPC call timed out");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(server_node);
        ssn_node_destroy(client_node);
        return false;
    }

    // Test subtract method
    LOG_INFO("Client calling /math/subtract with {\"a\": 10, \"b\": 4}");
    ssn_data_ref_t subtract_data = {
        .data = "{\"a\": 10, \"b\": 4}",
        .length = 17
    };

    ssn_url_ref_t subtract_url_ref = {
        .url = "/math/subtract",
        .url_len = 14
    };

    g_rpc_complete = false;
    if (ssn_node_rpc_call(client_node, SERVER_ADDRESS, &subtract_url_ref, &subtract_data,
                         rpc_reply_handler, NULL, 5000) < 0) {
        LOG_ERROR("Failed to make subtract RPC call");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(server_node);
        ssn_node_destroy(client_node);
        return false;
    }

    // Wait for response
    timeout = 5;
    while (timeout > 0 && !g_rpc_complete) {
        ssn_node_poll(client_node, 100);
        ssn_node_poll(server_node, 100);
        sleep(1);
        timeout--;
    }

    if (!g_rpc_complete) {
        LOG_ERROR("Subtract RPC call timed out");
        g_server_running = 0;
        pthread_join(server_tid, NULL);
        ssn_node_destroy(server_node);
        ssn_node_destroy(client_node);
        return false;
    }

    // Stop background server poller
    g_server_running = 0;
    pthread_join(server_tid, NULL);

    // Stop nodes
    ssn_node_stop(server_node);
    ssn_node_stop(client_node);

    // Destroy nodes
    ssn_node_destroy(server_node);
    ssn_node_destroy(client_node);

    LOG_INFO("RPC test completed successfully");
    return true;
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Node RPC example started");

    // Run test
    bool test = test_node_rpc();

    if (test) {
        LOG_INFO("\nAll RPC tests completed successfully!");
        return 0;
    } else {
        LOG_ERROR("\nRPC test failed!");
        return 1;
    }
}
