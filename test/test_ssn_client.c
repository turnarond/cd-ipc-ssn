/*
 * test_ssn_client.c - Client API functional test
 *
 * Tests: client create/connect/disconnect/close, RPC call, subscribe, message, poll
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
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

int main(void)
{
    int failed = 0;
    printf("=== Client API Tests ===\n");

    failed += test_create_destroy();
    failed += test_connect_disconnect();
    failed += test_rpc_call();
    failed += test_subscribe();
    failed += test_send_message();

    printf("=== Result: %d/5 passed, %d failed ===\n", 5 - failed, failed);
    return failed ? 1 : 0;
}
