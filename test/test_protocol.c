/*
 * Protocol Layer Unit Tests
 */

#include "protocol/ssn_protocol.h"
#include "protocol/rpc/ssn_rpc.h"
#include "protocol/pubsub/ssn_pubsub.h"
#include "protocol/msg/ssn_msg.h"
#include "transports/ssn_transport.h"
#include "util/ssn_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define TEST_BUFFER_SIZE 1024
#define TEST_TIMEOUT_MS 2000

static int g_tests_passed = 0;
static int g_tests_failed = 0;
static bool g_rpc_reply_received = false;
static bool g_msg_received = false;
static bool g_pubsub_message_received = false;
static char g_received_topic[64] = {0};
static char g_received_data[TEST_BUFFER_SIZE] = {0};

#define ASSERT(cond, msg) \
    do { \
        if (cond) { \
            g_tests_passed++; \
            printf("[PASS] %s\n", msg); \
        } else { \
            g_tests_failed++; \
            printf("[FAIL] %s\n", msg); \
        } \
    } while (0)

/* RPC测试回调函数 */
static void rpc_request_handler(uint16_t seqno, const char *method, 
                                const void *data, size_t data_len, 
                                void *arg)
{
    ssn_rpc_rep_t *rep = (ssn_rpc_rep_t *)arg;
    if (rep) {
        ssn_rpc_response(rep, seqno, 0, data, data_len);
    }
}

static void rpc_reply_handler(uint16_t seqno, uint32_t status, 
                             const void *data, size_t data_len, 
                             void *arg)
{
    g_rpc_reply_received = true;
    if (data && data_len > 0) {
        strncpy(g_received_data, (const char *)data, sizeof(g_received_data) - 1);
    }
}

/* 消息测试回调函数 */
static void msg_handler(const void *data, size_t data_len, void *arg)
{
    g_msg_received = true;
    if (data && data_len > 0) {
        strncpy(g_received_data, (const char *)data, sizeof(g_received_data) - 1);
    }
}

/* 发布订阅测试回调函数 */
static void pubsub_msg_handler(const char *topic, const void *data, 
                              size_t data_len, void *arg)
{
    g_pubsub_message_received = true;
    if (topic) {
        strncpy(g_received_topic, topic, sizeof(g_received_topic) - 1);
    }
    if (data && data_len > 0) {
        strncpy(g_received_data, (const char *)data, sizeof(g_received_data) - 1);
    }
}

static void test_protocol_create_destroy(void)
{
    // 测试RPC请求端创建销毁
    ssn_rpc_req_t *rpc_req = ssn_rpc_req_create(rpc_reply_handler, NULL);
    ASSERT(rpc_req != NULL, "Create RPC requester");
    if (rpc_req) {
        ssn_rpc_destroy((ssn_protocol_ctx_t *)rpc_req);
    }

    // 测试RPC应答端创建销毁
    ssn_rpc_rep_t *rpc_rep = ssn_rpc_rep_create(rpc_request_handler, NULL);
    ASSERT(rpc_rep != NULL, "Create RPC replier");
    if (rpc_rep) {
        ssn_rpc_destroy((ssn_protocol_ctx_t *)rpc_rep);
    }

    // 测试发布端创建销毁
    ssn_pubsub_pub_t *pub = ssn_pubsub_pub_create();
    ASSERT(pub != NULL, "Create publisher");
    if (pub) {
        ssn_pubsub_destroy((ssn_protocol_ctx_t *)pub);
    }

    // 测试订阅端创建销毁
    ssn_pubsub_sub_t *sub = ssn_pubsub_sub_create(pubsub_msg_handler, NULL);
    ASSERT(sub != NULL, "Create subscriber");
    if (sub) {
        ssn_pubsub_destroy((ssn_protocol_ctx_t *)sub);
    }

    // 测试消息发送端创建销毁
    ssn_msg_send_t *msg_send = ssn_msg_send_create();
    ASSERT(msg_send != NULL, "Create message sender");
    if (msg_send) {
        ssn_msg_destroy((ssn_protocol_ctx_t *)msg_send);
    }

    // 测试消息接收端创建销毁
    ssn_msg_recv_t *msg_recv = ssn_msg_recv_create(msg_handler, NULL);
    ASSERT(msg_recv != NULL, "Create message receiver");
    if (msg_recv) {
        ssn_msg_destroy((ssn_protocol_ctx_t *)msg_recv);
    }
}

