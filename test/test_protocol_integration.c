/*
 * Protocol Integration Tests
 * 完整的协议层集成测试方案
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
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>

#define TEST_BUFFER_SIZE 4096
#define RPC_TEST_PORT 9981
#define MSG_TEST_PORT 9982
#define PUBSUB_TEST_PORT 9983

static int g_tests_passed = 0;
static int g_tests_failed = 0;
static int g_integration_passed = 0;
static int g_integration_failed = 0;

typedef struct {
    sem_t sem;
    volatile int completed;
    volatile int success;
    void *data;
    size_t data_len;
} test_ctx_t;

#define ASSERT(cond, msg) \
    do { \
        if (cond) { g_tests_passed++; printf("[PASS] %s\n", msg); } \
        else { g_tests_failed++; printf("[FAIL] %s\n", msg); } \
    } while (0)

#define INT_ASSERT(cond, msg) \
    do { \
        if (cond) { g_integration_passed++; printf("[PASS] %s\n", msg); } \
        else { g_integration_failed++; printf("[FAIL] %s\n", msg); } \
    } while (0)

static void init_ctx(test_ctx_t *ctx) {
    sem_init(&ctx->sem, 0, 0);
    ctx->completed = 0;
    ctx->success = 0;
    ctx->data = NULL;
    ctx->data_len = 0;
}

static void cleanup_ctx(test_ctx_t *ctx) {
    sem_destroy(&ctx->sem);
    if (ctx->data) { free(ctx->data); ctx->data = NULL; }
}

static uint64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* ============================================================================
 * RPC 集成测试
 * ============================================================================ */

static void rpc_reply_cb(uint16_t seqno, uint32_t status, const void *data, size_t len, void *arg) {
    test_ctx_t *ctx = (test_ctx_t *)arg;
    if (ctx) {
        if (data && len > 0) {
            ctx->data = malloc(len);
            if (ctx->data) { memcpy(ctx->data, data, len); ctx->data_len = len; }
        }
        ctx->completed = 1;
        sem_post(&ctx->sem);
    }
}

static void rpc_req_handler(uint16_t seqno, const char *method, const void *data, size_t len, void *arg) {
    ssn_rpc_rep_t *rep = (ssn_rpc_rep_t *)arg;
    if (rep && method) {
        if (strcmp(method, "echo") == 0) {
            ssn_rpc_response(rep, seqno, 0, data, len);
        } else if (strcmp(method, "add") == 0) {
            int result = 0;
            if (data && len >= sizeof(int) * 2) {
                result = ((int*)data)[0] + ((int*)data)[1];
            }
            ssn_rpc_response(rep, seqno, 0, &result, sizeof(result));
        } else {
            ssn_rpc_response(rep, seqno, 1, "unknown", 7);
        }
    }
}

static void* rpc_server(void *arg) {
    (void)arg;
    ssn_transport_config_t cfg = { .type = SSN_TRANSPORT_TCP, .non_blocking = false, .reuse_address = true };
    ssn_transport_t *srv = ssn_transport_create(SSN_TRANSPORT_TCP, &cfg);
    if (!srv) return NULL;

    char addr_str[64];
    snprintf(addr_str, sizeof(addr_str), "tcp://127.0.0.1:%d", RPC_TEST_PORT);
    ssn_address_t addr;
    if (!ssn_address_parse(addr_str, &addr)) { ssn_transport_destroy(srv); return NULL; }
    if (!ssn_transport_bind(srv, &addr) || !ssn_transport_listen(srv, 1)) { ssn_transport_destroy(srv); return NULL; }

    ssn_rpc_rep_t *rep = ssn_rpc_rep_create(rpc_req_handler, NULL);
    if (rep) {
        ssn_rpc_bind((ssn_protocol_ctx_t*)rep, srv);
        ssn_address_t cli_addr;
        ssn_transport_t *cli = ssn_transport_accept(srv, &cli_addr, 5000);
        if (cli) {
            ssn_rpc_rep_t *rep2 = ssn_rpc_rep_create(rpc_req_handler, NULL);
            if (rep2) {
                rep2->base.user_data = rep2; /* pass rep2 to handler */
                ssn_rpc_bind((ssn_protocol_ctx_t*)rep2, cli);
                for (int i = 0; i < 3; i++) ssn_rpc_poll((ssn_protocol_ctx_t*)rep2, 1000);
                ssn_rpc_destroy((ssn_protocol_ctx_t*)rep2);
            }
            ssn_transport_destroy(cli);
        }
        ssn_rpc_destroy((ssn_protocol_ctx_t*)rep);
    }
    ssn_transport_destroy(srv);
    return NULL;
}

