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

#include "ssn_server.h"
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
static void message_handler(ssn_server_t *server, ssn_peer_id_t id,
                           ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg)
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

    LOG_INFO("Starting Hello World IPC server...");

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

    LOG_INFO("IPC server created successfully");

    // Set message handler
    ssn_server_set_message_handler(server, message_handler, NULL);

    // Set connection handler
    ssn_server_set_connect_handler(server, connect_handler, NULL);

    // Start the server
    if (!ssn_server_start(server)) {
        LOG_ERROR("Failed to start IPC server");
        ssn_server_destroy(server);
        return 1;
    }

    LOG_INFO("IPC server started on %s", SERVER_NAME);

    // Run server for 10 seconds
    LOG_INFO("Server running for 10 seconds...");
    /* 事件驱动循环：poll 阻塞至多 1 秒，有连接/消息事件立即返回处理，
     * 避免 poll(100)+sleep(1) 的空转周期（约 1.1s）导致客户端连接握手超时 */
    int elapsed = 0;
    while (elapsed < 10) {
        ssn_server_poll(server, 1000);
        elapsed++;
    }

    // Server is stopped automatically when destroyed
    LOG_INFO("Stopping IPC server...");

    // Destroy the server
    ssn_server_destroy(server);

    LOG_INFO("IPC server destroyed");

    return 0;
}