static void test_rpc_method_register(void)
{
    ssn_rpc_rep_t *rep = ssn_rpc_rep_create(rpc_request_handler, NULL);
    if (rep) {
        int result = ssn_rpc_register(rep, "test_method", rpc_request_handler, rep);
        ASSERT(result == 0, "Register RPC method");
        
        result = ssn_rpc_unregister(rep, "test_method");
        ASSERT(result == 0, "Unregister RPC method");
        
        ssn_rpc_destroy((ssn_protocol_ctx_t *)rep);
    }
}

static void test_pubsub_subscribe(void)
{
    ssn_pubsub_sub_t *sub = ssn_pubsub_sub_create(pubsub_msg_handler, NULL);
    if (sub) {
        // 注意：这里只是测试subscribe函数，实际需要transport才能成功
        int result = ssn_pubsub_sub_subscribe(sub, "test_topic");
        // 因为没有transport，应该失败
        ASSERT(result == -1, "Subscribe without transport");
        
        ssn_pubsub_destroy((ssn_protocol_ctx_t *)sub);
    }
}

static void test_msg_send(void)
{
    ssn_msg_send_t *send = ssn_msg_send_create();
    if (send) {
        // 注意：这里只是测试send函数，实际需要transport才能成功
        const char *test_data = "Test message";
        int result = ssn_msg_send(send, test_data, strlen(test_data));
        // 因为没有transport，应该失败
        ASSERT(result == -1, "Send message without transport");
        
        ssn_msg_destroy((ssn_protocol_ctx_t *)send);
    }
}

static void test_protocol_type_role(void)
{
    ssn_rpc_req_t *rpc_req = ssn_rpc_req_create(rpc_reply_handler, NULL);
    if (rpc_req) {
        ssn_protocol_type_t type = ssn_protocol_get_type((ssn_protocol_ctx_t *)rpc_req);
        ssn_role_t role = ssn_protocol_get_role((ssn_protocol_ctx_t *)rpc_req);
        ASSERT(type == SSN_PROTOCOL_RPC, "RPC protocol type");
        ASSERT(role == SSN_ROLE_REQ, "RPC requester role");
        ssn_rpc_destroy((ssn_protocol_ctx_t *)rpc_req);
    }

    ssn_pubsub_pub_t *pub = ssn_pubsub_pub_create();
    if (pub) {
        ssn_protocol_type_t type = ssn_protocol_get_type((ssn_protocol_ctx_t *)pub);
        ssn_role_t role = ssn_protocol_get_role((ssn_protocol_ctx_t *)pub);
        ASSERT(type == SSN_PROTOCOL_PUBSUB, "PubSub protocol type");
        ASSERT(role == SSN_ROLE_PUB, "Publisher role");
        ssn_pubsub_destroy((ssn_protocol_ctx_t *)pub);
    }

    ssn_msg_send_t *msg_send = ssn_msg_send_create();
    if (msg_send) {
        ssn_protocol_type_t type = ssn_protocol_get_type((ssn_protocol_ctx_t *)msg_send);
        ssn_role_t role = ssn_protocol_get_role((ssn_protocol_ctx_t *)msg_send);
        ASSERT(type == SSN_PROTOCOL_MSG, "Message protocol type");
        ASSERT(role == SSN_ROLE_SEND, "Message sender role");
        ssn_msg_destroy((ssn_protocol_ctx_t *)msg_send);
    }
}

static void test_protocol_is_connected(void)
{
    ssn_rpc_req_t *rpc_req = ssn_rpc_req_create(rpc_reply_handler, NULL);
    if (rpc_req) {
        bool connected = ssn_rpc_is_connected((ssn_protocol_ctx_t *)rpc_req);
        ASSERT(connected == false, "RPC not connected");
        ssn_rpc_destroy((ssn_protocol_ctx_t *)rpc_req);
    }

    ssn_pubsub_sub_t *sub = ssn_pubsub_sub_create(pubsub_msg_handler, NULL);
    if (sub) {
        bool connected = ssn_pubsub_is_connected((ssn_protocol_ctx_t *)sub);
        ASSERT(connected == false, "PubSub not connected");
        ssn_pubsub_destroy((ssn_protocol_ctx_t *)sub);
    }

    ssn_msg_send_t *msg_send = ssn_msg_send_create();
    if (msg_send) {
        bool connected = ssn_msg_is_connected((ssn_protocol_ctx_t *)msg_send);
        ASSERT(connected == false, "Message not connected");
        ssn_msg_destroy((ssn_protocol_ctx_t *)msg_send);
    }
}