static void test_rpc_integration(void) {
    printf("\n=== RPC Integration Test ===\n");
    test_ctx_t ctx;
    init_ctx(&ctx);

    pthread_t th;
    pthread_create(&th, NULL, rpc_server, NULL);
    usleep(300000);

    ssn_transport_config_t cfg = { .type = SSN_TRANSPORT_TCP, .non_blocking = false };
    ssn_transport_t *cli = ssn_transport_create(SSN_TRANSPORT_TCP, &cfg);
    if (cli) {
        char addr_str[64];
        snprintf(addr_str, sizeof(addr_str), "tcp://127.0.0.1:%d", RPC_TEST_PORT);
        ssn_address_t addr;
        if (ssn_address_parse(addr_str, &addr) && ssn_transport_connect(cli, &addr, 2000)) {
            ssn_rpc_req_t *req = ssn_rpc_req_create(rpc_reply_cb, &ctx);
            if (req) {
                ssn_rpc_connect((ssn_protocol_ctx_t*)req, cli);

                const char *test_data = "Hello RPC!";
                int result = ssn_rpc_call(req, "echo", test_data, strlen(test_data), 2000);
                INT_ASSERT(result == 0, "RPC call sent");

                uint64_t start = get_time_ms();
                while (!ctx.completed && (get_time_ms() - start) < 3000) {
                    ssn_rpc_poll((ssn_protocol_ctx_t*)req, 100);
                    usleep(10000);
                }
                INT_ASSERT(ctx.completed, "RPC response received");

                if (ctx.data && ctx.data_len == strlen(test_data)) {
                    INT_ASSERT(memcmp(ctx.data, test_data, ctx.data_len) == 0, "RPC echo data matches");
                }

                ssn_rpc_destroy((ssn_protocol_ctx_t*)req);
            }
        }
        ssn_transport_disconnect(cli);
        ssn_transport_destroy(cli);
    }

    pthread_join(th, NULL);
    cleanup_ctx(&ctx);
}

static void test_rpc_add_integration(void) {
    printf("\n=== RPC Add Integration Test ===\n");
    test_ctx_t ctx;
    init_ctx(&ctx);

    pthread_t th;
    pthread_create(&th, NULL, rpc_server, NULL);
    usleep(300000);

    ssn_transport_config_t cfg = { .type = SSN_TRANSPORT_TCP, .non_blocking = false };
    ssn_transport_t *cli = ssn_transport_create(SSN_TRANSPORT_TCP, &cfg);
    if (cli) {
        char addr_str[64];
        snprintf(addr_str, sizeof(addr_str), "tcp://127.0.0.1:%d", RPC_TEST_PORT);
        ssn_address_t addr;
        if (ssn_address_parse(addr_str, &addr) && ssn_transport_connect(cli, &addr, 2000)) {
            ssn_rpc_req_t *req = ssn_rpc_req_create(rpc_reply_cb, &ctx);
            if (req) {
                ssn_rpc_connect((ssn_protocol_ctx_t*)req, cli);

                int nums[2] = {15, 25};
                int result = ssn_rpc_call(req, "add", nums, sizeof(nums), 2000);
                INT_ASSERT(result == 0, "RPC add call sent");

                uint64_t start = get_time_ms();
                while (!ctx.completed && (get_time_ms() - start) < 3000) {
                    ssn_rpc_poll((ssn_protocol_ctx_t*)req, 100);
                    usleep(10000);
                }
                INT_ASSERT(ctx.completed, "RPC add response received");

                if (ctx.data && ctx.data_len == sizeof(int)) {
                    int *response = (int*)ctx.data;
                    INT_ASSERT(*response == 40, "RPC add result correct (15+25=40)");
                }

                ssn_rpc_destroy((ssn_protocol_ctx_t*)req);
            }
        }
        ssn_transport_disconnect(cli);
        ssn_transport_destroy(cli);
    }

    pthread_join(th, NULL);
    cleanup_ctx(&ctx);
}

/* ============================================================================
 * 消息协议集成测试
 * ============================================================================ */

