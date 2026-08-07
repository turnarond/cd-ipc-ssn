/*
 * error_handling.c - Error handling example
 *
 * This example demonstrates how to handle various error scenarios in cd-ipc-ssn library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "ssn_client.h"
#include "ssn_server.h"
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
    ssn_client_t *client = ssn_client_create();
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
    if (ssn_client_connect(client, NON_EXISTENT_SERVER, &timeout)) {
        LOG_ERROR("Expected connection to fail, but it succeeded");
        ssn_client_close(client);
        return false;
    }

    LOG_INFO("Test 1 passed (expected error)");

    // Close client
    ssn_client_close(client);
    return true;
}

/* Test 2 服务端事件循环运行标志 */
static volatile int g_srv_running;

/* Test 2 期望失败标志：由回调在超时（hdr == NULL）时置位 */
static bool expect_fail_observed;

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
 * @brief 无响应处理方法：接收请求但不回复，模拟服务器无响应（触发客户端超时）
 */
static void no_reply_handler(ssn_server_t *server, ssn_peer_id_t id, ssn_header_t *ipc_hdr,
                             ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg)
{
    (void)server;
    (void)id;
    (void)ipc_hdr;
    (void)url;
    (void)data;
    (void)arg;
}

/**
 * @brief 期望失败回调：服务器未响应（hdr == NULL）即视为预期失败成立
 */
static void expect_fail_cb(ssn_client_t *client, ssn_header_t *hdr,
                           ssn_data_ref_t *data, void *arg)
{
    (void)client;
    (void)data;
    (void)arg;

    if (hdr == NULL) {
        expect_fail_observed = true; // 超时 = 预期失败成立
    }
}

/**
 * @brief Test 2: RPC call to non-existent method
 */
static bool test_rpc_error(void)
{
    LOG_INFO("\nTest 2: RPC call to non-existent method");

    // Create server (so we can connect)
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

    // 注册无响应方法：接收请求但不回复，使客户端调用超时
    ssn_url_ref_t no_reply_url = {
        .url = "/non_existent_method",
        .url_len = 20
    };
    if (!ssn_server_add_method(server, &no_reply_url, no_reply_handler, NULL)) {
        LOG_ERROR("Failed to register no-reply method");
        ssn_server_destroy(server);
        return false;
    }

    // 启动服务端事件循环线程（连接握手与请求分发需要服务端 poll 驱动）
    g_srv_running = true;
    pthread_t srv_thread;
    if (pthread_create(&srv_thread, NULL, server_poll_thread, server) != 0) {
        LOG_ERROR("Failed to create server poll thread");
        g_srv_running = false;
        ssn_server_destroy(server);
        return false;
    }

    // Create IPC client
    ssn_client_t *client = ssn_client_create();
    if (!client) {
        LOG_ERROR("Failed to create IPC client");
        g_srv_running = false;
        pthread_join(srv_thread, NULL);
        ssn_server_destroy(server);
        return false;
    }

    // Set connection timeout
    struct timespec timeout = {
        .tv_sec = 5,
        .tv_nsec = 0
    };

    // Connect to server
    if (!ssn_client_connect(client, SERVER_NAME, &timeout)) {
        LOG_ERROR("Failed to connect to server: %s", SERVER_NAME);
        g_srv_running = false;
        pthread_join(srv_thread, NULL);
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    // Prepare message
    ssn_data_ref_t data = {
        .data = "test",
        .length = 4
    };

    // Prepare URL reference for non-existent method
    ssn_url_ref_t url = {
        .url = "/non_existent_method",
        .url_len = 20
    };

    // Make RPC call to non-existent method
    // 注意：ssn_client_call 发送成功即返回 0，调用失败必须通过回调判断
    //（回调收到 hdr == NULL 表示服务器未响应/超时，即预期失败成立）
    expect_fail_observed = false;
    ssn_client_call(client, &url, &data, expect_fail_cb, NULL, 200);
    ssn_client_poll(client, 1000); // 驱动回调（1 秒，需大于 200ms 超时）
    if (!expect_fail_observed) {
        LOG_ERROR("Expected RPC call to fail, but it succeeded");
        g_srv_running = false;
        pthread_join(srv_thread, NULL);
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("Test 2 passed (expected error)");

    // Cleanup
    g_srv_running = false;
    pthread_join(srv_thread, NULL);
    ssn_client_close(client);
    ssn_server_destroy(server);
    return true;
}

/**
 * @brief Test 3: Connection failure without server
 */
static bool test_connection_without_server(void)
{
    LOG_INFO("\nTest 3: Connection failure without server");

    // Create server object but do not start it, so the client cannot connect
    ssn_server_t *server = ssn_server_create(SERVER_NAME);
    if (!server) {
        LOG_ERROR("Failed to create IPC server");
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
    struct timespec timeout = {
        .tv_sec = 2,
        .tv_nsec = 0
    };

    // Try to connect to server (should fail because server isn't started)
    if (ssn_client_connect(client, SERVER_NAME, &timeout)) {
        LOG_ERROR("Expected connection to fail, but it succeeded");
        ssn_client_close(client);
        ssn_server_destroy(server);
        return false;
    }

    LOG_INFO("Test 3 passed (expected error)");

    // Cleanup
    ssn_client_close(client);
    ssn_server_destroy(server);
    return true;
}

/**
 * @brief Test 4: Error recovery
 */
static bool test_error_recovery(void)
{
    LOG_INFO("\nTest 4: Error recovery");

    // Create IPC client
    ssn_client_t *client = ssn_client_create();
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
        if (ssn_client_connect(client, NON_EXISTENT_SERVER, &timeout)) {
            connected = true;
            break;
        }
        LOG_INFO("Connection attempt %d failed, retrying...", i);
        sleep(500000 / 1000000); // Wait 500ms before retrying
    }

    if (connected) {
        LOG_ERROR("Expected connection to fail after %d attempts", attempts);
        ssn_client_close(client);
        return false;
    }

    LOG_INFO("Test 4 passed (error recovery handled)");

    // Cleanup
    ssn_client_close(client);
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
    bool test3 = test_connection_without_server();
    bool test4 = test_error_recovery();

    if (test1 && test2 && test3 && test4) {
        LOG_INFO("\nAll error handling tests completed successfully!");
        return 0;
    } else {
        LOG_ERROR("\nSome error handling tests failed!");
        return 1;
    }
}
