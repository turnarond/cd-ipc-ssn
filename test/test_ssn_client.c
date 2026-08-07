/*
 * test_ssn_client.c - Client API functional test
 *
 * Tests: client create/connect/disconnect/close, RPC call, subscribe, message, poll
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include "ssn_client.h"
#include "ssn_server.h"

#define TEST_SERVER_ADDR "unix:///tmp/ssn-client-test"
#define TEST_TIMEOUT_MS 5000

/* ---- Shared test infrastructure ---- */

static volatile int g_srv_running = 0;

static void echo_handler(ssn_server_t *server, ssn_peer_id_t cid,
                         ssn_header_t *ssn_hdr, ssn_url_ref_t *url,
                         ssn_data_ref_t *data, void *arg)
{
    (void)url; (void)arg;
    uint16_t seqno = ssn_get_seqno(ssn_hdr);
    ssn_server_response(server, cid, 0, seqno, data);
}

static void *server_thread(void *arg)
{
    ssn_server_t *srv = (ssn_server_t *)arg;
    while (g_srv_running) ssn_server_poll(srv, 100);
    return NULL;
}

static ssn_server_t *start_test_server(pthread_t *tid, const char *method)
{
    ssn_server_t *srv = ssn_server_create(TEST_SERVER_ADDR);
    if (!srv) return NULL;
    ssn_url_ref_t url = { .url = (char*)method, .url_len = strlen(method) };
    ssn_server_add_method(srv, &url, echo_handler, NULL);
    if (!ssn_server_start(srv)) { ssn_server_destroy(srv); return NULL; }
    g_srv_running = 1;
    pthread_create(tid, NULL, server_thread, srv);
    usleep(100000);
    return srv;
}

static void stop_test_server(ssn_server_t *srv, pthread_t tid)
{
    g_srv_running = 0;
    pthread_join(tid, NULL);
    ssn_server_destroy(srv);
}

/* ---- Test 1: Create/Destroy ---- */

static int test_create_destroy(void)
{
    printf("  Test 1: Client create/destroy... ");
    ssn_client_t *cli = ssn_client_create();
    if (!cli) { printf("FAIL\n"); return 1; }
    if (!ssn_client_is_connect(cli)) {
        /* Expected: not connected yet */
    }
    ssn_client_close(cli);
    printf("PASS\n");
    return 0;
}

/* ---- Test 2: Connect / Disconnect ---- */

static int test_connect_disconnect(void)
{
    printf("  Test 2: Connect / disconnect... ");
    pthread_t tid; ssn_server_t *srv = start_test_server(&tid, "/connect");
    if (!srv) { printf("FAIL (server)\n"); return 1; }

    ssn_client_t *cli = ssn_client_create();
    if (!cli) { stop_test_server(srv, tid); printf("FAIL (create)\n"); return 1; }

    {
        struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
        if (!ssn_client_connect(cli, TEST_SERVER_ADDR, &ts)) {
            ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (connect)\n"); return 1;
        }
    }
    if (!ssn_client_is_connect(cli)) {
        ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (is_connect)\n"); return 1;
    }

    ssn_client_disconnect(cli);
    if (ssn_client_is_connect(cli)) {
        ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (still connected)\n"); return 1;
    }

    ssn_client_close(cli);
    stop_test_server(srv, tid);
    printf("PASS\n");
    return 0;
}

/* ---- Test 3: RPC Call ---- */

static volatile int g_reply = 0;
static char g_buf[256];
static size_t g_len = 0;

static void rpc_cb(ssn_client_t *client, ssn_header_t *ssn_hdr,
                   ssn_data_ref_t *data, void *arg)
{
    (void)client; (void)ssn_hdr; (void)arg;
    if (data && data->data && data->length < sizeof(g_buf)) {
        memcpy(g_buf, data->data, data->length);
        g_len = data->length;
    }
    g_reply = 1;
}

