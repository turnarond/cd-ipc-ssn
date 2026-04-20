/*
 * error_handling.c - Error handling example
 *
 * This example demonstrates how to handle various error scenarios in cd-ipc-ssn library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cd_ipc_client.h"
#include "cd_ipc_server.h"
#include "util/ssn_log.h"

#define SERVER_NAME "unix:///tmp/error_server"
#define NON_EXISTENT_SERVER "unix:///tmp/non_existent_server"

/**
 * @brief Test 1: Connection to non-existent server
 */
static bool test_connection_error(void)
{
    LOG_INFO("Test 1: Connection to non-existent server");

    // Create IPC client
    ipc_client_t *client = ipc_client_create(NULL, NULL);
    if (!client) {
        LOG_ERROR("Failed to create IPC client");
        return false;
    }

    // Set connection timeout (short timeout for quick failure)
    struct timespec timeout = {
        .tv_sec = 2,
        .tv_nsec = 0
    };

    // Try to connect to non-existent server
    if (ipc_client_connect(client, NON_EXISTENT_SERVER, &timeout)) {
        LOG_ERROR("Expected connection to fail, but it succeeded");
        ipc_client_close(client);
        return false;
    }

    LOG_INFO("Test 1 passed (expected error)");

    // Close client
    ipc_client_close(client);
    return true;
}

/**
 * @brief Test 2: RPC call to non-existent method
 */
static bool test_rpc_error(void)
{
    LOG_INFO("\nTest 2: RPC call to non-existent method");

    // Create server (so we can connect)
    ipc_server_t *server = ipc_server_create(SERVER_NAME);
    if (!server) {
        LOG_ERROR("Failed to create IPC server");
        return false;
    }

    if (!ipc_server_start(server)) {
        LOG_ERROR("Failed to start IPC server");
        ipc_server_destroy(server);
        return false;
    }

    // Create IPC client
    ipc_client_t *client = ipc_client_create(NULL, NULL);
    if (!client) {
        LOG_ERROR("Failed to create IPC client");
        ipc_server_destroy(server);
        return false;
    }

    // Set connection timeout
    struct timespec timeout = {
        .tv_sec = 5,
        .tv_nsec = 0
    };

    // Connect to server
    if (!ipc_client_connect(client, SERVER_NAME, &timeout)) {
        LOG_ERROR("Failed to connect to server: %s", SERVER_NAME);
        ipc_client_close(client);
        ipc_server_destroy(server);
        return false;
    }

    // Prepare message
    ipc_data_ref_t data = {
        .data = "test",
        .length = 4
    };

    // Prepare URL reference for non-existent method
    ipc_url_ref_t url = {
        .url = "/non_existent_method",
        .url_len = 21
    };

    // Make RPC call to non-existent method
    int result = ipc_client_call(client, &url, &data, NULL, NULL, 2000);
    if (result >= 0) {
        LOG_ERROR("Expected RPC call to fail, but it succeeded");
        ipc_client_close(client);
        ipc_server_destroy(server);
        return false;
    }

    LOG_INFO("Test 2 passed (expected error)");

    // Cleanup
    ipc_client_close(client);
    ipc_server_destroy(server);
    return true;
}

/**
 * @brief Test 3: Message send with timeout
 */
static bool test_timeout_error(void)
{
    LOG_INFO("\nTest 3: Message send with timeout");

    // Create server (but don't start it to cause timeout)
    ipc_server_t *server = ipc_server_create(SERVER_NAME);
    if (!server) {
        LOG_ERROR("Failed to create IPC server");
        return false;
    }

    // Create IPC client
    ipc_client_t *client = ipc_client_create(NULL, NULL);
    if (!client) {
        LOG_ERROR("Failed to create IPC client");
        ipc_server_destroy(server);
        return false;
    }

    // Set connection timeout
    struct timespec timeout = {
        .tv_sec = 2,
        .tv_nsec = 0
    };

    // Try to connect to server (should fail because server isn't started)
    if (ipc_client_connect(client, SERVER_NAME, &timeout)) {
        LOG_ERROR("Expected connection to fail, but it succeeded");
        ipc_client_close(client);
        ipc_server_destroy(server);
        return false;
    }

    LOG_INFO("Test 3 passed (expected error)");

    // Cleanup
    ipc_client_close(client);
    ipc_server_destroy(server);
    return true;
}

/**
 * @brief Test 4: Error recovery
 */
static bool test_error_recovery(void)
{
    LOG_INFO("\nTest 4: Error recovery");

    // Create IPC client
    ipc_client_t *client = ipc_client_create(NULL, NULL);
    if (!client) {
        LOG_ERROR("Failed to create IPC client");
        return false;
    }

    // Set connection timeout
    struct timespec timeout = {
        .tv_sec = 1,
        .tv_nsec = 0
    };

    // Try to connect to non-existent server multiple times
    int attempts = 3;
    bool connected = false;

    for (int i = 1; i <= attempts; i++) {
        LOG_INFO("Connection attempt %d/%d", i, attempts);
        if (ipc_client_connect(client, NON_EXISTENT_SERVER, &timeout)) {
            connected = true;
            break;
        }
        LOG_INFO("Connection attempt %d failed, retrying...", i);
        sleep(500000 / 1000000); // Wait 500ms before retrying
    }

    if (connected) {
        LOG_ERROR("Expected connection to fail after %d attempts", attempts);
        ipc_client_close(client);
        return false;
    }

    LOG_INFO("Test 4 passed (error recovery handled)");

    // Cleanup
    ipc_client_close(client);
    return true;
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Error handling example started");

    // Run tests
    bool test1 = test_connection_error();
    bool test2 = test_rpc_error();
    bool test3 = test_timeout_error();
    bool test4 = test_error_recovery();

    if (test1 && test2 && test3 && test4) {
        LOG_INFO("\nAll error handling tests completed successfully!");
        return 0;
    } else {
        LOG_ERROR("\nSome error handling tests failed!");
        return 1;
    }
}
