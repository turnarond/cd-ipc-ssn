/*
 * server.c - RPC server example
 *
 * This example demonstrates how to create an IPC server that provides RPC methods.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ssn_server.h"
#include "util/ssn_log.h"

#define SERVER_NAME "unix:///tmp/rpc_server"

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

    LOG_INFO("RPC method called: /math/add with parameters: %.*s",
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

    LOG_INFO("RPC method /math/add returned: %d", result);
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

    LOG_INFO("RPC method called: /math/subtract with parameters: %.*s",
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

    LOG_INFO("RPC method /math/subtract returned: %d", result);
}

/**
 * @brief Multiply RPC method handler
 *
 * @param server IPC server instance
 * @param id Client ID
 * @param hdr IPC header
 * @param url URL reference
 * @param data Data reference
 * @param arg User argument
 */
static void multiply_handler(ssn_server_t *server, ssn_peer_id_t id, ssn_header_t *hdr,
                          ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg)
{
    (void)url;
    (void)arg;

    LOG_INFO("RPC method called: /math/multiply with parameters: %.*s",
             (int)data->length, (const char*)data->data);

    // Parse parameters
    int a = 0, b = 0;
    sscanf((const char*)data->data, "{\"a\": %d, \"b\": %d}", &a, &b);

    // Calculate result
    int result = a * b;

    // Prepare response
    char response[64];
    snprintf(response, sizeof(response), "%d", result);

    ssn_data_ref_t resp_data = {
        .data = response,
        .length = strlen(response)
    };

    // Send response
    ssn_server_response(server, id, 0, ssn_get_seqno(hdr), &resp_data);

    LOG_INFO("RPC method /math/multiply returned: %d", result);
}

/**
 * @brief Divide RPC method handler
 *
 * @param server IPC server instance
 * @param id Client ID
 * @param hdr IPC header
 * @param url URL reference
 * @param data Data reference
 * @param arg User argument
 */
static void divide_handler(ssn_server_t *server, ssn_peer_id_t id, ssn_header_t *hdr,
                         ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg)
{
    (void)url;
    (void)arg;

    LOG_INFO("RPC method called: /math/divide with parameters: %.*s",
             (int)data->length, (const char*)data->data);

    // Parse parameters
    int a = 0, b = 0;
    sscanf((const char*)data->data, "{\"a\": %d, \"b\": %d}", &a, &b);

    // Calculate result
    int result = 0;
    if (b != 0) {
        result = a / b;
    } else {
        LOG_ERROR("Division by zero");
        // Send error response
        ssn_data_ref_t error_data = {
            .data = "Error: Division by zero",
            .length = 23
        };
        ssn_server_response(server, id, 1, ssn_get_seqno(hdr), &error_data);
        return;
    }

    // Prepare response
    char response[64];
    snprintf(response, sizeof(response), "%d", result);

    ssn_data_ref_t resp_data = {
        .data = response,
        .length = strlen(response)
    };

    // Send response
    ssn_server_response(server, id, 0, ssn_get_seqno(hdr), &resp_data);

    LOG_INFO("RPC method /math/divide returned: %d", result);
}

/**
 * @brief Connection handler callback
 *
 * @param server IPC server instance
 * @param id Client ID
 * @param connect True if client connected, false if disconnected
 * @param arg User argument
 */
static void connect_handler(ssn_server_t *server, ssn_peer_id_t id, bool connect, void *arg)
{
    (void)server;
    (void)arg;

    if (connect) {
        LOG_INFO("Client connected: id=%u", id);
    } else {
        LOG_INFO("Client disconnected: id=%u", id);
    }
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Starting RPC server...");

    // Create server options
    server_options_t options = {
        .send_timeout_ms = 5000,
        .conn_timeout_ms = 3000,
        .idle_timeout_sec = 60,
        .ifname = ""
    };

    // Create IPC server
    ssn_server_t *server = ssn_server_create_with_options(SERVER_NAME, &options);
    if (!server) {
        LOG_ERROR("Failed to create IPC server");
        return 1;
    }

    LOG_INFO("RPC server created successfully");

    // Set connection handler
    ssn_server_set_connect_handler(server, connect_handler, NULL);

    // Register RPC methods
    ssn_url_ref_t add_url = {.url = "/math/add", .url_len = 9};
    ssn_url_ref_t subtract_url = {.url = "/math/subtract", .url_len = 14};
    ssn_url_ref_t multiply_url = {.url = "/math/multiply", .url_len = 14};
    ssn_url_ref_t divide_url = {.url = "/math/divide", .url_len = 12};

    ssn_server_add_method(server, &add_url, add_handler, NULL);
    ssn_server_add_method(server, &subtract_url, subtract_handler, NULL);
    ssn_server_add_method(server, &multiply_url, multiply_handler, NULL);
    ssn_server_add_method(server, &divide_url, divide_handler, NULL);

    LOG_INFO("RPC methods registered successfully");

    // Start the server
    if (!ssn_server_start(server)) {
        LOG_ERROR("Failed to start IPC server");
        ssn_server_destroy(server);
        return 1;
    }

    LOG_INFO("RPC server started on %s", SERVER_NAME);

    // Run server for 15 seconds
    LOG_INFO("Server running for 15 seconds...");
    int elapsed = 0;
    while (elapsed < 15) {
        ssn_server_poll(server, 100);
        sleep(1);
        elapsed++;
    }

    // Server is stopped automatically when destroyed
    LOG_INFO("Stopping RPC server...");

    // Destroy the server
    ssn_server_destroy(server);

    LOG_INFO("RPC server destroyed");

    return 0;
}
