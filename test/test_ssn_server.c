/*
 * test_ssn_server.c - Server API functional test
 *
 * Tests: server create/start/stop/destroy, add RPC method, poll, response
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "ssn_server.h"
#include "ssn_client.h"

#define TEST_SERVER_ADDR "unix:///tmp/ssn-server-test"
#define TEST_TIMEOUT_MS 5000

static volatile int g_handler_called = 0;

/* RPC handler: echoes back the request data */
static void echo_handler(ssn_server_t *server, ssn_peer_id_t cid,
                         ssn_header_t *ssn_hdr, ssn_url_ref_t *url,
                         ssn_data_ref_t *data, void *arg)
{
    (void)url; (void)arg;
    uint16_t seqno = ssn_get_seqno(ssn_hdr);
    g_handler_called = 1;
    ssn_server_response(server, cid, 0, seqno, data);
}

/* Server thread: runs event loop until signaled */
static volatile int g_srv_running = 0;
static int g_srv_poll_ms = 100;

static void *server_thread(void *arg)
{
    ssn_server_t *srv = (ssn_server_t *)arg;
    while (g_srv_running) {
        ssn_server_poll(srv, g_srv_poll_ms);
    }
    return NULL;
}

/* Client-side: connects, calls RPC, waits for reply */
static volatile int g_reply_received = 0;
static char g_reply_buf[256];
static size_t g_reply_len = 0;

static void reply_cb(ssn_client_t *client, ssn_header_t *ssn_hdr,
                     ssn_data_ref_t *data, void *arg)
{
    (void)client; (void)ssn_hdr; (void)arg;
    if (data && data->data && data->length < sizeof(g_reply_buf)) {
        memcpy(g_reply_buf, data->data, data->length);
        g_reply_len = data->length;
    }
    g_reply_received = 1;
}

static int test_create_destroy(void)
{
    printf("  Test 1: Server create/destroy... ");
    ssn_server_t *srv = ssn_server_create(TEST_SERVER_ADDR);
    if (!srv) { printf("FAIL (create)\n"); return 1; }
    ssn_server_destroy(srv);
    printf("PASS\n");
    return 0;
}

static int test_start_stop(void)
{
    printf("  Test 2: Server start/stop... ");
    ssn_server_t *srv = ssn_server_create(TEST_SERVER_ADDR);
    if (!srv) { printf("FAIL (create)\n"); return 1; }
    if (!ssn_server_start(srv)) { printf("FAIL (start)\n"); ssn_server_destroy(srv); return 1; }
    ssn_server_destroy(srv);
    printf("PASS\n");
    return 0;
}

static int test_add_method(void)
{
    printf("  Test 3: Add RPC method... ");
    ssn_server_t *srv = ssn_server_create(TEST_SERVER_ADDR);
    if (!srv) { printf("FAIL (create)\n"); return 1; }

    ssn_url_ref_t url = { .url = "/echo", .url_len = 5 };
    if (!ssn_server_add_method(srv, &url, echo_handler, NULL)) {
        printf("FAIL (add_method)\n"); ssn_server_destroy(srv); return 1;
    }
    ssn_server_destroy(srv);
    printf("PASS\n");
    return 0;
}