static int test_rpc_call(void)
{
    printf("  Test 3: RPC call... ");
    pthread_t tid; ssn_server_t *srv = start_test_server(&tid, "/rpc");
    if (!srv) { printf("FAIL (server)\n"); return 1; }

    ssn_client_t *cli = ssn_client_create();
    struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
    if (!cli || !ssn_client_connect(cli, TEST_SERVER_ADDR, &ts)) {
        if (cli) ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (connect)\n"); return 1;
    }

    g_reply = 0; g_len = 0;
    const char *msg = "ping";
    ssn_url_ref_t url = { .url = "/rpc", .url_len = 4 };
    ssn_data_ref_t req = { .data = (void*)msg, .length = 4 };

    if (ssn_client_call(cli, &url, &req, rpc_cb, NULL, TEST_TIMEOUT_MS) < 0) {
        ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (call)\n"); return 1;
    }

    for (int i = 0; i < 10 && !g_reply; i++) { ssn_client_poll(cli, 100); usleep(50000); }

    ssn_client_close(cli);
    stop_test_server(srv, tid);

    if (!g_reply) { printf("FAIL (no reply)\n"); return 1; }
    if (g_len != 4 || memcmp(g_buf, "ping", 4) != 0) { printf("FAIL (bad reply)\n"); return 1; }
    printf("PASS\n");
    return 0;
}

/* ---- Test 4: Subscribe ---- */

static int test_subscribe(void)
{
    printf("  Test 4: Subscribe... ");
    pthread_t tid; ssn_server_t *srv = start_test_server(&tid, "/topic");
    if (!srv) { printf("FAIL (server)\n"); return 1; }

    ssn_client_t *cli = ssn_client_create();
    {
        struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
        if (!cli || !ssn_client_connect(cli, TEST_SERVER_ADDR, &ts)) {
            if (cli) ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (connect)\n"); return 1;
        }
    }

    ssn_url_ref_t url = { .url = "/topic", .url_len = 6 };
    if (!ssn_client_subscribe(cli, &url, NULL, NULL, TEST_TIMEOUT_MS)) {
        ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (subscribe)\n"); return 1;
    }

    ssn_client_close(cli);
    stop_test_server(srv, tid);
    printf("PASS\n");
    return 0;
}

/* ---- Test 5: Send Message ---- */

static int test_send_message(void)
{
    printf("  Test 5: Send message... ");
    pthread_t tid; ssn_server_t *srv = start_test_server(&tid, "/msg");
    if (!srv) { printf("FAIL (server)\n"); return 1; }

    ssn_client_t *cli = ssn_client_create();
    {
        struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
        if (!cli || !ssn_client_connect(cli, TEST_SERVER_ADDR, &ts)) {
            if (cli) ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (connect)\n"); return 1;
        }
    }

    const char *msg = "hello";
    ssn_url_ref_t url = { .url = "/msg", .url_len = 4 };
    ssn_data_ref_t data = { .data = (void*)msg, .length = 5 };
    if (ssn_client_message(cli, &url, &data) < 0) {
        ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (send)\n"); return 1;
    }

    ssn_client_close(cli);
    stop_test_server(srv, tid);
    printf("PASS\n");
    return 0;
}

/* ---- Test 6: Poll Timeout ---- */

static int test_poll_timeout(void)
{
    printf("  Test 6: Poll timeout (500ms)... ");
    pthread_t tid; ssn_server_t *srv = start_test_server(&tid, "/poll");
    if (!srv) { printf("FAIL (server)\n"); return 1; }

    ssn_client_t *cli = ssn_client_create();
    {
        struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
        if (!cli || !ssn_client_connect(cli, TEST_SERVER_ADDR, &ts)) {
            if (cli) ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (connect)\n"); return 1;
        }
    }

    /* 测量 ssn_client_poll(cli, 500) 的实际等待时长：
     * 修复前毫秒余数被直接当作纳秒（500ms 只等约 500ns），修复后应等待约 500ms。
     * 最多轮询 3 次并累计（防止残留事件导致 pselect 提前返回），累计须 ≥ 400ms。 */
    long long elapsed_ms = 0;
    for (int i = 0; i < 3 && elapsed_ms < 400; i++) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        ssn_client_poll(cli, 500);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        elapsed_ms += (t1.tv_sec - t0.tv_sec) * 1000LL +
                      (t1.tv_nsec - t0.tv_nsec) / 1000000LL;
    }

    ssn_client_close(cli);
    stop_test_server(srv, tid);

    printf("waited %lldms... ", elapsed_ms);
    if (elapsed_ms < 400) { printf("FAIL (poll(500) 实际等待不足 400ms)\n"); return 1; }
    printf("PASS\n");
    return 0;
}

/* ---- Test 7: 定时器线程空列表存活（回归：空列表 break 导致线程永久退出） ---- */

static volatile int g_timeout_fired = 0;

/* 超时回调：应答头为 NULL 说明是超时触发而非服务端应答 */
static void timeout_cb(ssn_client_t *client, ssn_header_t *ipc_hdr,
                       ssn_data_ref_t *data, void *arg)
{
    (void)client; (void)data; (void)arg;
    if (ipc_hdr == NULL) g_timeout_fired = 1;
}

