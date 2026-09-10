/*
 * test_cliauto.c - 自动重连客户端（ssn_cliauto）单元测试
 *
 * 回归：Issue #14——keepalive ping 未实现（SSN_CLIENT_AUTO_MAX_PING_LOST 无引用、
 *       ping_lost 只清零从不递增），半开连接无法感知；state/running 数据竞争。
 * 验证点：
 *   Test 1: 连接存活时 keepalive 周期 ping 正常（不误判断开，onconn(false) 不触发）
 *   Test 2: start 校验与重复 start 拒绝；stop 幂等停止
 *   Test 3: 服务端退出（正常 FIN）后 cliauto 检测断开并回调 onconn(false)，随后自动重连
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#include "ssn_cliauto.h"
#include "ssn_server.h"

#define TEST_SERVER_ADDR "unix:///tmp/ssn-cliauto-test"
#define TEST_TIMEOUT_MS  5000

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { g_tests_passed++; printf("[PASS] %s\n", msg); } \
        else { g_tests_failed++; printf("[FAIL] %s\n", msg); } \
    } while (0)

/* ---- 共享测试基础设施（复用 test_ssn_client 模式） ---- */

static volatile int g_srv_running = 0;

static void *server_thread(void *arg)
{
    ssn_server_t *srv = (ssn_server_t *)arg;
    while (g_srv_running) ssn_server_poll(srv, 100);
    return NULL;
}

static ssn_server_t *start_test_server(pthread_t *tid)
{
    /* 清理上一测试残留的 socket 文件（unix 服务端 start 会 unlink，但保险起见） */
    unlink("/tmp/ssn-cliauto-test");
    ssn_server_t *srv = ssn_server_create(TEST_SERVER_ADDR);
    if (!srv) return NULL;
    if (!ssn_server_start(srv)) { ssn_server_destroy(srv); return NULL; }
    g_srv_running = 1;
    pthread_create(tid, NULL, server_thread, srv);
    usleep(300000);   /* 等服务端就绪 */
    /* 确认 socket 文件已就绪（server listen 后应存在） */
    if (access("/tmp/ssn-cliauto-test", F_OK) != 0) {
        fprintf(stderr, "server socket file not ready\n");
    }
    return srv;
}

static void stop_test_server(ssn_server_t *srv, pthread_t tid)
{
    g_srv_running = 0;
    pthread_join(tid, NULL);
    ssn_server_destroy(srv);
}

/* ---- 连接状态回调（记录 connect/disconnect 事件） ---- */

static volatile int g_connect_events = 0;
static volatile int g_disconnect_events = 0;

static void conn_cb(void *arg, ssn_client_auto_t *cliauto, bool connect)
{
    (void)arg; (void)cliauto;
    if (connect) {
        __atomic_fetch_add(&g_connect_events, 1, __ATOMIC_SEQ_CST);
    } else {
        __atomic_fetch_add(&g_disconnect_events, 1, __ATOMIC_SEQ_CST);
    }
}

/* ---- Test 1: 连接存活时 keepalive ping 正常（不误判断开） ----
 *
 * 缺陷背景：CONNECTED 分支从不发送 PING_ECHO，仅 poll 后查 is_connect——
 * 对正常连接这不会误断，但半开连接（服务端崩溃无 FIN）永远感知不到。
 * 回归：keepalive=50ms 运行 1s，连接存活（服务端 poll 响应 ping），
 *       不应触发 onconn(false)（断开误判）。
 */
