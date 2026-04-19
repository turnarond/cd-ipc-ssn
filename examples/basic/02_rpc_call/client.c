/*
 * client.c - RPC client example
 *
 * This example demonstrates how to create an IPC client that calls RPC methods on a server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cd_ipc_client.h"
#include "util/ssn_log.h"

#define SERVER_NAME "/tmp/rpc_server"

/**
 * @brief RPC reply handler
 * 
 * This function is called when an RPC response is received from the server.
 * 
 * @param client IPC client instance
 * @param hdr IPC header
 * @param data Data reference
 * @param arg User argument
 */
static void rpc_reply_handler(ipc_client_t *client, ipc_header_t *hdr, 
                            ipc_data_ref_t *data, void *arg)
{
    (void)client;
    (void)hdr;

    int *success = (int *)arg;

    if (data) {
        LOG_INFO("RPC call successful, result: %.*s", 
                 (int)data->length, (const char*)data->data);
        *success = 1;
    } else {
        LOG_ERROR("RPC call failed");
        *success = 0;
    }
}

/**
 * @brief Make an RPC call
 * 
 * @param client IPC client instance
 * @param url RPC method URL
 * @param params RPC parameters
 * @return true if call was successful, false otherwise
 */
bool make_rpc_call(ipc_client_t *client, const char *url, const char *params)
{
    int success = 0;

    // Prepare URL reference
    ipc_url_ref_t url_ref = {
        .url = (char*)url,
        .url_len = strlen(url)
    };

    // Prepare data reference
    ipc_data_ref_t data_ref = {
        .data = (void*)params,
        .length = strlen(params)
    };

    // Make RPC call
    if (ipc_client_call(client, &url_ref, &data_ref, 
                       rpc_reply_handler, &success, 5000) < 0) {
        LOG_ERROR("Failed to make RPC call: %s", url);
        return false;
    }

    // Wait for response
    int timeout = 5; // 5 seconds
    while (timeout > 0 && !success) {
        ipc_client_poll(client, 100);
        sleep(1);
        timeout--;
    }

    return success;
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Starting RPC client...");

    // Create IPC client
    ipc_client_t *client = ipc_client_create(NULL, NULL);
    if (!client) {
        LOG_ERROR("Failed to create IPC client");
        return 1;
    }

    LOG_INFO("RPC client created successfully");

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

    LOG_INFO("Connected to server: %s", SERVER_NAME);

    // Test RPC methods
    LOG_INFO("Testing RPC methods...");

    // Test add method
    LOG_INFO("\nTesting /math/add");
    make_rpc_call(client, "/math/add", "{\"a\": 5, \"b\": 3}");

    // Test subtract method
    LOG_INFO("\nTesting /math/subtract");
    make_rpc_call(client, "/math/subtract", "{\"a\": 10, \"b\": 4}");

    // Test multiply method
    LOG_INFO("\nTesting /math/multiply");
    make_rpc_call(client, "/math/multiply", "{\"a\": 6, \"b\": 7}");

    // Test divide method
    LOG_INFO("\nTesting /math/divide");
    make_rpc_call(client, "/math/divide", "{\"a\": 20, \"b\": 4}");

    // Test divide by zero (should return error)
    LOG_INFO("\nTesting /math/divide (division by zero)");
    make_rpc_call(client, "/math/divide", "{\"a\": 10, \"b\": 0}");

    // Wait for all responses
    LOG_INFO("\nWaiting for all responses...");
    sleep(2);

    // Disconnect from server
    ipc_client_disconnect(client);

    // Close the client
    ipc_client_close(client);

    LOG_INFO("RPC client closed");

    return 0;
}
