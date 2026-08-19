/*
 * client.c - Multithreaded IPC client example
 *
 * This example demonstrates how to create a multithreaded IPC client that can
 * make multiple concurrent RPC calls to a server.
 */

#define _XOPEN_SOURCE 500 /* 启用 usleep 等 POSIX 接口（-std=c99 下需显式特性宏） */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "ssn_client.h"
#include "util/ssn_log.h"
#include "util/ssn_mutex.h"

#define SERVER_NAME "unix:///tmp/multithread_server"
#define NUM_THREADS 2

/**
 * @brief Thread data structure
 */
typedef struct {
    ssn_client_t *client;
    int thread_id;
} thread_data_t;

/**
 * @brief RPC reply handler
 *
 * @param client IPC client instance
 * @param hdr IPC header
 * @param data Data reference
 * @param arg User argument
 */
static void rpc_reply_handler(ssn_client_t *client, ssn_header_t *hdr,
                            ssn_data_ref_t *data, void *arg)
{
    (void)client;
    (void)hdr;

    thread_data_t *thread_data = (thread_data_t *)arg;

    if (data) {
        LOG_INFO("Thread %d: RPC call successful: %.*s",
                 thread_data->thread_id, (int)data->length, (const char*)data->data);
    } else {
        LOG_ERROR("Thread %d: RPC call failed", thread_data->thread_id);
    }
}

/**
 * @brief Client thread function
 *
 * @param arg Thread argument
 * @return Thread exit status
 */
static void *client_thread(void *arg)
{
    thread_data_t *thread_data = (thread_data_t *)arg;
    ssn_client_t *client = thread_data->client;
    int thread_id = thread_data->thread_id;

    LOG_INFO("Client thread %d started", thread_id);

    // Wait a bit to ensure all threads are started
    struct timespec delay = { 0, 100000000 * thread_id }; /* 线程 1/2 分别延迟 0.1/0.2 秒，错开调用时机 */
    nanosleep(&delay, NULL);

    // Prepare message
    char message[64];
    snprintf(message, sizeof(message), "Hello from thread %d", thread_id);

    ssn_data_ref_t data = {
        .data = message,
        .length = strlen(message)
    };

    // Prepare URL reference
    ssn_url_ref_t url = {
        .url = "/echo",
        .url_len = 5
    };

    // Make RPC call
    if (ssn_client_call(client, &url, &data, rpc_reply_handler, thread_data, 5000) < 0) {
        LOG_ERROR("Thread %d: Failed to make RPC call", thread_id);
        return NULL;
    }

    LOG_INFO("Thread %d: RPC call sent", thread_id);

    // Wait for response
    int timeout = 5; // 5 seconds
    while (timeout > 0) {
        ssn_client_poll(client, 100);
        usleep(100000);
        timeout--;
    }

    LOG_INFO("Client thread %d stopped", thread_id);
    return NULL;
}

int main(void)
{
    // Set log level to INFO
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    LOG_INFO("Starting multithreaded client...");

    // Create IPC client
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create IPC client");
        return 1;
    }

    LOG_INFO("Multithreaded client created successfully");

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

    // Create thread data
    thread_data_t thread_data[NUM_THREADS];
    pthread_t threads[NUM_THREADS];

    // Create client threads
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].client = client;
        thread_data[i].thread_id = i + 1;

        if (pthread_create(&threads[i], NULL, client_thread, &thread_data[i]) != 0) {
            LOG_ERROR("Failed to create client thread %d", i + 1);
            // Continue with remaining threads
        }
    }

    // Wait for threads to complete
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Wait a bit more to ensure all responses are received
    sleep(2);

    // Disconnect from server
    ssn_client_disconnect(client);

    // Close the client
    ssn_client_close(client);

    LOG_INFO("Multithreaded client closed");

    return 0;
}
