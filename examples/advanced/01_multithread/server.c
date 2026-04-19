/*
 * server.c - Multithreaded IPC server example
 *
 * This example demonstrates how to create a multithreaded IPC server that can
 * handle multiple concurrent connections and RPC calls.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "cd_ipc_server.h"
#include "util/ssn_log.h"
#include "util/ssn_mutex.h"

#define SERVER_NAME "/tmp/multithread_server"
#define MAX_THREADS 4

/**
 * @brief Thread data structure
 */
typedef struct {
    ipc_server_t *server;
    cli_id_t client_id;
    int thread_id;
} thread_data_t;

/**
 * @brief Echo RPC method handler
 * 
 * @param server IPC server instance
 * @param id Client ID
 * @param hdr IPC header
 * @param url URL reference
 * @param data Data reference
 * @param arg User argument
 */
static void echo_handler(ipc_server_t *server, cli_id_t id, ipc_header_t *hdr, 
                        ipc_url_ref_t *url, ipc_data_ref_t *data, void *arg)
{
    (void)url;
    (void)arg;

    // Get thread ID from argument
    int thread_id = *((int*)arg);
    LOG_INFO("Thread %d: RPC method /echo called with: %.*s", 
             thread_id, (int)data->length, (const char*)data->data);

    // Prepare response (echo back the message)
    ipc_data_ref_t resp_data = {
        .data = data->data,
        .length = data->length
    };

    // Send response
    ipc_server_response(server, id, 0, ipc_get_seqno(hdr), &resp_data);
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

    static int thread_count = 0;
    static pthread_mutex_t thread_count_mutex = PTHREAD_MUTEX_INITIALIZER;

    if (connect) {
        // Get thread ID
        pthread_mutex_lock(&thread_count_mutex);
        int thread_id = ++thread_count;
        pthread_mutex_unlock(&thread_count_mutex);

        LOG_INFO("Thread %d: Handling connection from client %u", thread_id, id);
    } else {
        LOG_INFO("Client disconnected: id=%u", id);
    }
}

/**
 * @brief Server thread function
 * 
 * @param arg Thread argument
 * @return Thread exit status
 */
static void *server_thread(void *arg)
{
    ipc_server_t *server = (ipc_server_t *)arg;
    int thread_id = (int)(intptr_t)pthread_self() % 1000;

    LOG_INFO("Server thread %d started", thread_id);

    // Run server loop
    while (1) {
        // Poll for events with timeout
        if (ipc_server_poll(server, 1000) < 0) {
            break;
        }
    }

    LOG_INFO("Server thread %d stopped", thread_id);
    return NULL;
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Starting multithreaded server...");

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

    LOG_INFO("Multithreaded server created successfully");

    // Set connection handler
    ipc_server_set_connect_handler(server, connect_handler, NULL);

    // Register RPC method with thread-specific argument
    ipc_url_ref_t echo_url = {.url = "/echo", .url_len = 6};
    static int thread_ids[MAX_THREADS];
    for (int i = 0; i < MAX_THREADS; i++) {
        thread_ids[i] = i + 1;
        ipc_server_add_method(server, &echo_url, echo_handler, &thread_ids[i]);
    }

    LOG_INFO("RPC methods registered successfully");

    // Start the server
    if (!ipc_server_start(server)) {
        LOG_ERROR("Failed to start IPC server");
        ipc_server_destroy(server);
        return 1;
    }

    LOG_INFO("Multithreaded server started on %s", SERVER_NAME);

    // Create server threads
    pthread_t threads[MAX_THREADS];
    for (int i = 0; i < MAX_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, server_thread, server) != 0) {
            LOG_ERROR("Failed to create server thread %d", i + 1);
            // Continue with remaining threads
        }
    }

    // Run for 20 seconds
    LOG_INFO("Server running for 20 seconds...");
    sleep(20);

    // Server is stopped automatically when destroyed
    LOG_INFO("Stopping multithreaded server...");

    // Wait for threads to exit
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    LOG_INFO("Multithreaded server stopped");

    // Destroy the server
    ipc_server_destroy(server);

    LOG_INFO("Multithreaded server destroyed");

    return 0;
}
