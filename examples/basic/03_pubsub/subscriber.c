/*
 * subscriber.c - Publish/Subscribe subscriber example
 *
 * This example demonstrates how to create an IPC client that subscribes to topics.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ssn_client.h"
#include "util/ssn_log.h"

#define SERVER_NAME "unix:///tmp/pubsub_server"

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
    (void)arg;

    LOG_INFO("Received message on %s: %.*s",
             url->url, (int)data->length, (const char*)data->data);
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Starting subscriber...");

    // Create IPC client
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create IPC client");
        return 1;
    }

    // Set message handler
    ssn_client_set_on_message(client, message_handler, NULL);

    LOG_INFO("Subscriber created successfully");

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

    LOG_INFO("Connected to publisher: %s", SERVER_NAME);

    // Subscribe to news topic
    ssn_url_ref_t news_topic = {
        .url = "/news",
        .url_len = 6
    };

    if (!ssn_client_subscribe(client, &news_topic, message_handler, NULL, 5000)) {
        LOG_ERROR("Failed to subscribe to topic: /news");
        ssn_client_close(client);
        return 1;
    }

    LOG_INFO("Subscribed to topic: /news");

    // Run for 15 seconds
    LOG_INFO("Running for 15 seconds...");
    int count = 0;
    while (count < 15) {
        // Poll for messages
        ssn_client_poll(client, 1000);
        sleep(1);
        count++;
    }

    // Unsubscribe from topic
    if (!ssn_client_unsubscribe(client, &news_topic, 5000)) {
        LOG_ERROR("Failed to unsubscribe from topic: /news");
    } else {
        LOG_INFO("Unsubscribed from topic: /news");
    }

    // Disconnect from server
    ssn_client_disconnect(client);

    // Close the client
    ssn_client_close(client);

    LOG_INFO("Subscriber closed");

    return 0;
}
