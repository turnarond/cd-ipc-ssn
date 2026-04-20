/*
 * publisher.c - Publish/Subscribe publisher example
 *
 * This example demonstrates how to create an IPC server that publishes messages to topics.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cd_ipc_server.h"
#include "util/ssn_log.h"

#define SERVER_NAME "unix:///tmp/pubsub_server"

/**
 * @brief Publish a message to a topic
 * 
 * @param server IPC server instance
 * @param topic Topic name
 * @param message Message content
 */
static void publish_message(ipc_server_t *server, const char *topic, const char *message)
{
    // Prepare URL reference
    ipc_url_ref_t url = {
        .url = (char*)topic,
        .url_len = strlen(topic)
    };

    // Prepare data reference
    ipc_data_ref_t data = {
        .data = (void*)message,
        .length = strlen(message)
    };

    // Publish message
    if (ipc_server_publish(server, &url, &data) < 0) {
        LOG_ERROR("Failed to publish message to topic: %s", topic);
    } else {
        LOG_INFO("Publishing message to %s: %s", topic, message);
    }
}

/**
 * @brief Connection handler callback
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
        LOG_INFO("Subscriber connected: id=%u", id);
    } else {
        LOG_INFO("Subscriber disconnected: id=%u", id);
    }
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Starting publisher...");

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

    LOG_INFO("Publisher created successfully");

    // Set connection handler
    ipc_server_set_connect_handler(server, connect_handler, NULL);

    // Start the server
    if (!ipc_server_start(server)) {
        LOG_ERROR("Failed to start IPC server");
        ipc_server_destroy(server);
        return 1;
    }

    LOG_INFO("Publisher started on %s", SERVER_NAME);

    // Wait for subscribers to connect
    LOG_INFO("Waiting for subscribers to connect...");
    int wait_time = 5;
    while (wait_time > 0) {
        ipc_server_poll(server, 100);
        sleep(1);
        wait_time--;
    }

    // Publish messages
    LOG_INFO("\nPublishing messages...");

    // Publish news message
    ipc_server_poll(server, 100);
    publish_message(server, "/news", "Breaking news! Server is online");
    sleep(2);

    // Publish weather message
    ipc_server_poll(server, 100);
    publish_message(server, "/weather", "Today's weather is sunny");
    sleep(2);

    // Publish another news message
    ipc_server_poll(server, 100);
    publish_message(server, "/news", "Another breaking news story");
    sleep(2);

    // Publish sports message
    ipc_server_poll(server, 100);
    publish_message(server, "/sports", "Sports update: Team won the game");
    sleep(2);

    // Wait for messages to be delivered
    LOG_INFO("\nWaiting for messages to be delivered...");
    int deliver_time = 5;
    while (deliver_time > 0) {
        ipc_server_poll(server, 100);
        sleep(1);
        deliver_time--;
    }

    // Server is stopped automatically when destroyed
    LOG_INFO("Stopping publisher...");

    // Destroy the server
    ipc_server_destroy(server);

    LOG_INFO("Publisher destroyed");

    return 0;
}