static int test_rpc_echo(void)
{
    printf("  Test 4: RPC echo via client... ");
    g_handler_called = 0;
    g_reply_received = 0;
    g_reply_len = 0;

    /* Create and start server */
    ssn_server_t *srv = ssn_server_create(TEST_SERVER_ADDR);
    if (!srv) { printf("FAIL (create server)\n"); return 1; }

    ssn_url_ref_t url = { .url = "/echo", .url_len = 5 };
    ssn_server_add_method(srv, &url, echo_handler, NULL);

    if (!ssn_server_start(srv)) { printf("FAIL (start)\n"); ssn_server_destroy(srv); return 1; }

    /* Run server event loop in background */
    pthread_t tid;
    g_srv_running = 1;
    pthread_create(&tid, NULL, server_thread, srv);
    usleep(100000); /* Give server time to start */

    /* Create client and connect */
    ssn_client_t *cli = ssn_client_create();
    if (!cli) { printf("FAIL (create client)\n"); goto cleanup; }

    struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
    if (!ssn_client_connect(cli, TEST_SERVER_ADDR, &ts)) {
        printf("FAIL (connect)\n"); ssn_client_close(cli); goto cleanup;
    }

    /* Make RPC call */
    const char *msg = "Hello echo!";
    ssn_data_ref_t req = { .data = (void*)msg, .length = strlen(msg) };
    int ret = ssn_client_call(cli, &url, &req, reply_cb, NULL, TEST_TIMEOUT_MS);
    if (ret < 0) { printf("FAIL (call)\n"); ssn_client_close(cli); goto cleanup; }

    /* Wait for reply */
    for (int i = 0; i < 10 && !g_reply_received; i++) {
        ssn_client_poll(cli, 100);
        usleep(50000);
    }

    ssn_client_close(cli);

    if (!g_handler_called) { printf("FAIL (handler not called)\n"); goto cleanup; }
    if (!g_reply_received) { printf("FAIL (no reply)\n"); goto cleanup; }
    if (g_reply_len != strlen(msg) || memcmp(g_reply_buf, msg, g_reply_len) != 0) {
        printf("FAIL (reply mismatch)\n"); goto cleanup;
    }

    g_srv_running = 0;
    pthread_join(tid, NULL);
    ssn_server_destroy(srv);
    printf("PASS\n");
    return 0;

cleanup:
    g_srv_running = 0;
    pthread_join(tid, NULL);
    ssn_server_destroy(srv);
    return 1;
}

/* ---- Test 5: idle 超时断开（回归：服务端定时器线程空列表退出） ---- */

static int test_idle_timeout_disconnect(void)
{
    printf("  Test 5: Idle timeout disconnects idle client... ");

    /* 空转 150ms（> 定时器 50ms 周期）：确保至少一个 tick 观察到空列表。
     * 修复前服务端定时器线程在空列表时 break 永久退出（死亡窗口），
     * 之后所有超时检测（idle/握手）全部失效。 */
    usleep(150000);

    /* conn_timeout_ms 未设置（0）：客户端握手完成后 hst.alive 为 0，
     * 定时器线程任一次 tick 检查即触发断开。 */
    server_options_t opts = { .idle_timeout_sec = 1 };
    ssn_server_t *srv = ssn_server_create_with_options(TEST_SERVER_ADDR, &opts);
    if (!srv) { printf("FAIL (create server)\n"); return 1; }
    if (!ssn_server_start(srv)) { printf("FAIL (start)\n"); ssn_server_destroy(srv); return 1; }

    pthread_t tid;
    g_srv_running = 1;
    pthread_create(&tid, NULL, server_thread, srv);
    usleep(50000); /* Give server poll time to start */

    ssn_client_t *cli = ssn_client_create();
    if (!cli) { printf("FAIL (create client)\n"); goto cleanup; }

    struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
    if (!ssn_client_connect(cli, TEST_SERVER_ADDR, &ts)) {
        printf("FAIL (connect)\n"); ssn_client_close(cli); goto cleanup;
    }

    /* 保持连接（期间无消息）并驱动事件循环，等待服务端超时断开。
     * 修复前定时器线程已死 → alive 不递减 → 连接一直保持 → 断言失败。 */
    int dropped = 0;
    for (int i = 0; i < 35 && !dropped; i++) {
        ssn_client_poll(cli, 100);
        if (!ssn_client_is_connect(cli)) {
            dropped = 1;
            break;
        }
        usleep(100000);
    }

    ssn_client_close(cli);
    g_srv_running = 0;
    pthread_join(tid, NULL);
    ssn_server_destroy(srv);

    if (!dropped) { printf("FAIL (connection not dropped by timeout)\n"); return 1; }
    printf("PASS\n");
    return 0;

cleanup:
    g_srv_running = 0;
    pthread_join(tid, NULL);
    ssn_server_destroy(srv);
    return 1;
}

