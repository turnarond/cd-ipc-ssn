/*
 * subscriber2.c - Publish/Subscribe subscriber example (second subscriber)
 *
 * This example demonstrates another IPC client that subscribes to different topics.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cd_ipc_client.h"
#include "util/ssn_log.h"

#define SERVER_NAME "/tmp/pubsub_server"

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
static void message_handler(ipc_client_t *client, ipc_url_ref_t *url, 
                           ipc_data_ref_t *data, void *arg)
{
    (void)client;
    (void)arg;

    LOG_INFO("Received message on %s: %.*s", 
             url->url, (int)data->length, (const char*)data->data);
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Starting subscriber2...");

    // Create IPC client with message handler
    ipc_client_t *client = ipc_client_create(message_handler, NULL);
    if (!client) {
        LOG_ERROR("Failed to create IPC client");
        return 1;
    }

    LOG_INFO("Subscriber2 created successfully");

    // Set connection timeout
    struct timespec timeout = {
        .tv_sec = 5,
        .tv_nsec = 0
    };

    // Connect to server
    if (!ipc_client_connect(client, SERVER_NAME, &timeout)) {
        LOG_ERROR("Failed to connect to server: %s", SERVER_NAME);
        ipc_client_close(client);
        return 1;
    }

    LOG_INFO("Connected to publisher: %s", SERVER_NAME);

    // Subscribe to weather topic
    ipc_url_ref_t weather_topic = {
        .url = "/weather",
        .url_len = 8
    };

    if (!ipc_client_subscribe(client, &weather_topic, NULL, NULL, 5000)) {
        LOG_ERROR("Failed to subscribe to topic: /weather");
        ipc_client_close(client);
        return 1;
    }

    LOG_INFO("Subscribed to topic: /weather");

    // Also subscribe to sports topic
    ipc_url_ref_t sports_topic = {
        .url = "/sports",
        .url_len = 7
    };

    if (!ipc_client_subscribe(client, &sports_topic, NULL, NULL, 5000)) {
        LOG_ERROR("Failed to subscribe to topic: /sports");
    } else {
        LOG_INFO("Subscribed to topic: /sports");
    }

    // Run for 15 seconds
    LOG_INFO("Running for 15 seconds...");
    int count = 0;
    while (count < 15) {
        // Poll for messages
        ipc_client_poll(client, 1000);
        sleep(1);
        count++;
    }

    // Unsubscribe from topics
    if (!ipc_client_unsubscribe(client, &weather_topic, NULL, NULL, 5000)) {
        LOG_ERROR("Failed to unsubscribe from topic: /weather");
    } else {
        LOG_INFO("Unsubscribed from topic: /weather");
    }

    if (!ipc_client_unsubscribe(client, &sports_topic, NULL, NULL, 5000)) {
        LOG_ERROR("Failed to unsubscribe from topic: /sports");
    } else {
        LOG_INFO("Unsubscribed from topic: /sports");
    }

    // Disconnect from server
    ipc_client_disconnect(client);

    // Close the client
    ipc_client_close(client);

    LOG_INFO("Subscriber2 closed");

    return 0;
}