static void msg_handler_cb(const void *data, size_t len, void *arg) {
    test_ctx_t *ctx = (test_ctx_t *)arg;
    if (ctx) {
        if (data && len > 0) {
            ctx->data = malloc(len);
            if (ctx->data) { memcpy(ctx->data, data, len); ctx->data_len = len; }
        }
        ctx->completed = 1;
        sem_post(&ctx->sem);
    }
}

static void* msg_server(void *arg) {
    test_ctx_t *ctx = (test_ctx_t *)arg;
    ssn_transport_config_t cfg = { .type = SSN_TRANSPORT_TCP, .non_blocking = false, .reuse_address = true };
    ssn_transport_t *srv = ssn_transport_create(SSN_TRANSPORT_TCP, &cfg);
    if (!srv) return NULL;

    char addr_str[64];
    snprintf(addr_str, sizeof(addr_str), "tcp://127.0.0.1:%d", MSG_TEST_PORT);
    ssn_address_t addr;
    if (!ssn_address_parse(addr_str, &addr)) { ssn_transport_destroy(srv); return NULL; }
    if (!ssn_transport_bind(srv, &addr) || !ssn_transport_listen(srv, 1)) { ssn_transport_destroy(srv); return NULL; }

    ssn_msg_recv_t *recv = ssn_msg_recv_create(msg_handler_cb, NULL);
    if (recv) {
        ssn_msg_recv_bind(recv, srv);
        ssn_address_t cli_addr;
        ssn_transport_t *cli = ssn_transport_accept(srv, &cli_addr, 5000);
        if (cli) {
            ssn_msg_recv_t *recv2 = ssn_msg_recv_create(msg_handler_cb, ctx);
            if (recv2) {
                ssn_msg_recv_bind(recv2, cli);
                for (int i = 0; i < 3; i++) ssn_msg_poll((ssn_protocol_ctx_t*)recv2, 1000);
                ssn_msg_destroy((ssn_protocol_ctx_t*)recv2);
            }
            ssn_transport_destroy(cli);
        }
        ssn_msg_destroy((ssn_protocol_ctx_t*)recv);
    }
    ssn_transport_destroy(srv);
    return NULL;
}

static void test_msg_integration(void) {
    printf("\n=== Message Integration Test ===\n");
    test_ctx_t ctx;
    init_ctx(&ctx);

    pthread_t th;
    pthread_create(&th, NULL, msg_server, &ctx);
    usleep(300000);

    ssn_transport_config_t cfg = { .type = SSN_TRANSPORT_TCP, .non_blocking = false };
    ssn_transport_t *cli = ssn_transport_create(SSN_TRANSPORT_TCP, &cfg);
    if (cli) {
        char addr_str[64];
        snprintf(addr_str, sizeof(addr_str), "tcp://127.0.0.1:%d", MSG_TEST_PORT);
        ssn_address_t addr;
        if (ssn_address_parse(addr_str, &addr) && ssn_transport_connect(cli, &addr, 2000)) {
            ssn_msg_send_t *send = ssn_msg_send_create();
            if (send) {
                ssn_msg_send_connect(send, cli);

                const char *test_msg = "Hello Message!";
                int result = ssn_msg_send(send, test_msg, strlen(test_msg));
                INT_ASSERT(result == 0, "Message sent");

                uint64_t start = get_time_ms();
                while (!ctx.completed && (get_time_ms() - start) < 3000) usleep(10000);
                INT_ASSERT(ctx.completed, "Message received");

                if (ctx.data && ctx.data_len == strlen(test_msg)) {
                    INT_ASSERT(memcmp(ctx.data, test_msg, ctx.data_len) == 0, "Message data matches");
                }

                ssn_msg_destroy((ssn_protocol_ctx_t*)send);
            }
        }
        ssn_transport_disconnect(cli);
        ssn_transport_destroy(cli);
    }

    pthread_join(th, NULL);
    cleanup_ctx(&ctx);
}

/* ============================================================================
 * 发布订阅集成测试
 * ============================================================================ */

static void pubsub_handler_cb(const char *topic, const void *data, size_t len, void *arg) {
    test_ctx_t *ctx = (test_ctx_t *)arg;
    if (ctx) {
        if (topic && data && len > 0) {
            ctx->data = malloc(len);
            if (ctx->data) { memcpy(ctx->data, data, len); ctx->data_len = len; }
        }
        ctx->completed = 1;
        sem_post(&ctx->sem);
    }
}