/* 故意不回包的 RPC 方法：使客户端请求只能通过超时机制结束 */
static void no_reply_handler(ssn_server_t *server, ssn_peer_id_t cid,
                             ssn_header_t *ssn_hdr, ssn_url_ref_t *url,
                             ssn_data_ref_t *data, void *arg)
{
    (void)server; (void)cid; (void)ssn_hdr; (void)url; (void)data; (void)arg;
}

static int test_timer_thread_survives(void)
{
    printf("  Test 7: Timer thread survives empty list... ");

    /* 确保无存活 client（前面用例的 client 均已关闭）：
     * 空转 100ms（> 50ms 定时器周期），若定时器线程在空列表时 break
     * 退出，此窗口内线程已永久死亡，之后所有 pending 超时不再被处理。 */
    usleep(100000);

    pthread_t tid; ssn_server_t *srv = start_test_server(&tid, "/timer");
    if (!srv) { printf("FAIL (server)\n"); return 1; }

    /* 注册一个不回包的方法（不与 start_test_server 的 echo 方法重名） */
    ssn_url_ref_t no_reply_url = { .url = "/timer-no-reply", .url_len = 15 };
    if (!ssn_server_add_method(srv, &no_reply_url, no_reply_handler, NULL)) {
        stop_test_server(srv, tid); printf("FAIL (add_method)\n"); return 1;
    }

    ssn_client_t *cli = ssn_client_create();
    {
        struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
        if (!cli || !ssn_client_connect(cli, TEST_SERVER_ADDR, &ts)) {
            if (cli) ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (connect)\n"); return 1;
        }
    }

    /* 发送 RPC（服务端不回包）→ 依赖定时器线程递减超时并触发超时回调 */
    g_timeout_fired = 0;
    const char *msg = "ping";
    ssn_data_ref_t req = { .data = (void*)msg, .length = 4 };
    if (ssn_client_call(cli, &no_reply_url, &req, timeout_cb, NULL, 500) < 0) {
        ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (call)\n"); return 1;
    }

    /* poll 驱动事件循环，最多约 3s（500ms 超时 + 定时器 50ms 周期余量） */
    for (int i = 0; i < 20 && !g_timeout_fired; i++) { ssn_client_poll(cli, 100); usleep(50000); }

    int fired = g_timeout_fired;

    ssn_client_close(cli);
    stop_test_server(srv, tid);

    if (!fired) { printf("FAIL (timeout callback not fired)\n"); return 1; }
    printf("PASS\n");
    return 0;
}

/* ---- Test 8: set_on_message + 订阅消息分发（回归：set_on_message 恢复 onsub 赋值） ---- */

static volatile int g_pub_received = 0;
static char g_pub_buf[256];
static size_t g_pub_len = 0;

/* set_on_message 设置的消息回调：订阅（无回调）后收到的发布消息应分发到这里 */
static void pub_handler(ssn_client_t *client, ssn_url_ref_t *url,
                        ssn_data_ref_t *data, void *arg)
{
    (void)client; (void)url; (void)arg;
    if (data && data->data && data->length < sizeof(g_pub_buf)) {
        memcpy(g_pub_buf, data->data, data->length);
        g_pub_len = data->length;
    }
    g_pub_received = 1;
}

static int test_set_on_message_subscribe(void)
{
    printf("  Test 8: set_on_message + subscribe + publish... ");
    pthread_t tid; ssn_server_t *srv = start_test_server(&tid, "/pubtopic");
    if (!srv) { printf("FAIL (server)\n"); return 1; }

    ssn_client_t *cli = ssn_client_create();
    {
        struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
        if (!cli || !ssn_client_connect(cli, TEST_SERVER_ADDR, &ts)) {
            if (cli) ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (connect)\n"); return 1;
        }
    }

    /* set_on_message 后订阅（订阅不带回调）：发布消息应分发到该回调；
     * 同时覆盖 NULL 订阅回调的保护（不得因空回调崩溃） */
    ssn_client_set_on_message(cli, pub_handler, NULL);

    ssn_url_ref_t url = { .url = "/pubtopic", .url_len = 9 };
    if (!ssn_client_subscribe(cli, &url, NULL, NULL, TEST_TIMEOUT_MS)) {
        ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (subscribe)\n"); return 1;
    }

    /* ssn_client_subscribe 为异步发送（无回调时不登记 pending）：
     * 需等服务端 poll 处理完 SUBSCRIBE 并注册订阅后再发布 */
    ssn_client_poll(cli, 100);
    usleep(500000); // 服务端 poll 间隔 100ms，留足处理余量

    /* 服务端发布 → 客户端 poll 驱动消息分发到 set_on_message 回调 */
    g_pub_received = 0; g_pub_len = 0;
    const char *payload = "hello-pub";
    ssn_data_ref_t pub_data = { .data = (void*)payload, .length = 9 };
    if (!ssn_server_publish(srv, &url, &pub_data)) {
        ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (publish)\n"); return 1;
    }

    for (int i = 0; i < 20 && !g_pub_received; i++) { ssn_client_poll(cli, 100); usleep(50000); }

    ssn_client_close(cli);
    stop_test_server(srv, tid);

    if (!g_pub_received) { printf("FAIL (no message)\n"); return 1; }
    if (g_pub_len != 9 || memcmp(g_pub_buf, "hello-pub", 9) != 0) { printf("FAIL (bad message)\n"); return 1; }
    printf("PASS\n");
    return 0;
}

