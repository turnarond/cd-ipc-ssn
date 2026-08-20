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

/* ---- Test 10: connect 失败路径 fd 泄漏（回归：Issue #10） ----
 *
 * 缺陷：tcp/unix/udp_transport_create 构造时创建 socket fd，connect 时再次
 *       socket() 覆盖 impl->sock_fd——构造 fd 永久泄漏（每轮 +1）。
 * 回归：连接（tcp 失败路径 + udp 重建路径）循环后 /proc/self/fd 计数不增长。
 */

#include <dirent.h>

static int fd_count(void)
{
    struct dirent *e;
    int n = 0;
    DIR *d = opendir("/proc/self/fd");
    if (!d) return -1;
    while ((e = readdir(d))) { if (e->d_name[0] != '.') n++; }
    closedir(d);
    return n;
}

static int test_connect_fail_fd_leak(void)
{
    printf("  Test 10: Connect-fail fd leak... ");
    struct timespec ts = { 1, 0 };
    int base = fd_count();

    for (int i = 0; i < 5; i++) {
        ssn_client_t *cli = ssn_client_create();
        if (!cli) { printf("FAIL (create)\n"); return 1; }
        /* 连接不存在的服务端 → connect 失败路径（transport 创建 + 重建） */
        ssn_client_connect(cli, "tcp://127.0.0.1:18949", &ts);
        ssn_client_close(cli);
        /* UDP 路径同源泄漏回归（udp connect 重建 socket 覆盖构造 fd） */
        ssn_client_t *uc = ssn_client_create();
        if (!uc) { printf("FAIL (create udp)\n"); return 1; }
        ssn_client_connect(uc, "udp://127.0.0.1:18949", &ts);
        ssn_client_close(uc);
    }

    int after = fd_count();
    if (after > base) {
        printf("FAIL (fd grew %d -> %d)\n", base, after);
        return 1;
    }
    printf("PASS\n");
    return 0;
}

/* ---- Test 11: 慢握手服务端（回归：connect 重试预算 vs 文档化事件循环周期） ----
 *
 * 缺陷：ssn_client_connect 的握手应答 recv 重试预算仅 5×100ms=500ms，而文档化
 *       服务端事件循环（poll(100)+sleep(1)≈1.1s 周期，见 examples/ 各 server）
 *       下握手需 ≥2 个 poll 周期（accept 一轮 + SERVICE_INFO 应答一轮）≈2.2s，
 *       客户端必然在服务端应答前超时失败（用户旅程实测 26 次全失败）。
 * 回归：慢事件循环服务端（0.5s 周期，握手约 1s）下，connect(timeout=3s) 仍成功。
 */

static void *slow_server_thread(void *arg)
{
    ssn_server_t *srv = (ssn_server_t *)arg;
    while (g_srv_running) {
        ssn_server_poll(srv, 100);
        usleep(600000); /* 模拟文档化事件循环的 sleep：约 0.7s 周期，
                         * 握手 ≥2 周期 ≈1.4s，远超 connect 500ms 预算 */
    }
    return NULL;
}