static void* pubsub_server(void *arg) {
    (void)arg;
    ssn_transport_config_t cfg = { .type = SSN_TRANSPORT_TCP, .non_blocking = false, .reuse_address = true };
    ssn_transport_t *srv = ssn_transport_create(SSN_TRANSPORT_TCP, &cfg);
    if (!srv) return NULL;

    char addr_str[64];
    snprintf(addr_str, sizeof(addr_str), "tcp://127.0.0.1:%d", PUBSUB_TEST_PORT);
    ssn_address_t addr;
    if (!ssn_address_parse(addr_str, &addr)) { ssn_transport_destroy(srv); return NULL; }
    if (!ssn_transport_bind(srv, &addr) || !ssn_transport_listen(srv, 1)) { ssn_transport_destroy(srv); return NULL; }

    ssn_pubsub_pub_t *pub = ssn_pubsub_pub_create();
    if (pub) {
        ssn_pubsub_pub_bind(pub, srv);
        ssn_address_t cli_addr;
        ssn_transport_t *cli = ssn_transport_accept(srv, &cli_addr, 5000);
        if (cli) {
            ssn_pubsub_pub_t *pub2 = ssn_pubsub_pub_create();
            if (pub2) {
                ssn_pubsub_pub_bind(pub2, cli);
                for (int i = 0; i < 3; i++) ssn_pubsub_poll((ssn_protocol_ctx_t*)pub2, 1000);
                ssn_pubsub_destroy((ssn_protocol_ctx_t*)pub2);
            }
            ssn_transport_destroy(cli);
        }
        ssn_pubsub_destroy((ssn_protocol_ctx_t*)pub);
    }
    ssn_transport_destroy(srv);
    return NULL;
}

static void test_pubsub_integration(void) {
    printf("\n=== PubSub Integration Test ===\n");
    test_ctx_t ctx;
    init_ctx(&ctx);

    pthread_t th;
    pthread_create(&th, NULL, pubsub_server, NULL);
    usleep(300000);

    ssn_transport_config_t cfg = { .type = SSN_TRANSPORT_TCP, .non_blocking = false };
    ssn_transport_t *cli = ssn_transport_create(SSN_TRANSPORT_TCP, &cfg);
    if (cli) {
        char addr_str[64];
        snprintf(addr_str, sizeof(addr_str), "tcp://127.0.0.1:%d", PUBSUB_TEST_PORT);
        ssn_address_t addr;
        if (ssn_address_parse(addr_str, &addr) && ssn_transport_connect(cli, &addr, 2000)) {
            ssn_pubsub_sub_t *sub = ssn_pubsub_sub_create(pubsub_handler_cb, &ctx);
            if (sub) {
                ssn_pubsub_sub_connect(sub, cli);

                int result = ssn_pubsub_sub_subscribe(sub, "test_topic");
                INT_ASSERT(result == 0, "Subscribed to topic");

                usleep(100000);

                ssn_pubsub_pub_t *pub = ssn_pubsub_pub_create();
                if (pub) {
                    ssn_pubsub_pub_bind(pub, cli);

                    const char *msg = "Hello PubSub!";
                    result = ssn_pubsub_pub_publish(pub, "test_topic", msg, strlen(msg));
                    INT_ASSERT(result == 0, "Published message");

                    ssn_pubsub_destroy((ssn_protocol_ctx_t*)pub);
                }

                ssn_pubsub_destroy((ssn_protocol_ctx_t*)sub);
            }
        }
        ssn_transport_disconnect(cli);
        ssn_transport_destroy(cli);
    }

    pthread_join(th, NULL);
    cleanup_ctx(&ctx);
}

/* ============================================================================
 * 边界条件和异常测试
 * ============================================================================ */