/* ---- Test 9: 64 KiB 大消息往返（回归：SSN_MAX_PACKET_SIZE 宏重定义） ---- */

#define BIG_MSG_SIZE (64 * 1024)

static volatile int g_big_reply = 0;
static char *g_big_buf = NULL;
static size_t g_big_len = 0;

/* 应答回调：校验长度并拷贝内容，供主流程逐字节比对 */
static void big_reply_cb(ssn_client_t *client, ssn_header_t *ssn_hdr,
                         ssn_data_ref_t *data, void *arg)
{
    (void)client; (void)ssn_hdr; (void)arg;
    if (data && data->data && data->length == BIG_MSG_SIZE && g_big_buf) {
        memcpy(g_big_buf, data->data, data->length);
        g_big_len = data->length;
    }
    g_big_reply = 1;
}

static int test_big_message_roundtrip(void)
{
    printf("  Test 9: 64 KiB message round trip... ");
    pthread_t tid; ssn_server_t *srv = start_test_server(&tid, "/bigmsg");
    if (!srv) { printf("FAIL (server)\n"); return 1; }

    ssn_client_t *cli = ssn_client_create();
    struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
    if (!cli || !ssn_client_connect(cli, TEST_SERVER_ADDR, &ts)) {
        if (cli) ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (connect)\n"); return 1;
    }

    /* 64 KiB（> 旧 server 侧 8192 上限）递增模式数据，RPC echo 后逐字节比对 */
    unsigned char *payload = (unsigned char *)malloc(BIG_MSG_SIZE);
    g_big_buf = (char *)malloc(BIG_MSG_SIZE);
    if (!payload || !g_big_buf) {
        free(payload); free(g_big_buf); g_big_buf = NULL;
        ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (malloc)\n"); return 1;
    }
    for (int i = 0; i < BIG_MSG_SIZE; i++) {
        payload[i] = (unsigned char)(i & 0xFF);
    }

    g_big_reply = 0; g_big_len = 0;
    ssn_url_ref_t url = { .url = "/bigmsg", .url_len = 7 };
    ssn_data_ref_t req = { .data = payload, .length = BIG_MSG_SIZE };

    if (ssn_client_call(cli, &url, &req, big_reply_cb, NULL, TEST_TIMEOUT_MS) < 0) {
        free(payload); free(g_big_buf); g_big_buf = NULL;
        ssn_client_close(cli); stop_test_server(srv, tid); printf("FAIL (call)\n"); return 1;
    }

    for (int i = 0; i < 20 && !g_big_reply; i++) { ssn_client_poll(cli, 100); usleep(50000); }

    int ok = g_big_reply && g_big_len == BIG_MSG_SIZE &&
             memcmp(payload, g_big_buf, BIG_MSG_SIZE) == 0;

    free(payload); free(g_big_buf); g_big_buf = NULL;
    ssn_client_close(cli);
    stop_test_server(srv, tid);

    if (!ok) { printf("FAIL (round trip mismatch)\n"); return 1; }
    printf("PASS\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    printf("=== Client API Tests ===\n");

    failed += test_create_destroy();
    failed += test_connect_disconnect();
    failed += test_rpc_call();
    failed += test_subscribe();
    failed += test_send_message();
    failed += test_poll_timeout();
    failed += test_timer_thread_survives();
    failed += test_set_on_message_subscribe();
    failed += test_big_message_roundtrip();

    printf("=== Result: %d/9 passed, %d failed ===\n", 9 - failed, failed);
    return failed ? 1 : 0;
}