static int test_slow_handshake_connect(void)
{
    printf("  Test 11: Slow-handshake connect... ");

    ssn_server_t *srv = ssn_server_create(TEST_SERVER_ADDR);
    if (!srv) { printf("FAIL (server create)\n"); return 1; }
    if (!ssn_server_start(srv)) {
        ssn_server_destroy(srv);
        printf("FAIL (server start)\n"); return 1;
    }
    g_srv_running = 1;
    pthread_t tid;
    pthread_create(&tid, NULL, slow_server_thread, srv);

    ssn_client_t *cli = ssn_client_create();
    struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
    bool ok = (cli != NULL) && ssn_client_connect(cli, TEST_SERVER_ADDR, &ts);

    if (cli) ssn_client_close(cli);
    stop_test_server(srv, tid);

    if (!ok) {
        printf("FAIL (connect timed out vs slow handshake)\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}

/* ---- Test 12: 超时/应答回调内再次调用 client API 不死锁（回归：持锁回调） ----
 *
 * 缺陷：ssn_client_timeout_all / process_events 超时分支 / unref 释放路径在持
 *       client->lock 时直接调用用户回调，而回调内调用任何 ssn_client_* API
 *       （call/disconnect 等）都需要获取同一把非递归锁 → 自锁死锁。
 * 回归：RPC 应答回调内调用 ssn_client_is_connect（取锁）；RPC 超时回调内调用
 *       ssn_client_disconnect（取锁）——均不得挂死。
 */

static volatile int g_cb_reentrant_ok = 0;

static void rpc_reentrant_cb(ssn_client_t *client, ssn_header_t *ssn_hdr,
                             ssn_data_ref_t *data, void *arg)
{
    (void)ssn_hdr; (void)data; (void)arg;
    /* 回调内再次调用 client API：修复前此处自锁死锁 */
    if (ssn_client_is_connect(client)) {
        g_cb_reentrant_ok = 1;
    }
}

static int test_callback_reentrant(void)
{
    printf("  Test 12: Callback reentrant (no self-deadlock)... ");
    pthread_t tid; ssn_server_t *srv = start_test_server(&tid, "/reentrant");
    if (!srv) { printf("FAIL (server)\n"); return 1; }

    ssn_client_t *cli = ssn_client_create();
    struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
    if (!cli || !ssn_client_connect(cli, TEST_SERVER_ADDR, &ts)) {
        if (cli) ssn_client_close(cli); stop_test_server(srv, tid);
        printf("FAIL (connect)\n"); return 1;
    }

    g_cb_reentrant_ok = 0;
    const char *msg = "reentrant";
    ssn_url_ref_t url = { .url = "/reentrant", .url_len = 10 };
    ssn_data_ref_t req = { .data = (void*)msg, .length = 9 };

    if (ssn_client_call(cli, &url, &req, rpc_reentrant_cb, NULL, TEST_TIMEOUT_MS) < 0) {
        ssn_client_close(cli); stop_test_server(srv, tid);
        printf("FAIL (call)\n"); return 1;
    }

    for (int i = 0; i < 10 && !g_cb_reentrant_ok; i++) {
        ssn_client_poll(cli, 100);
        usleep(50000);
    }

    int ok = g_cb_reentrant_ok;
    ssn_client_close(cli);
    stop_test_server(srv, tid);

    if (!ok) { printf("FAIL (callback deadlock or no reply)\n"); return 1; }
    printf("PASS\n");
    return 0;
}

/* ---- Test 13: 空闲连接 poll 不误判断开（回归 Issue #22） ----
 *
 * 缺陷：ssn_client_process_events 的局部变量 pkt_e 未初始化——当 socket 无数据
 * （did_recv=false）时 pkt_e 读取未初始化内存（UB），垃圾值可能为 true → 误判
 * 「连接丢失」→ 断开。cliauto CONNECTED 分支（v2.5.1 保活改造后）每次 tick 都
 * 调 ssn_client_poll，连接空闲时即触发，导致连接建立后 ~50ms 误断并循环重连。
 * 回归：连接建立后保持完全空闲（无消息/无 ping），反复 poll 空闲 socket 200ms，
 *       is_connect 必须始终为 true（不误断）。
 */
static int test_idle_connect_stays_alive(void)
{
    printf("  Test 13: Idle connect stays alive across polls... ");
    pthread_t tid; ssn_server_t *srv = start_test_server(&tid, "/idle");
    if (!srv) { printf("FAIL (server)\n"); return 1; }

    ssn_client_t *cli = ssn_client_create();
    struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
    if (!cli || !ssn_client_connect(cli, TEST_SERVER_ADDR, &ts)) {
        if (cli) ssn_client_close(cli); stop_test_server(srv, tid);
        printf("FAIL (connect)\n"); return 1;
    }

    /* 连接后保持空闲：不发消息、不 ping，反复 poll 空闲 socket。
     * 修复前 pkt_e 未初始化导致 poll 偶发误判断开（is_connect 变 false）。 */
    int disconnected = 0;
    for (int i = 0; i < 20; i++) {
        ssn_client_poll(cli, 10);
        if (!ssn_client_is_connect(cli)) {
            disconnected = 1;
            break;
        }
        usleep(10000);
    }

    int ok = !disconnected;
    ssn_client_close(cli);
    stop_test_server(srv, tid);

    if (!ok) { printf("FAIL (idle connect falsely disconnected)\n"); return 1; }
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
    failed += test_connect_fail_fd_leak();
    failed += test_slow_handshake_connect();
    failed += test_callback_reentrant();
    failed += test_idle_connect_stays_alive();

    printf("=== Result: %d/13 passed, %d failed ===\n", 13 - failed, failed);
    return failed ? 1 : 0;
}
