/*
 * server.c - Multithreaded IPC server example
 *
 * This example demonstrates an IPC server with a dedicated event-loop thread
 * that handles multiple concurrent connections and RPC calls.
 *
 * 注意：ssn_server 为单线程事件循环模型（ssn_server_poll 驱动）。多个线程
 * 并发 poll 同一 server 不受支持（会并发 accept/recv 同一套接字造成竞态、
 * 阻塞与崩溃），因此服务端使用单个事件循环线程；多线程并发能力由客户端
 * 多线程 RPC 调用演示（见同目录 client.c）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "ssn_server.h"
#include "util/ssn_log.h"
#include "util/ssn_mutex.h"

#define SERVER_NAME "unix:///tmp/multithread_server"

/* 服务端事件循环停止标志：主线程在销毁服务器前置位，使 poll 线程正常退出 */
static volatile int g_server_running = 1;

/**
 * @brief Thread data structure
 */
typedef struct {
    ssn_server_t *server;
    ssn_peer_id_t client_id;
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
static void echo_handler(ssn_server_t *server, ssn_peer_id_t id, ssn_header_t *hdr,
                        ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg)
{
    (void)url;
    (void)arg;

    // Get thread ID from argument
    int thread_id = *((int*)arg);
    LOG_INFO("Thread %d: RPC method /echo called with: %.*s",
             thread_id, (int)data->length, (const char*)data->data);

    // Prepare response (echo back the message)
    ssn_data_ref_t resp_data = {
        .data = data->data,
        .length = data->length
    };

    // Send response
    ssn_server_response(server, id, 0, ssn_get_seqno(hdr), &resp_data);
}

/**
 * @brief Connection handler callback
 *
 * @param server IPC server instance
 * @param id Client ID
 * @param connect True if client connected, false if disconnected
 * @param arg User argument
 */
static void connect_handler(ssn_server_t *server, ssn_peer_id_t id, bool connect, void *arg)
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
    ssn_server_t *server = (ssn_server_t *)arg;
    int thread_id = (int)(intptr_t)pthread_self() % 1000;

    LOG_INFO("Server thread %d started", thread_id);

    // Run server loop (退出条件由主线程置位 g_server_running 控制)
    while (g_server_running) {
        // Poll for events with timeout
        if (ssn_server_poll(server, 1000) < 0) {
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
    ssn_server_t *server = ssn_server_create_with_options(SERVER_NAME, &options);
    if (!server) {
        LOG_ERROR("Failed to create IPC server");
        return 1;
    }

    LOG_INFO("Multithreaded server created successfully");

    // Set connection handler
    ssn_server_set_connect_handler(server, connect_handler, NULL);

    // Register RPC method with thread-specific argument (只注册一次)
    ssn_url_ref_t echo_url = {.url = "/echo", .url_len = 5};
    static int thread_id = 1;
    if (!ssn_server_add_method(server, &echo_url, echo_handler, &thread_id)) {
        LOG_ERROR("Failed to register RPC method");
        ssn_server_destroy(server);
        return 1;
    }

    LOG_INFO("RPC methods registered successfully");

    // Start the server
    if (!ssn_server_start(server)) {
        LOG_ERROR("Failed to start IPC server");
        ssn_server_destroy(server);
        return 1;
    }

    LOG_INFO("Multithreaded server started on %s", SERVER_NAME);

    // Create server event-loop thread (单个事件循环线程：ssn_server 不支持多线程并发 poll)
    pthread_t thread;
    if (pthread_create(&thread, NULL, server_thread, server) != 0) {
        LOG_ERROR("Failed to create server thread");
        ssn_server_destroy(server);
        return 1;
    }

    // Run for 20 seconds
    LOG_INFO("Server running for 20 seconds...");
    sleep(20);

    // Stop the server event loop thread (置位停止标志后 join)
    LOG_INFO("Stopping multithreaded server...");
    g_server_running = 0;

    // Wait for the thread to exit
    pthread_join(thread, NULL);

    LOG_INFO("Multithreaded server stopped");

    // Destroy the server
    ssn_server_destroy(server);

    LOG_INFO("Multithreaded server destroyed");

    return 0;
}
