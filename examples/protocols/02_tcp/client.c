/*
 * client.c - TCP client example
 *
 * This example demonstrates how to create a TCP client using cd-ipc-ssn library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cd_ipc_client.h"
#include "util/ssn_log.h"

#define SERVER_ADDRESS "127.0.0.1:8888"

/**
 * @brief Message handler callback
 * 
 * @param client IPC client instance
 * @param url URL reference
 * @param data Data reference
 * @param arg User argument
 */
static void message_handler(ipc_client_t *client, ipc_url_ref_t *url, 
                           ipc_data_ref_t *data, void *arg)
{
    (void)client;
    (void)url;
    (void)arg;

    LOG_INFO("Received message from server: %.*s", 
             (int)data->length, (const char*)data->data);
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Starting TCP client...");

    // Create IPC client with message handler
    ipc_client_t *client = ipc_client_create(message_handler, NULL);
    if (!client) {
        LOG_ERROR("Failed to create TCP client");
        return 1;
    }

    LOG_INFO("TCP client created successfully");

    // Set connection timeout
    struct timespec timeout = {
        .tv_sec = 5,
        .tv_nsec = 0
    };

    // Connect to server
    if (!ipc_client_connect(client, SERVER_ADDRESS, &timeout)) {
        LOG_ERROR("Failed to connect to TCP server: %s", SERVER_ADDRESS);
        ipc_client_close(client);
        return 1;
    }

    LOG_INFO("TCP client connected to %s", SERVER_ADDRESS);

    // Prepare message data
    ipc_data_ref_t data = {
        .data = "Hello from TCP client!",
        .length = 22
    };

    // Prepare URL reference
    ipc_url_ref_t url = {
        .url = "/tcp/test",
        .url_len = 9
    };

    // Send message to server
    if (ipc_client_message(client, &url, &data) < 0) {
        LOG_ERROR("Failed to send message");
        ipc_client_close(client);
        return 1;
    }

    LOG_INFO("Message sent successfully");

    // Wait for possible response
    LOG_INFO("Waiting for 2 seconds...");
    sleep(2);

    // Disconnect from server
    ipc_client_disconnect(client);

    // Destroy the client
    ipc_client_close(client);

    LOG_INFO("TCP client disconnected");

    return 0;
}
