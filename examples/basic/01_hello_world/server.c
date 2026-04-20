/*
 * server.c - Hello World IPC server example
 *
 * This example demonstrates how to create an IPC server that listens for connections
 * and handles messages from clients.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cd_ipc_server.h"
#include "util/ssn_log.h"

#define SERVER_NAME "unix:///tmp/hello_server"

/**
 * @brief Message handler callback
 * 
 * This function is called when a message is received from a client.
 * 
 * @param server IPC server instance
 * @param id Client ID
 * @param url URL reference
 * @param data Data reference
 * @param arg User argument
 */
static void message_handler(ipc_server_t *server, cli_id_t id, 
                           ipc_url_ref_t *url, ipc_data_ref_t *data, void *arg)
{
    (void)server;
    (void)url;
    (void)arg;

    LOG_INFO("Message received from client %u: %.*s", 
             id, (int)data->length, (const char*)data->data);
}

/**
 * @brief Connection handler callback
 * 
 * This function is called when a client connects or disconnects.
 * 
 * @param server IPC server instance
 * @param id Client ID
 * @param connect True if client connected, false if disconnected
 * @param arg User argument
 */
static void connect_handler(ipc_server_t *server, cli_id_t id, bool connect, void *arg)
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

    LOG_INFO("Starting Hello World IPC server...");

    // Create server options
    server_options_t options = {
        .send_timeout_ms = 5000,
        .conn_timeout_ms = 3000,
        .idle_timeout_sec = 60,
        .ifname = ""
    };

    // Create IPC server
    ipc_server_t *server = ipc_server_create_with_options(SERVER_NAME, &options);
    if (!server) {
        LOG_ERROR("Failed to create IPC server");
        return 1;
    }

    LOG_INFO("IPC server created successfully");

    // Set message handler
    ipc_server_set_message_handler(server, message_handler, NULL);

    // Set connection handler
    ipc_server_set_connect_handler(server, connect_handler, NULL);

    // Start the server
    if (!ipc_server_start(server)) {
        LOG_ERROR("Failed to start IPC server");
        ipc_server_destroy(server);
        return 1;
    }

    LOG_INFO("IPC server started on %s", SERVER_NAME);

    // Run server for 10 seconds
    LOG_INFO("Server running for 10 seconds...");
    int elapsed = 0;
    while (elapsed < 10) {
        ipc_server_poll(server, 100);
        sleep(1);
        elapsed++;
    }

    // Server is stopped automatically when destroyed
    LOG_INFO("Stopping IPC server...");

    // Destroy the server
    ipc_server_destroy(server);

    LOG_INFO("IPC server destroyed");

    return 0;
}