/* ---- Test 6: idle 断开（idle_timeout_sec 接线回归：握手完成后应用层 idle 检测） ---- */

static int test_idle_timeout_disconnect_after_handshake(void)
{
    printf("  Test 6: Idle timeout disconnects idle client after handshake... ");

    /* idle_timeout_sec=1：握手完成后进入 1s idle 计时；
     * conn_timeout_ms=3000：握手超时路径由 Test 5 覆盖，此处保持默认不受影响。 */
    server_options_t opts = { .conn_timeout_ms = 3000, .idle_timeout_sec = 1 };
    ssn_server_t *srv = ssn_server_create_with_options(TEST_SERVER_ADDR, &opts);
    if (!srv) { printf("FAIL (create server)\n"); return 1; }
    if (!ssn_server_start(srv)) { printf("FAIL (start)\n"); ssn_server_destroy(srv); return 1; }

    pthread_t tid;
    g_srv_running = 1;
    pthread_create(&tid, NULL, server_thread, srv);
    usleep(50000); /* Give server poll time to start */

    ssn_client_t *cli = ssn_client_create();
    if (!cli) { printf("FAIL (create client)\n"); goto cleanup; }

    struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
    if (!ssn_client_connect(cli, TEST_SERVER_ADDR, &ts)) {
        printf("FAIL (connect)\n"); ssn_client_close(cli); goto cleanup;
    }

    /* 保持空闲 2.5 秒（无消息），期间驱动客户端事件循环感知服务端断开。
     * 修复前握手完成后无应用层 idle 检测 → 连接保持 → peer_count 仍为 1 → 断言失败。 */
    for (int i = 0; i < 25; i++) {
        ssn_client_poll(cli, 100);
        usleep(100000);
    }

    int cnt = ssn_server_peer_count(srv);

    ssn_client_close(cli);
    g_srv_running = 0;
    pthread_join(tid, NULL);
    ssn_server_destroy(srv);

    if (cnt != 0) { printf("FAIL (peer count %d, expected 0)\n", cnt); return 1; }
    printf("PASS\n");
    return 0;

cleanup:
    g_srv_running = 0;
    pthread_join(tid, NULL);
    ssn_server_destroy(srv);
    return 1;
}

/* ---- Test 7: 活跃保持（活动重置回归：idle 计时必须在每次收包后重置） ---- */

static int test_active_connection_kept(void)
{
    printf("  Test 7: Active connection kept alive by messages... ");

    server_options_t opts = { .conn_timeout_ms = 3000, .idle_timeout_sec = 1 };
    ssn_server_t *srv = ssn_server_create_with_options(TEST_SERVER_ADDR, &opts);
    if (!srv) { printf("FAIL (create server)\n"); return 1; }
    if (!ssn_server_start(srv)) { printf("FAIL (start)\n"); ssn_server_destroy(srv); return 1; }

    pthread_t tid;
    g_srv_running = 1;
    pthread_create(&tid, NULL, server_thread, srv);
    usleep(50000); /* Give server poll time to start */

    ssn_client_t *cli = ssn_client_create();
    if (!cli) { printf("FAIL (create client)\n"); goto cleanup; }

    struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
    if (!ssn_client_connect(cli, TEST_SERVER_ADDR, &ts)) {
        printf("FAIL (connect)\n"); ssn_client_close(cli); goto cleanup;
    }

    /* 每 300ms 发一条消息，持续 3 秒（> idle 周期 1s）：
     * 若活动重置未实现，连接空闲 1s 即被断开 → 断言失败。 */
    ssn_url_ref_t url = { .url = "/activity", .url_len = 9 };
    const char *msg = "ping";
    ssn_data_ref_t data = { .data = (void *)msg, .length = strlen(msg) };
    for (int i = 0; i < 10; i++) {
        if (ssn_client_message(cli, &url, &data) < 0) {
            printf("FAIL (message send)\n"); ssn_client_close(cli); goto cleanup;
        }
        usleep(300000);
    }

    int cnt = ssn_server_peer_count(srv);

    ssn_client_close(cli);
    g_srv_running = 0;
    pthread_join(tid, NULL);
    ssn_server_destroy(srv);

    if (cnt != 1) { printf("FAIL (peer count %d, expected 1)\n", cnt); return 1; }
    printf("PASS\n");
    return 0;

cleanup:
    g_srv_running = 0;
    pthread_join(tid, NULL);
    ssn_server_destroy(srv);
    return 1;
}

