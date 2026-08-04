/*
 * transport_selection.c - Transport protocol selection example
 *
 * This example demonstrates how to use different transport protocols in cd-ipc-ssn library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ssn_client.h"
#include "ssn_server.h"
#include "util/ssn_log.h"

#define UNIX_SOCKET_SERVER "/tmp/unix_socket_server"
#define TCP_SERVER "127.0.0.1:8888"
#define UDP_SERVER "127.0.0.1:9999"

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

    LOG_INFO("Message received: %.*s",
             (int)data->length, (const char*)data->data);
}

/**
 * @brief Test Unix Socket transport
 */
static bool test_unix_socket(void)
{
    LOG_INFO("Test 1: Unix Socket transport");

   // Create Unix Socket server
    ssn_server_t *server = ssn_server_create(UNIX_SOCKET_SERVER);
    if (!server) {
        LOG_ERROR("Failed to create Unix Socket server");
        return false;
    }

    if (!ssn_server_start(server)) {
        LOG_ERROR("Failed to start Unix Socket server");
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("Unix Socket server started on %s", UNIX_SOCKET_SERVER);

    // Create client
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create Unix Socket client");
        ssn_server_destroy(server);
        return false;
    }

    // Set message handler
    ssn_client_set_on_message(client, message_handler, NULL);

    // Connect to server
    struct timespec timeout = {
        .tv_sec = 5,
        .tv_nsec = 0
    };

    if (!ssn_client_connect(client, UNIX_SOCKET_SERVER, &timeout)) {
        LOG_ERROR("Failed to connect to Unix Socket server");
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("Unix Socket client connected");

    // Send message
    ssn_data_ref_t data = {
        .data = "Hello via Unix Socket",
        .length = 21
    };

    ssn_url_ref_t url = {
        .url = "/test",
        .url_len = 6
    };

    if (ssn_client_message(client, &url, &data) < 0) {
        LOG_ERROR("Failed to send message via Unix Socket");
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("Message sent via Unix Socket");

    // Wait for message to be received
    sleep(2);

    // Cleanup
    ssn_client_close(client);
    ssn_server_destroy(server);

    LOG_INFO("Test 1 passed");
    return true;
}

/**
 * @brief Test TCP transport
 */
static bool test_tcp(void)
{
    LOG_INFO("\nTest 2: TCP transport");

    // Create TCP server
    ssn_server_t *server = ssn_server_create(TCP_SERVER);
    if (!server) {
        LOG_ERROR("Failed to create TCP server");
        return false;
    }

    if (!ssn_server_start(server)) {
        LOG_ERROR("Failed to start TCP server");
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("TCP server started on %s", TCP_SERVER);

    // Create client
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create TCP client");
        ssn_server_destroy(server);
        return false;
    }

    // Set message handler
    ssn_client_set_on_message(client, message_handler, NULL);

    // Connect to server
    struct timespec timeout = {
        .tv_sec = 5,
        .tv_nsec = 0
    };

    if (!ssn_client_connect(client, TCP_SERVER, &timeout)) {
        LOG_ERROR("Failed to connect to TCP server");
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("TCP client connected");

    // Send message
    ssn_data_ref_t data = {
        .data = "Hello via TCP",
        .length = 14
    };

    ssn_url_ref_t url = {
        .url = "/test",
        .url_len = 6
    };

    if (ssn_client_message(client, &url, &data) < 0) {
        LOG_ERROR("Failed to send message via TCP");
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("Message sent via TCP");

    // Wait for message to be received
    sleep(2);

    // Cleanup
    ssn_client_close(client);
    ssn_server_destroy(server);

    LOG_INFO("Test 2 passed");
    return true;
}

/**
 * @brief Test UDP transport
 */
static bool test_udp(void)
{
    LOG_INFO("\nTest 3: UDP transport");

    // Create UDP server
    ssn_server_t *server = ssn_server_create(UDP_SERVER);
    if (!server) {
        LOG_ERROR("Failed to create UDP server");
        return false;
    }

    if (!ssn_server_start(server)) {
        LOG_ERROR("Failed to start UDP server");
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("UDP server started on %s", UDP_SERVER);

    // Create client
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create UDP client");
        ssn_server_destroy(server);
        return false;
    }

    // Set message handler
    ssn_client_set_on_message(client, message_handler, NULL);

    // Connect to server
    struct timespec timeout = {
        .tv_sec = 5,
        .tv_nsec = 0
    };

    if (!ssn_client_connect(client, UDP_SERVER, &timeout)) {
        LOG_ERROR("Failed to connect to UDP server");
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("UDP client connected");

    // Send message
    ssn_data_ref_t data = {
        .data = "Hello via UDP",
        .length = 14
    };

    ssn_url_ref_t url = {
        .url = "/test",
        .url_len = 6
    };

    if (ssn_client_message(client, &url, &data) < 0) {
        LOG_ERROR("Failed to send message via UDP");
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("Message sent via UDP");

    // Wait for message to be received
    sleep(2);

    // Cleanup
    ssn_client_close(client);
    ssn_server_destroy(server);

    LOG_INFO("Test 3 passed");
    return true;
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Transport selection example started");

    // Run tests
    bool test1 = test_unix_socket();
    bool test2 = test_tcp();
    bool test3 = test_udp();

    if (test1 && test2 && test3) {
        LOG_INFO("\nAll transport tests completed successfully!");
        return 0;
    } else {
        LOG_ERROR("\nSome transport tests failed!");
        return 1;
    }
}
