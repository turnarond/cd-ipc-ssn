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

static void *server_thread(void *arg)
{
    ssn_server_t *srv = (ssn_server_t *)arg;
    while (g_srv_running) {
        ssn_server_poll(srv, 100);
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

int main(void)
{
    int failed = 0;
    printf("=== Server API Tests ===\n");

    failed += test_create_destroy();
    failed += test_start_stop();
    failed += test_add_method();
    failed += test_rpc_echo();

    printf("=== Result: %d/4 passed, %d failed ===\n", 4 - failed, failed);
    return failed ? 1 : 0;
}