/* ---- Test 8: 空连接不阻塞服务端（回归：Issue #4 accept 后 recv(0) 无限阻塞） ----
 *
 * 缺陷：ipc_server_handle_new_connection 在 accept 后无条件 ssn_transport_recv(..., 0)，
 *       timeout=0 不设置 SO_RCVTIMEO → recv 无限阻塞。任意客户端 connect 后不发数据
 *       即让 poll 线程永久卡死（持锁），整个服务端挂死（单连接 DoS）。
 * 修复：删除 accept 后的立即 recv——新连接 fd 进入 clis 表后，下一轮 poll 的
 *       FD_ISSET 先验（ssn_server_handle_client_input）自然接管，无阻塞风险。
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int test_empty_connection_not_blocking(void)
{
    printf("  Test 8: Empty connection does not block server... ");

    /* TCP 服务端（unix 路径同样受影响，此处用 TCP 复现 Issue 场景） */
    ssn_server_t *srv = ssn_server_create("tcp://127.0.0.1:18951");
    if (!srv) { printf("FAIL (create server)\n"); return 1; }

    ssn_url_ref_t url = { .url = "/echo", .url_len = 5 };
    ssn_server_add_method(srv, &url, echo_handler, NULL);
    if (!ssn_server_start(srv)) { printf("FAIL (start)\n"); ssn_server_destroy(srv); return 1; }

    pthread_t tid;
    g_srv_running = 1;
    pthread_create(&tid, NULL, server_thread, srv);
    usleep(100000);

    /* 1. 恶意空连接：connect 后不发任何数据，保持连接 */
    int bad_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (bad_fd < 0) { printf("FAIL (bad socket)\n"); goto cleanup; }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(18951);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    if (connect(bad_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        printf("FAIL (bad connect)\n"); close(bad_fd); goto cleanup;
    }

    /* 2. 给缺陷留出触发时间（修复前：poll 线程卡死在 recv(0)） */
    usleep(1000000);

    /* 3. 正常客户端应仍能完成 RPC 往返（修复前：无应答 → 红） */
    g_handler_called = 0;
    g_reply_received = 0;
    g_reply_len = 0;

    ssn_client_t *cli = ssn_client_create();
    if (!cli) { printf("FAIL (create client)\n"); close(bad_fd); goto cleanup; }

    struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
    if (!ssn_client_connect(cli, "tcp://127.0.0.1:18951", &ts)) {
        printf("FAIL (connect)\n"); ssn_client_close(cli); close(bad_fd); goto cleanup;
    }

    const char *msg = "alive";
    ssn_data_ref_t req = { .data = (void *)msg, .length = strlen(msg) };
    int ret = ssn_client_call(cli, &url, &req, reply_cb, NULL, 2000);
    if (ret < 0) {
        printf("FAIL (call)\n"); ssn_client_close(cli); close(bad_fd); goto cleanup;
    }

    for (int i = 0; i < 10 && !g_reply_received; i++) {
        ssn_client_poll(cli, 100);
        usleep(50000);
    }

    ssn_client_close(cli);
    close(bad_fd);

    if (!g_reply_received) {
        printf("FAIL (no reply - server blocked by empty connection)\n");
        goto cleanup;
    }
    if (g_reply_len != strlen(msg) || memcmp(g_reply_buf, msg, g_reply_len) != 0) {
        printf("FAIL (reply mismatch)\n"); goto cleanup;
    }

    g_srv_running = 0;
    pthread_join(tid, NULL);
    ssn_server_destroy(srv);
    printf("PASS\n");
    return 0;

cleanup:
    g_srv_running = 0;
    pthread_join(tid, NULL);
    ssn_server_destroy(srv);
    return 1;
}

