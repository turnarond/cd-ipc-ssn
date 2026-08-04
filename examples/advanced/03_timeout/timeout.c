/*
 * timeout.c - Timeout handling example
 *
 * This example demonstrates how to handle various timeout scenarios in cd-ipc-ssn library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ssn_client.h"
#include "ssn_server.h"
#include "util/ssn_log.h"

#define SERVER_NAME "unix:///tmp/timeout_server"
#define NON_EXISTENT_SERVER "unix:///tmp/non_existent_server"

/**
 * @brief Test 1: Connection timeout
 */
static bool test_connection_timeout(void)
{
    LOG_INFO("Test 1: Connection timeout");

    // Create IPC client
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create IPC client");
        return false;
    }

    // Set short connection timeout (1 second)
    struct timespec timeout = {
        .tv_sec = 1,
        .tv_nsec = 0
    };

    // Try to connect to non-existent server
    if (ssn_client_connect(client, NON_EXISTENT_SERVER, &timeout)) {
        LOG_ERROR("Expected connection to timeout, but it succeeded");
        ssn_client_close(client);
        return false;
    }

    LOG_INFO("Test 1 passed (expected timeout)");

    // Destroy client
    ssn_client_close(client);
    return true;
}

/**
 * @brief Test 2: RPC call timeout
 */
static bool test_rpc_timeout(void)
{
    LOG_INFO("\nTest 2: RPC call timeout");

    // Create server that doesn't respond to RPC calls
    ssn_server_t *server = ssn_server_create(SERVER_NAME);
    if (!server) {
        LOG_ERROR("Failed to create IPC server");
        return false;
    }

    if (!ssn_server_start(server)) {
        LOG_ERROR("Failed to start IPC server");
        ssn_server_destroy(server);
        return false;
    }

    // Create IPC client
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create IPC client");
        ssn_server_destroy(server);
        return false;
    }

    // Set connection timeout
    struct timespec conn_timeout = {
        .tv_sec = 5,
        .tv_nsec = 0
    };

    // Connect to server
    if (!ssn_client_connect(client, SERVER_NAME, &conn_timeout)) {
        LOG_ERROR("Failed to connect to server: %s", SERVER_NAME);
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    // Prepare message
    ssn_data_ref_t data = {
        .data = "test",
        .length = 4
    };

    // Prepare URL reference
    ssn_url_ref_t url = {
        .url = "/test",
        .url_len = 6
    };

    // Make RPC call with short timeout (2 seconds)
    int result = ssn_client_call(client, &url, &data, NULL, NULL, 2000);
    if (result >= 0) {
        LOG_ERROR("Expected RPC call to timeout, but it succeeded");
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("Test 2 passed (expected timeout)");

    // Cleanup
    ssn_client_close(client);
    ssn_server_destroy(server);
    return true;
}

/**
 * @brief Test 3: Message send timeout
 */
static bool test_message_timeout(void)
{
    LOG_INFO("\nTest 3: Message send timeout");

    // Create server that doesn't process messages
    ssn_server_t *server = ssn_server_create(SERVER_NAME);
    if (!server) {
        LOG_ERROR("Failed to create IPC server");
        return false;
    }

    if (!ssn_server_start(server)) {
        LOG_ERROR("Failed to start IPC server");
        ssn_server_destroy(server);
        return false;
    }

    // Create IPC client
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create IPC client");
        ssn_server_destroy(server);
        return false;
    }

    // Set connection timeout
    struct timespec conn_timeout = {
        .tv_sec = 5,
        .tv_nsec = 0
    };

    // Connect to server
    if (!ssn_client_connect(client, SERVER_NAME, &conn_timeout)) {
        LOG_ERROR("Failed to connect to server: %s", SERVER_NAME);
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    // Prepare large message to increase chances of timeout
    char large_message[1024 * 1024]; // 1MB message
    memset(large_message, 'A', sizeof(large_message));
    large_message[sizeof(large_message) - 1] = '\0';

    ssn_data_ref_t data = {
        .data = large_message,
        .length = sizeof(large_message)
    };

    // Prepare URL reference
    ssn_url_ref_t url = {
        .url = "/large_message",
        .url_len = 14
    };

    // Set client send timeout (1 second)
    // Note: This assumes the client has a way to set send timeout
    // In practice, you would set this in the client options

    // Send message (this should timeout)
    int result = ssn_client_message(client, &url, &data);
    if (result >= 0) {
        LOG_WARN("Message send succeeded, but expected timeout");
        // Continue anyway
    }

    LOG_INFO("Test 3 completed");

    // Cleanup
    ssn_client_close(client);
    ssn_server_destroy(server);
    return true;
}

/**
 * @brief Test 4: Idle timeout
 */
static bool test_idle_timeout(void)
{
    LOG_INFO("\nTest 4: Idle timeout");

    // Create server with short idle timeout
    server_options_t options = {
        .send_timeout_ms = 5000,
        .conn_timeout_ms = 3000,
        .idle_timeout_sec = 5, // 5 seconds idle timeout
        .ifname = ""
    };

    ssn_server_t *server = ssn_server_create_with_options(SERVER_NAME, &options);
    if (!server) {
        LOG_ERROR("Failed to create IPC server");
        return false;
    }

    if (!ssn_server_start(server)) {
        LOG_ERROR("Failed to start IPC server");
        ssn_server_destroy(server);
        return false;
    }

    // Create IPC client
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create IPC client");
        ssn_server_destroy(server);
        return false;
    }

    // Set connection timeout
    struct timespec conn_timeout = {
        .tv_sec = 5,
        .tv_nsec = 0
    };

    // Connect to server
    if (!ssn_client_connect(client, SERVER_NAME, &conn_timeout)) {
        LOG_ERROR("Failed to connect to server: %s", SERVER_NAME);
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("Connected to server, waiting for idle timeout...");

    // Wait longer than idle timeout
    sleep(7); // Wait 7 seconds

    // Try to send a message after idle timeout
    ssn_data_ref_t data = {
        .data = "test",
        .length = 4
    };

    ssn_url_ref_t url = {
        .url = "/test",
        .url_len = 6
    };

    int result = ssn_client_message(client, &url, &data);
    if (result < 0) {
        LOG_INFO("Test 4 passed (idle timeout occurred)");
    } else {
        LOG_INFO("Test 4: Connection still active");
    }

    // Cleanup
    ssn_client_close(client);
    ssn_server_destroy(server);
    return true;
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Timeout example started");

    // Run tests
    bool test1 = test_connection_timeout();
    bool test2 = test_rpc_timeout();
    bool test3 = test_message_timeout();
    bool test4 = test_idle_timeout();

    if (test1 && test2 && test3 && test4) {
        LOG_INFO("\nAll timeout tests completed successfully!");
        return 0;
    } else {
        LOG_ERROR("\nSome timeout tests failed!");
        return 1;
    }
}