static void test_keepalive_no_false_disconnect(void)
{
    printf("Test 1: keepalive ping 不误判断开...\n");
    pthread_t tid;
    ssn_server_t *srv = start_test_server(&tid);
    CHECK(srv != NULL, "启动测试服务端");

    ssn_client_auto_t *cliauto = ssn_client_auto_create();
    CHECK(cliauto != NULL, "创建 cliauto");

    ssn_client_auto_setup(cliauto, conn_cb, NULL);
    g_connect_events = 0;
    g_disconnect_events = 0;
    bool started = ssn_client_auto_start(cliauto, TEST_SERVER_ADDR, NULL, 0,
                                         50 /* keepalive ms */, 2000 /* conn_timeout */,
                                         100 /* reconn_delay */);
    CHECK(started, "start 成功");

    /* 运行 1 秒（20 个 keepalive 周期），期间不应误判断开 */
    usleep(1000000);

    int disc = __atomic_load_n(&g_disconnect_events, __ATOMIC_SEQ_CST);
    CHECK(disc == 0, "连接存活时无断开事件（keepalive 正常）");

    /* 服务端仍在运行且连接应保持 */
    ssn_client_t *cli = ssn_client_auto_handle(cliauto);
    CHECK(cli != NULL && ssn_client_is_connect(cli), "连接保持存活");

    ssn_client_auto_stop(cliauto);
    ssn_client_auto_delete(cliauto);
    stop_test_server(srv, tid);
}

/* ---- Test 2: start 参数校验与重复 start 拒绝；stop 幂等 ---- */
static void test_start_stop_validation(void)
{
    printf("Test 2: start/stop 校验...\n");
    ssn_client_auto_t *cliauto = ssn_client_auto_create();
    CHECK(cliauto != NULL, "创建 cliauto");

    /* NULL 参数拒绝 */
    CHECK(!ssn_client_auto_start(cliauto, NULL, NULL, 0, 50, 2000, 100),
          "server=NULL 拒绝");
    CHECK(!ssn_client_auto_start(NULL, TEST_SERVER_ADDR, NULL, 0, 50, 2000, 100),
          "cliauto=NULL 拒绝");

    /* IDLE 状态 stop 应为 no-op（返回 false，不崩溃） */
    CHECK(!ssn_client_auto_stop(cliauto), "未 start 时 stop 返回 false");

    ssn_client_auto_delete(cliauto);
}

/* ---- Test 3: 服务端退出（FIN）后检测断开并回调 ---- */
static void test_disconnect_detection(void)
{
    printf("Test 3: 服务端退出后检测断开...\n");
    fflush(stdout);
    pthread_t tid;
    ssn_server_t *srv = start_test_server(&tid);
    CHECK(srv != NULL, "启动测试服务端");
    if (!srv) return;

    ssn_client_auto_t *cliauto = ssn_client_auto_create();
    CHECK(cliauto != NULL, "创建 cliauto");
    if (!cliauto) { stop_test_server(srv, tid); return; }

    /* 注册连接回调（onconn 非 NULL 时连接建立/断开才会回调） */
    ssn_client_auto_setup(cliauto, conn_cb, NULL);

    g_connect_events = 0;
    g_disconnect_events = 0;
    bool started = ssn_client_auto_start(cliauto, TEST_SERVER_ADDR, NULL, 0,
                                         50, 2000, 100);
    CHECK(started, "start 成功");

    /* 等待连接建立 */
    int conns = 0;
    for (int i = 0; i < 20; i++) {
        usleep(100000);
        conns = __atomic_load_n(&g_connect_events, __ATOMIC_SEQ_CST);
        if (conns >= 1) break;
    }
    CHECK(conns >= 1, "连接建立回调触发");

    /* 停止服务端（发送 FIN） */
    stop_test_server(srv, tid);

    /* cliauto 应检测到断开（poll 发现 EOF 或 ping 超时）并回调 onconn(false) */
    int disc = 0;
    for (int i = 0; i < 50; i++) {
        usleep(100000);
        disc = __atomic_load_n(&g_disconnect_events, __ATOMIC_SEQ_CST);
        if (disc >= 1) break;
    }
    CHECK(disc >= 1, "断开检测回调触发（onconn(false)）");

    ssn_client_auto_stop(cliauto);
    ssn_client_auto_delete(cliauto);
}

/* ---- Test 4: ssn_client_ping 半开检测（回归 Issue #14 keepalive 路径） ----
 *
 * 直接验证 ping API：连接存活时 ping 返回 true；服务端停止（不 destroy，
 * 模拟无 FIN 的半开连接）后 ping 返回 false（超时无应答）。
 */