/* ---- Test 9: conn_timeout_ms=0 握手竞态（回归 Issue #15） ----
 *
 * 缺陷背景：server_options_t 未设置 conn_timeout_ms 时 handshake_timeout=0，
 * cli_init 把 hst.alive 置 0 但仍留在 hst 链表。若客户端在 connect（TCP 建立）
 * 之后、SERVICE_INFO 握手包到达之前停顿（慢启动/负载），定时器首个 tick
 * （alive=0<=50ms）即置 emit → evtfd 销毁该连接——真实世界的「连接建立但
 * 握手迟到」竞态，客户端 connect 偶发失败。
 * 回归：用 transport 层建立裸 TCP 连接（不发握手包），sleep 超过定时器周期后
 * 再发 SERVICE_INFO 握手——修复前连接已被销毁、握手必然失败；修复后
 * handshake_timeout 回退默认值，连接存活到握手完成。
 */
static int test_zero_handshake_timeout(void)
{
    printf("  Test 9: Zero handshake timeout survives delayed handshake... ");

    server_options_t opts = {
        .send_timeout_ms = 5000,
        .conn_timeout_ms = 0,          /* 未设置（缺陷触发路径） */
        .idle_timeout_sec = 60,
        .ifname = ""
    };
    ssn_server_t *srv = ssn_server_create_with_options(TEST_SERVER_ADDR, &opts);
    if (!srv) { printf("FAIL (create)\n"); return 1; }
    if (!ssn_server_start(srv)) {
        printf("FAIL (start)\n"); ssn_server_destroy(srv); return 1;
    }
    g_srv_running = 1;
    g_srv_poll_ms = 10;    /* 快速事件循环：及时处理 accept 与 evtfd */
    pthread_t tid;
    pthread_create(&tid, NULL, server_thread, srv);
    usleep(100000);

    /* 裸 TCP 连接（不发握手）：server accept 后 cli 进入 hst 链表且 alive=0 */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("FAIL (socket)\n");
        g_srv_running = 0; pthread_join(tid, NULL);
        ssn_server_destroy(srv);
        return 1;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    /* 剥离 unix:// 前缀（裸 socket 的 sun_path 不含协议前缀） */
    const char *path = TEST_SERVER_ADDR + strlen("unix://");
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("FAIL (connect)\n");
        close(fd);
        g_srv_running = 0; pthread_join(tid, NULL);
        ssn_server_destroy(srv);
        return 1;
    }

    /* 停顿超过定时器周期（100ms > 50ms tick）：修复前 alive=0 的连接在此被销毁 */
    usleep(200000);

    /* 再发 SERVICE_INFO 握手包（模拟迟到握手；手工拼帧——ssn_create_header
     * 为库内部符号未导出，测试用原始字节构造头部） */
    uint8_t hdr[SSN_HEADER_SIZE];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = SSN_MAGIC_BYTE;         /* magic */
    hdr[1] = SSN_PROTOCOL_VERSION;   /* version */
    hdr[2] = SSN_MSG_TYPE_SERVICE_INFO; /* msg_type */
    ssn_set_seqno((ssn_header_t *)hdr, 1);
    ssize_t n = send(fd, hdr, sizeof(hdr), 0);

    /* 等待服务端应答握手（SERVICE_INFO 应答含 cid） */
    int got_reply = 0;
    for (int i = 0; i < 30; i++) {
        uint8_t buf[512];
        ssize_t r = recv(fd, buf, sizeof(buf), 0);
        if (r > 0) {
            got_reply = 1;
            break;
        }
        usleep(100000);
    }

    close(fd);
    g_srv_running = 0;
    pthread_join(tid, NULL);
    ssn_server_destroy(srv);

    if (!got_reply || n <= 0) {
        printf("FAIL (handshake killed before SERVICE_INFO processed)\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}

/* ---- Test 10: 握手超时到期与对端 FIN 并发（hst 链表 UAF 回归） ----
 *
 * 缺陷背景：ssn_server_cli_destroy 用 cli->hst.alive 判断是否从 hst 链表摘除，
 * 而 alive==0 同时表示「定时器已到期但事件未消费」与「已摘除」二义。竞态：
 * 定时器（独立线程，不依赖 poll）将 alive 置 0 并 signal evtfd → 对端 FIN
 * 触发 recv 0 → destroy 因 alive==0 跳过摘除 → free(cli) → 之后
 * handle_event_input 对已释放节点 DELETE_FROM_LIST + 读 cli->transport → UAF。
 * 回归：主线程手动控制 poll 时机——先让定时器归零 signal（不消费），再 FIN，
 * 最后单次 poll 同时看到 FIN+evtfd 两事件（clis 先处理 → UAF 确定性触发）。
 * 修复前 ASAN 报 heap-use-after-free，修复后全绿。
 */
static int test_handshake_timeout_fin_race(void)
{
    printf("  Test 10: Handshake-timeout + FIN race (hst UAF)... ");

    const int ROUNDS = 20;
    for (int r = 0; r < ROUNDS; r++) {
        server_options_t opts = {
            .send_timeout_ms = 5000,
            .conn_timeout_ms = 60,       /* 短握手超时：~50ms 内 alive 归零并 signal evtfd */
            .idle_timeout_sec = 60,
            .ifname = ""
        };
        ssn_server_t *srv = ssn_server_create_with_options(TEST_SERVER_ADDR, &opts);
        if (!srv) { printf("FAIL (create round %d)\n", r); return 1; }
        if (!ssn_server_start(srv)) {
            printf("FAIL (start round %d)\n", r); ssn_server_destroy(srv); return 1;
        }
        /* 不启动 poll 线程：定时器线程独立运行，主线程手动控制 poll 时机 */

        /* 裸 TCP 连接（不发握手包）：cli 进入 hst 链表，alive=60ms 倒计时 */
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            printf("FAIL (socket round %d)\n", r);
            ssn_server_destroy(srv);
            return 1;
        }
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        const char *path = TEST_SERVER_ADDR + strlen("unix://");
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            printf("FAIL (connect round %d)\n", r);
            close(fd);
            ssn_server_destroy(srv);
            return 1;
        }

        /* 步骤 1：不 poll，等定时器 tick（50ms）把 alive 减到 0 并 signal evtfd */
        usleep(80000);

        /* 步骤 2：对端 FIN（此时 evtfd 已 signal 但未被消费） */
        close(fd);

        /* 步骤 3：单次 poll——pselect 同时返回 cli fd（FIN）与 evtfd →
         * clis 先处理（recv 0 → destroy，alive==0 跳过 hst 摘除 → free），
         * 随后 handle_event_input 遍历残留 hst 节点 → UAF */
        ssn_server_poll(srv, 0);

        /* 再 poll 一轮让事件循环收敛 */
        usleep(100000);
        ssn_server_poll(srv, 0);

        ssn_server_destroy(srv);
    }
    printf("PASS\n");
    return 0;
}

int main(void)
{
    int failed = 0;
    printf("=== Server API Tests ===\n");

    failed += test_create_destroy();
    failed += test_start_stop();
    failed += test_add_method();
    failed += test_rpc_echo();
    failed += test_idle_timeout_disconnect();
    failed += test_idle_timeout_disconnect_after_handshake();
    failed += test_active_connection_kept();
    failed += test_empty_connection_not_blocking();
    failed += test_zero_handshake_timeout();
    failed += test_handshake_timeout_fin_race();

    printf("=== Result: %d/10 passed, %d failed ===\n", 10 - failed, failed);
    return failed ? 1 : 0;
}