static void test_protocol_no_transport(void) {
    printf("\n=== Protocol Without Transport Test ===\n");
    test_ctx_t ctx;
    init_ctx(&ctx);

    ssn_rpc_req_t *req = ssn_rpc_req_create(rpc_reply_cb, &ctx);
    if (req) {
        int result = ssn_rpc_call(req, "test", "data", 4, 100);
        INT_ASSERT(result == -1, "RPC call fails without transport");
        ssn_rpc_destroy((ssn_protocol_ctx_t*)req);
    }

    ssn_pubsub_sub_t *sub = ssn_pubsub_sub_create(pubsub_handler_cb, &ctx);
    if (sub) {
        int result = ssn_pubsub_sub_subscribe(sub, "topic");
        INT_ASSERT(result == -1, "Subscribe fails without transport");
        ssn_pubsub_destroy((ssn_protocol_ctx_t*)sub);
    }

    ssn_msg_send_t *send = ssn_msg_send_create();
    if (send) {
        int result = ssn_msg_send(send, "test", 4);
        INT_ASSERT(result == -1, "Send fails without transport");
        ssn_msg_destroy((ssn_protocol_ctx_t*)send);
    }

    cleanup_ctx(&ctx);
}

static void test_protocol_create_destroy_cycles(void) {
    printf("\n=== Protocol Create/Destroy Cycles Test ===\n");
    test_ctx_t ctx;
    init_ctx(&ctx);

    for (int i = 0; i < 20; i++) {
        ssn_rpc_req_t *req = ssn_rpc_req_create(rpc_reply_cb, &ctx);
        if (req) ssn_rpc_destroy((ssn_protocol_ctx_t*)req);

        ssn_pubsub_pub_t *pub = ssn_pubsub_pub_create();
        if (pub) ssn_pubsub_destroy((ssn_protocol_ctx_t*)pub);

        ssn_msg_send_t *msg_send = ssn_msg_send_create();
        if (msg_send) ssn_msg_destroy((ssn_protocol_ctx_t*)msg_send);
    }

    INT_ASSERT(true, "20 create/destroy cycles completed");
    cleanup_ctx(&ctx);
}

static void test_protocol_types_and_roles(void) {
    printf("\n=== Protocol Types and Roles Test ===\n");
    test_ctx_t ctx;
    init_ctx(&ctx);

    ssn_rpc_req_t *req = ssn_rpc_req_create(rpc_reply_cb, &ctx);
    if (req) {
        INT_ASSERT(ssn_protocol_get_type((ssn_protocol_ctx_t*)req) == SSN_PROTOCOL_RPC, "RPC request type correct");
        INT_ASSERT(ssn_protocol_get_role((ssn_protocol_ctx_t*)req) == SSN_ROLE_REQ, "RPC request role correct");
        ssn_rpc_destroy((ssn_protocol_ctx_t*)req);
    }

    ssn_pubsub_pub_t *pub = ssn_pubsub_pub_create();
    if (pub) {
        INT_ASSERT(ssn_protocol_get_type((ssn_protocol_ctx_t*)pub) == SSN_PROTOCOL_PUBSUB, "PubSub pub type correct");
        INT_ASSERT(ssn_protocol_get_role((ssn_protocol_ctx_t*)pub) == SSN_ROLE_PUB, "PubSub pub role correct");
        ssn_pubsub_destroy((ssn_protocol_ctx_t*)pub);
    }

    ssn_msg_send_t *msg_send = ssn_msg_send_create();
    if (msg_send) {
        INT_ASSERT(ssn_protocol_get_type((ssn_protocol_ctx_t*)msg_send) == SSN_PROTOCOL_MSG, "MSG send type correct");
        INT_ASSERT(ssn_protocol_get_role((ssn_protocol_ctx_t*)msg_send) == SSN_ROLE_SEND, "MSG send role correct");
        ssn_msg_destroy((ssn_protocol_ctx_t*)msg_send);
    }

    cleanup_ctx(&ctx);
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    printf("========================================\n");
    printf("SSN Protocol Integration Tests\n");
    printf("========================================\n");

    printf("\n[Boundary and Exception Tests]\n");
    test_protocol_no_transport();
    test_protocol_create_destroy_cycles();
    test_protocol_types_and_roles();

    printf("\n[RPC Integration Tests]\n");
    test_rpc_integration();
    test_rpc_add_integration();

    printf("\n[Message Integration Tests]\n");
    test_msg_integration();

    printf("\n[PubSub Integration Tests]\n");
    test_pubsub_integration();

    printf("\n========================================\n");
    printf("Unit Tests: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    printf("Integration Tests: %d passed, %d failed\n", g_integration_passed, g_integration_failed);
    printf("========================================\n");

    return (g_tests_failed + g_integration_failed) > 0 ? 1 : 0;
}