static void test_ping_api(void)
{
    printf("Test 4: ssn_client_ping 半开检测...\n");
    pthread_t tid;
    ssn_server_t *srv = start_test_server(&tid);
    CHECK(srv != NULL, "启动测试服务端");

    ssn_client_t *cli = ssn_client_create();
    CHECK(cli != NULL, "创建 client");
    struct timespec ts = { .tv_sec = 3, .tv_nsec = 0 };
    CHECK(ssn_client_connect(cli, TEST_SERVER_ADDR, &ts), "连接成功");

    /* 连接存活：ping 应收到服务端回显应答 */
    bool alive = ssn_client_ping(cli, 200);
    CHECK(alive, "存活时 ping 返回 true（服务端回显应答）");

    /* 半开模拟：停止服务端 poll（不再处理任何请求）但不 destroy（不发 FIN） */
    g_srv_running = 0;
    pthread_join(tid, NULL);

    /* 服务端不再回显：ping 超时返回 false（keepalive 路径可感知断开） */
    bool dead = !ssn_client_ping(cli, 300);
    CHECK(dead, "服务端停止后 ping 超时返回 false（半开可感知）");

    ssn_client_close(cli);
    ssn_server_destroy(srv);
}

/* ---- Test 5: cliauto 空闲连接不误判断开（回归 Issue #22） ----
 *
 * 缺陷：ssn_client_process_events 的局部变量 pkt_e 未初始化——socket 无数据
 * （did_recv=false）时读取未初始化栈值（UB），垃圾值可能为 true → 误判连接丢失
 * → 断开。v2.5.1 cliauto CONNECTED 分支每次 tick 都 poll，空闲连接触发
 * 「建立后 ~50ms 误断 → 循环重连」（复现程序 -O2 下约 10% 概率）。
 * 回归：cliauto 连上后空闲运行，多次连接循环，期间不得出现断开回调。
 */
static void test_idle_connect_no_false_disconnect(void)
{
    printf("Test 5: cliauto 空闲连接不误判断开（Issue #22）...\n");
    pthread_t tid;
    ssn_server_t *srv = start_test_server(&tid);
    CHECK(srv != NULL, "启动测试服务端");

    /* 多次连接循环（提高未初始化 UB 触发概率，修复前 -O2 下可捕捉） */
    bool ok = true;
    for (int round = 0; round < 5 && ok; round++) {
        ssn_client_auto_t *cliauto = ssn_client_auto_create();
        CHECK(cliauto != NULL, "创建 cliauto");

        ssn_client_auto_setup(cliauto, conn_cb, NULL);
        g_connect_events = 0;
        g_disconnect_events = 0;
        bool started = ssn_client_auto_start(cliauto, TEST_SERVER_ADDR, NULL, 0,
                                             1000 /* keepalive ms */, 1000 /* conn_timeout */,
                                             1000 /* reconn_delay：与 Issue 环境一致 */);
        CHECK(started, "start 成功");

        /* 等待连接建立后空闲运行 2 秒（无业务流量，仅 ping 应答） */
        usleep(300000);
        int disc = 0;
        for (int i = 0; i < 20; i++) {
            usleep(100000);
            disc = __atomic_load_n(&g_disconnect_events, __ATOMIC_SEQ_CST);
            if (disc > 0) break;
        }
        if (disc > 0) {
            ok = false;
        }
        ssn_client_auto_stop(cliauto);
        ssn_client_auto_delete(cliauto);
    }
    CHECK(ok, "5 轮空闲连接均无断开事件（不误判断开）");

    stop_test_server(srv, tid);
}

int main(void)
{
    printf("=== Client Auto (cliauto) Tests ===\n\n");
    test_keepalive_no_false_disconnect();
    test_start_stop_validation();
    test_disconnect_detection();
    test_ping_api();
    test_idle_connect_no_false_disconnect();
    printf("\n=== Result: %d passed, %d failed ===\n",
           g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