static void test_protocol_poll(void)
{
    ssn_rpc_req_t *rpc_req = ssn_rpc_req_create(rpc_reply_handler, NULL);
    if (rpc_req) {
        int result = ssn_rpc_poll((ssn_protocol_ctx_t *)rpc_req, 100);
        ASSERT(result == -1, "RPC poll without transport");
        ssn_rpc_destroy((ssn_protocol_ctx_t *)rpc_req);
    }

    ssn_pubsub_sub_t *sub = ssn_pubsub_sub_create(pubsub_msg_handler, NULL);
    if (sub) {
        int result = ssn_pubsub_poll((ssn_protocol_ctx_t *)sub, 100);
        ASSERT(result == -1, "PubSub poll without transport");
        ssn_pubsub_destroy((ssn_protocol_ctx_t *)sub);
    }

    ssn_msg_send_t *msg_send = ssn_msg_send_create();
    if (msg_send) {
        int result = ssn_msg_poll((ssn_protocol_ctx_t *)msg_send, 100);
        ASSERT(result == -1, "Message poll without transport");
        ssn_msg_destroy((ssn_protocol_ctx_t *)msg_send);
    }
}

static void test_protocol_bind_connect(void)
{
    ssn_transport_config_t config = {
        .type = SSN_TRANSPORT_TCP,
        .non_blocking = false,
        .reuse_address = true
    };

    ssn_transport_t *transport = ssn_transport_create(SSN_TRANSPORT_TCP, &config);
    if (transport) {
        // 测试RPC绑定
        ssn_rpc_rep_t *rpc_rep = ssn_rpc_rep_create(rpc_request_handler, NULL);
        if (rpc_rep) {
            int result = ssn_rpc_bind((ssn_protocol_ctx_t *)rpc_rep, transport);
            ASSERT(result == 0, "RPC bind to transport");
            ssn_rpc_destroy((ssn_protocol_ctx_t *)rpc_rep);
        }

        // 测试消息接收端绑定
        ssn_msg_recv_t *msg_recv = ssn_msg_recv_create(msg_handler, NULL);
        if (msg_recv) {
            int result = ssn_msg_recv_bind(msg_recv, transport);
            ASSERT(result == 0, "Message receiver bind to transport");
            ssn_msg_destroy((ssn_protocol_ctx_t *)msg_recv);
        }

        // 测试发布端绑定
        ssn_pubsub_pub_t *pub = ssn_pubsub_pub_create();
        if (pub) {
            int result = ssn_pubsub_pub_bind(pub, transport);
            ASSERT(result == 0, "Publisher bind to transport");
            ssn_pubsub_destroy((ssn_protocol_ctx_t *)pub);
        }

        ssn_transport_destroy(transport);
    }
}

static void test_rpc_integration(void)
{
    // 简化集成测试，只测试基本功能
    printf("[INFO] RPC integration test skipped for now\n");
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    printf("========================================\n");
    printf("SSN Protocol Layer Unit Tests\n");
    printf("========================================\n\n");

    printf("Running protocol creation tests...\n");
    test_protocol_create_destroy();

    printf("\nRunning RPC method register tests...\n");
    test_rpc_method_register();

    printf("\nRunning PubSub subscribe tests...\n");
    test_pubsub_subscribe();

    printf("\nRunning message send tests...\n");
    test_msg_send();

    printf("\nRunning protocol type/role tests...\n");
    test_protocol_type_role();

    printf("\nRunning protocol is_connected tests...\n");
    test_protocol_is_connected();

    printf("\nRunning protocol poll tests...\n");
    test_protocol_poll();

    printf("\nRunning protocol bind/connect tests...\n");
    test_protocol_bind_connect();

    printf("\nRunning RPC integration test...\n");
    test_rpc_integration();

    printf("\n========================================\n");
    printf("Test Results: %d passed, %d failed\n",
           g_tests_passed, g_tests_failed);
    printf("========================================\n");

    return g_tests_failed > 0 ? 1 : 0;
}
