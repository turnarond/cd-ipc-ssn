/*
 * client.c - UDP client example
 *
 * This example demonstrates how to create a UDP client using cd-ipc-ssn library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ssn_client.h"
#include "util/ssn_log.h"

#define SERVER_ADDRESS "127.0.0.1:9999"

/**
 * @brief Message handler callback
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

    LOG_INFO("Received message from server: %.*s", 
             (int)data->length, (const char*)data->data);
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Starting UDP client...");

    // Create IPC client
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create UDP client");
        return 1;
    }

    LOG_INFO("UDP client created successfully");

    // Set message handler
    ssn_client_set_on_message(client, message_handler, NULL);

    // Set connection timeout
    struct timespec timeout = {
        .tv_sec = 5,
        .tv_nsec = 0
    };

    // Connect to server
    if (!ssn_client_connect(client, SERVER_ADDRESS, &timeout)) {
        LOG_ERROR("Failed to connect to UDP server: %s", SERVER_ADDRESS);
        ssn_client_close(client);
        return 1;
    }

    LOG_INFO("UDP client connected to %s", SERVER_ADDRESS);

    // Prepare message data
    ssn_data_ref_t data = {
        .data = "Hello from UDP client!",
        .length = 22
    };

    // Prepare URL reference
    ssn_url_ref_t url = {
        .url = "/udp/test",
        .url_len = 9
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

    // Destroy the client
    ssn_client_close(client);

    LOG_INFO("UDP client disconnected");

    return 0;
}
