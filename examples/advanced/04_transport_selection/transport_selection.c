/*
 * transport_selection.c - Transport protocol selection example
 *
 * This example demonstrates how to use different transport protocols in cd-ipc-ssn library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "ssn_client.h"
#include "ssn_server.h"
#include "util/ssn_log.h"

#define UNIX_SOCKET_SERVER "unix:///tmp/unix_socket_server"
#define TCP_SERVER "tcp://127.0.0.1:8888"
#define UDP_SERVER "udp://127.0.0.1:9999"

/* 服务端事件循环运行标志 */
static volatile int g_srv_running;

/**
 * @brief 服务端事件循环线程：驱动 ssn_server_poll 处理连接与请求
 */
static void *server_poll_thread(void *arg)
{
    ssn_server_t *srv = (ssn_server_t *)arg;

    while (g_srv_running) {
        ssn_server_poll(srv, 100);
    }
    return NULL;
}

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

    // 启动服务端事件循环线程（连接握手需要服务端 poll 驱动）
    g_srv_running = true;
    pthread_t srv_thread;
    if (pthread_create(&srv_thread, NULL, server_poll_thread, server) != 0) {
        LOG_ERROR("Failed to create server poll thread");
        g_srv_running = false;
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("Unix Socket server started on %s", UNIX_SOCKET_SERVER);

    // Create client
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create Unix Socket client");
        g_srv_running = false;
        pthread_join(srv_thread, NULL);
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
        g_srv_running = false;
        pthread_join(srv_thread, NULL);
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
        .url_len = 5
    };

    if (ssn_client_message(client, &url, &data) < 0) {
        LOG_ERROR("Failed to send message via Unix Socket");
        g_srv_running = false;
        pthread_join(srv_thread, NULL);
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("Message sent via Unix Socket");

    // Wait for message to be received
    sleep(2);

    // Cleanup
    g_srv_running = false;
    pthread_join(srv_thread, NULL);
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

    // 启动服务端事件循环线程（连接握手需要服务端 poll 驱动）
    g_srv_running = true;
    pthread_t srv_thread;
    if (pthread_create(&srv_thread, NULL, server_poll_thread, server) != 0) {
        LOG_ERROR("Failed to create server poll thread");
        g_srv_running = false;
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("TCP server started on %s", TCP_SERVER);

    // Create client
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create TCP client");
        g_srv_running = false;
        pthread_join(srv_thread, NULL);
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
        g_srv_running = false;
        pthread_join(srv_thread, NULL);
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("TCP client connected");

    // Send message
    ssn_data_ref_t data = {
        .data = "Hello via TCP",
        .length = 13
    };

    ssn_url_ref_t url = {
        .url = "/test",
        .url_len = 5
    };

    if (ssn_client_message(client, &url, &data) < 0) {
        LOG_ERROR("Failed to send message via TCP");
        g_srv_running = false;
        pthread_join(srv_thread, NULL);
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("Message sent via TCP");

    // Wait for message to be received
    sleep(2);

    // Cleanup
    g_srv_running = false;
    pthread_join(srv_thread, NULL);
    ssn_client_close(client);
    ssn_server_destroy(server);

    LOG_INFO("Test 2 passed");
    return true;
}

/**
 * @brief Test UDP transport（框架限制演示）
 *
 * UDP 为无连接传输，框架不支持 UDP server 模式握手（udp_transport_accept
 * 恒返回 NULL，见 src/transports/ssn_transport_udp.c 与传输层设计文档限制标注），
 * ssn_server 无法运行于 UDP 之上；客户端 connect 依赖服务端握手应答，
 * 因此连接必然在接收超时后失败。本测试演示该框架限制：连接失败即符合预期。
 */
static bool test_udp(void)
{
    LOG_INFO("\nTest 3: UDP transport（限制演示：客户端连接必然失败）");

    // 不创建 ssn_server——UDP 不支持 server 模式（无 accept/握手）
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create UDP client");
        return false;
    }

    // Connect to server (expected to fail: UDP 无 server 握手)
    struct timespec timeout = {
        .tv_sec = 6,
        .tv_nsec = 0
    };

    if (ssn_client_connect(client, UDP_SERVER, &timeout)) {
        LOG_ERROR("Unexpected: UDP client connected (framework limit should prevent this)");
        ssn_client_close(client);
        return false;
    }

    LOG_INFO("UDP client connect failed as expected (UDP 不支持 server 模式握手，框架限制)");

    // Cleanup
    ssn_client_close(client);

    LOG_INFO("Test 3 passed (UDP limitation demonstrated)");
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
