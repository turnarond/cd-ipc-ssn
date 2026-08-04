/*
 * client.c - Hello World IPC client example
 *
 * This example demonstrates how to create an IPC client that connects to a server
 * and sends a message.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ssn_client.h"
#include "util/ssn_log.h"

#define SERVER_NAME "unix:///tmp/hello_server"

/**
 * @brief Message handler callback
 *
 * This function is called when a message is received from the server.
 *
 * @param client IPC client instance
 * @param url URL reference
 * @param data Data reference
 * @param arg User argument
 */
static void message_handler(ssn_client_t *client, ssn_url_ref_t *url,
                           ssn_data_ref_t *data, void *arg)
{
    (void)client;
    (void)url;
    (void)arg;

    LOG_INFO("Message received from server: %.*s",
             (int)data->length, (const char*)data->data);
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Starting Hello World IPC client...");

    // Create IPC client
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create IPC client");
        return 1;
    }

    // Set message handler
    ssn_client_set_on_message(client, message_handler, NULL);

    LOG_INFO("IPC client created successfully");

    // Set connection timeout
    struct timespec timeout = {
        .tv_sec = 5,
        .tv_nsec = 0
    };

    // Connect to server
    if (!ssn_client_connect(client, SERVER_NAME, &timeout)) {
        LOG_ERROR("Failed to connect to server: %s", SERVER_NAME);
        ssn_client_close(client);
        return 1;
    }

    LOG_INFO("Connected to server: %s", SERVER_NAME);

    // Prepare message data
    ssn_data_ref_t data = {
        .data = "Hello from client!",
        .length = 18
    };

    // Prepare URL reference
    ssn_url_ref_t url = {
        .url = "/hello",
        .url_len = 6
    };

    // Send message to server
    if (ssn_client_message(client, &url, &data) < 0) {
        LOG_ERROR("Failed to send message");
        ssn_client_close(client);
        return 1;
    }

    LOG_INFO("Message sent successfully");

    // Wait for possible response
    LOG_INFO("Waiting for 2 seconds...");
    sleep(2);

    // Disconnect from server
    ssn_client_disconnect(client);

    // Close the client
    ssn_client_close(client);

    LOG_INFO("IPC client closed");

    return 0;
}
