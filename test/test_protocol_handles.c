/*
 * test_protocol_handles.c - 协议层 handle 原语单元测试
 *
 * 覆盖 v1.1 事件循环归属收敛（Issue #31，docs/superpowers/specs/
 * 2026-08-24-event-loop-unify-design.md）新增的 4 个 handle 原语：
 *
 *   ssn_rpc_handle_reply    应答帧校验 + 协议池匹配 + 回调触发
 *   ssn_rpc_handle_request  请求帧校验 + on_request 触发
 *   ssn_pubsub_handle_message  PUBLISH 帧校验 + on_message 触发
 *   ssn_msg_handle_data     MESSAGE 帧校验 + on_message 触发
 *
 * 关键验证点（收编边界 v1.1）：
 * - 无 I/O、无锁、纯函数式：对象不绑定 transport 即可调用（不依赖网络）
 * - 帧校验：错误 msg_type 不触发回调（返回 0）
 * - 回调触发：命中时在 handle 返回前同步触发（参数正确）
 * - pending 池：命中后槽位释放；未命中不触发（上层池匹配兜底）
 * - 参数校验：NULL 入参返回 -1
 */

#include "protocol/ssn_protocol.h"
#include "protocol/rpc/ssn_rpc.h"
#include "protocol/pubsub/ssn_pubsub.h"
#include "protocol/msg/ssn_msg.h"
#include "ssn_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_passed = 0;
static int g_tests_failed = 0;

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

/* ---- 回调记录上下文 ---- */
typedef struct {
    bool fired;
    uint16_t seqno;
    uint32_t status;
    char topic[64];
    char data[128];
    size_t data_len;
} cb_ctx_t;

static void cb_ctx_reset(cb_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

static void reply_cb(uint16_t seqno, uint32_t status,
                     const void *data, size_t data_len, void *arg)
{
    cb_ctx_t *ctx = (cb_ctx_t *)arg;
    ctx->fired = true;
    ctx->seqno = seqno;
    ctx->status = status;
    if (data && data_len > 0) {
        size_t n = data_len < sizeof(ctx->data) - 1 ? data_len : sizeof(ctx->data) - 1;
        memcpy(ctx->data, data, n);
        ctx->data[n] = '\0';
        ctx->data_len = n;
    }
}

static void request_cb(uint16_t seqno, const char *method,
                       const void *data, size_t data_len, void *arg)
{
    cb_ctx_t *ctx = (cb_ctx_t *)arg;
    ctx->fired = true;
    ctx->seqno = seqno;
    if (method) {
        strncpy(ctx->topic, method, sizeof(ctx->topic) - 1);
    }
    if (data && data_len > 0) {
        size_t n = data_len < sizeof(ctx->data) - 1 ? data_len : sizeof(ctx->data) - 1;
        memcpy(ctx->data, data, n);
        ctx->data[n] = '\0';
        ctx->data_len = n;
    }
}

static void pubsub_cb(const char *topic, const void *data,
                      size_t data_len, void *arg)
{
    cb_ctx_t *ctx = (cb_ctx_t *)arg;
    ctx->fired = true;
    if (topic) {
        strncpy(ctx->topic, topic, sizeof(ctx->topic) - 1);
    }
    if (data && data_len > 0) {
        size_t n = data_len < sizeof(ctx->data) - 1 ? data_len : sizeof(ctx->data) - 1;
        memcpy(ctx->data, data, n);
        ctx->data[n] = '\0';
        ctx->data_len = n;
    }
}

static void msg_cb(const void *data, size_t data_len, void *arg)
{
    cb_ctx_t *ctx = (cb_ctx_t *)arg;
    ctx->fired = true;
    if (data && data_len > 0) {
        size_t n = data_len < sizeof(ctx->data) - 1 ? data_len : sizeof(ctx->data) - 1;
        memcpy(ctx->data, data, n);
        ctx->data[n] = '\0';
        ctx->data_len = n;
    }
}

/* ---- 帧构造：url 紧随头部，data 紧随 url（与 ssn_get_url/ssn_get_data 布局一致） ---- */
static ssn_header_t *make_frame(uint8_t type, uint32_t status, uint16_t seqno,
                                const char *url, const void *data, size_t data_len,
                                uint8_t *buf)
{
    ssn_header_t *hdr = ssn_create_header(buf, type, status, seqno);
    size_t off = SSN_HEADER_SIZE;

    if (url && url[0]) {
        size_t n = strlen(url);
        memcpy(buf + off, url, n);
        ssn_set_url_length(hdr, (uint16_t)n);
        off += n;
    }
    if (data && data_len > 0) {
        memcpy(buf + off, data, data_len);
        ssn_set_data_length(hdr, (uint32_t)data_len);
    }
    return hdr;
}

/* ========================================================================
 * ssn_rpc_handle_reply
 * ======================================================================== */

static void test_handle_reply_args(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_RPC_REQUEST, 0, 1, NULL, "x", 1, buf);
    ssn_rpc_req_t *req = ssn_rpc_req_create(reply_cb, NULL);

    ASSERT(ssn_rpc_handle_reply(NULL, hdr) == -1, "reply: NULL req -> -1");
    ASSERT(ssn_rpc_handle_reply(req, NULL) == -1, "reply: NULL hdr -> -1");

    ssn_rpc_destroy((ssn_protocol_ctx_t *)req);
}

static void test_handle_reply_wrong_type(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_MESSAGE, 0, 1, NULL, "x", 1, buf);
    cb_ctx_t ctx;
    ssn_rpc_req_t *req = ssn_rpc_req_create(reply_cb, &ctx);

    cb_ctx_reset(&ctx);
    ASSERT(ssn_rpc_handle_reply(req, hdr) == 0, "reply: 非 RPC 应答帧 -> 0");
    ASSERT(ctx.fired == false, "reply: 非 RPC 应答帧不触发回调");

    ssn_rpc_destroy((ssn_protocol_ctx_t *)req);
}

static void test_handle_reply_no_pending(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    /* 有效应答帧但无匹配 pending（协议池为空属预期，上层池匹配兜底） */
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_RPC_REQUEST, 0, 99, NULL, "x", 1, buf);
    cb_ctx_t ctx;
    ssn_rpc_req_t *req = ssn_rpc_req_create(reply_cb, &ctx);

    cb_ctx_reset(&ctx);
    ASSERT(ssn_rpc_handle_reply(req, hdr) == 0, "reply: 无匹配 pending -> 0");
    ASSERT(ctx.fired == false, "reply: 无匹配 pending 不触发回调");

    ssn_rpc_destroy((ssn_protocol_ctx_t *)req);
}

static void test_handle_reply_dispatch(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    const char payload[] = "reply-payload";
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_RPC_REQUEST, 0xABCD, 42, NULL,
                                   payload, sizeof(payload) - 1, buf);
    cb_ctx_t ctx;
    ssn_rpc_req_t *req = ssn_rpc_req_create(reply_cb, &ctx);

    /* 注入协议池 pending 槽（与 ssn_rpc_call 登记形态一致；白盒测试） */
    req->pending_pool[0].in_use = true;
    req->pending_pool[0].seqno = 42;
    req->pending_pool[0].callback = reply_cb;
    req->pending_pool[0].arg = &ctx;

    cb_ctx_reset(&ctx);
    ASSERT(ssn_rpc_handle_reply(req, hdr) == 1, "reply: 命中 pending -> 1");
    ASSERT(ctx.fired == true, "reply: 命中触发回调");
    ASSERT(ctx.seqno == 42, "reply: 回调 seqno 正确");
    ASSERT(ctx.status == 0xABCD, "reply: 回调 status 正确");
    ASSERT(strcmp(ctx.data, payload) == 0, "reply: 回调 data 正确");
    ASSERT(req->pending_pool[0].in_use == false, "reply: 命中后槽位释放");

    ssn_rpc_destroy((ssn_protocol_ctx_t *)req);
}

/* ========================================================================
 * ssn_rpc_handle_request
 * ======================================================================== */

static void test_handle_request_args(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_RPC_REQUEST, 0, 1, "add", NULL, 0, buf);
    ssn_rpc_rep_t *rep = ssn_rpc_rep_create(request_cb, NULL);

    ASSERT(ssn_rpc_handle_request(NULL, hdr) == -1, "request: NULL rep -> -1");
    ASSERT(ssn_rpc_handle_request(rep, NULL) == -1, "request: NULL hdr -> -1");

    ssn_rpc_destroy((ssn_protocol_ctx_t *)rep);
}

static void test_handle_request_wrong_type(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_MESSAGE, 0, 1, NULL, "x", 1, buf);
    cb_ctx_t ctx;
    ssn_rpc_rep_t *rep = ssn_rpc_rep_create(request_cb, &ctx);

    cb_ctx_reset(&ctx);
    ASSERT(ssn_rpc_handle_request(rep, hdr) == 0, "request: 非 RPC 请求帧 -> 0");
    ASSERT(ctx.fired == false, "request: 非 RPC 请求帧不触发回调");

    ssn_rpc_destroy((ssn_protocol_ctx_t *)rep);
}

static void test_handle_request_no_onrequest(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_RPC_REQUEST, 0, 1, "add", NULL, 0, buf);
    ssn_rpc_rep_t *rep = ssn_rpc_rep_create(NULL, NULL);

    ASSERT(ssn_rpc_handle_request(rep, hdr) == 0, "request: 无 on_request -> 0");

    ssn_rpc_destroy((ssn_protocol_ctx_t *)rep);
}

static void test_handle_request_dispatch(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    const char method[] = "add";
    const char body[] = "1+2";
    cb_ctx_t ctx;
    ssn_rpc_rep_t *rep = ssn_rpc_rep_create(request_cb, &ctx);

    /* 帧 1：仅方法名（buf 清零 → 方法名后跟 NUL，适配非长度前缀回调；
     * 线上 url 为长度前缀非 NUL 结尾，此处为测试便利） */
    memset(buf, 0, sizeof(buf));
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_RPC_REQUEST, 0, 7, method, NULL, 0, buf);
    cb_ctx_reset(&ctx);
    ASSERT(ssn_rpc_handle_request(rep, hdr) == 1, "request: 分发 -> 1");
    ASSERT(ctx.fired == true, "request: 触发 on_request");
    ASSERT(ctx.seqno == 7, "request: on_request seqno 正确");
    ASSERT(strcmp(ctx.topic, method) == 0, "request: on_request method 正确");

    /* 帧 2：带数据（方法名后紧跟数据、非 NUL 结尾——与线上格式一致；
     * 回调按数据长度拷贝，不受影响） */
    hdr = make_frame(SSN_MSG_TYPE_RPC_REQUEST, 0, 8, method, body, sizeof(body) - 1, buf);
    cb_ctx_reset(&ctx);
    ASSERT(ssn_rpc_handle_request(rep, hdr) == 1, "request: 分发（带数据）-> 1");
    ASSERT(ctx.fired == true, "request: 触发 on_request（带数据）");
    ASSERT(strcmp(ctx.data, body) == 0, "request: on_request data 正确");

    ssn_rpc_destroy((ssn_protocol_ctx_t *)rep);
}

/* ========================================================================
 * ssn_pubsub_handle_message
 * ======================================================================== */

static void test_handle_message_args(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_PUBLISH, 0, 0, "news", NULL, 0, buf);
    ssn_pubsub_sub_t *sub = ssn_pubsub_sub_create(pubsub_cb, NULL);

    ASSERT(ssn_pubsub_handle_message(NULL, hdr) == -1, "message: NULL sub -> -1");
    ASSERT(ssn_pubsub_handle_message(sub, NULL) == -1, "message: NULL hdr -> -1");

    ssn_pubsub_destroy((ssn_protocol_ctx_t *)sub);
}

static void test_handle_message_wrong_type(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_MESSAGE, 0, 0, NULL, "x", 1, buf);
    cb_ctx_t ctx;
    ssn_pubsub_sub_t *sub = ssn_pubsub_sub_create(pubsub_cb, &ctx);

    cb_ctx_reset(&ctx);
    ASSERT(ssn_pubsub_handle_message(sub, hdr) == 0, "message: 非 PUBLISH 帧 -> 0");
    ASSERT(ctx.fired == false, "message: 非 PUBLISH 帧不触发回调");

    ssn_pubsub_destroy((ssn_protocol_ctx_t *)sub);
}

static void test_handle_message_no_callback(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_PUBLISH, 0, 0, "news", NULL, 0, buf);
    ssn_pubsub_sub_t *sub = ssn_pubsub_sub_create(NULL, NULL);

    ASSERT(ssn_pubsub_handle_message(sub, hdr) == 0, "message: 无 on_message -> 0");

    ssn_pubsub_destroy((ssn_protocol_ctx_t *)sub);
}

static void test_handle_message_dispatch(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    const char topic[] = "news";
    const char body[] = "hello";
    cb_ctx_t ctx;
    ssn_pubsub_sub_t *sub = ssn_pubsub_sub_create(pubsub_cb, &ctx);

    /* 帧 1：仅主题（buf 清零 → 主题后跟 NUL） */
    memset(buf, 0, sizeof(buf));
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_PUBLISH, 0, 0, topic, NULL, 0, buf);
    cb_ctx_reset(&ctx);
    ASSERT(ssn_pubsub_handle_message(sub, hdr) == 1, "message: 分发 -> 1");
    ASSERT(ctx.fired == true, "message: 触发 on_message");
    ASSERT(strcmp(ctx.topic, topic) == 0, "message: on_message topic 正确");

    /* 帧 2：带数据（主题后紧跟数据、非 NUL 结尾——与线上格式一致） */
    hdr = make_frame(SSN_MSG_TYPE_PUBLISH, 0, 0, topic, body, sizeof(body) - 1, buf);
    cb_ctx_reset(&ctx);
    ASSERT(ssn_pubsub_handle_message(sub, hdr) == 1, "message: 分发（带数据）-> 1");
    ASSERT(ctx.fired == true, "message: 触发 on_message（带数据）");
    ASSERT(strcmp(ctx.data, body) == 0, "message: on_message data 正确");

    ssn_pubsub_destroy((ssn_protocol_ctx_t *)sub);
}

/* ========================================================================
 * ssn_msg_handle_data
 * ======================================================================== */

static void test_handle_data_args(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_MESSAGE, 0, 1, NULL, "x", 1, buf);
    ssn_msg_recv_t *recv = ssn_msg_recv_create(msg_cb, NULL);

    ASSERT(ssn_msg_handle_data(NULL, hdr) == -1, "data: NULL recv -> -1");
    ASSERT(ssn_msg_handle_data(recv, NULL) == -1, "data: NULL hdr -> -1");

    ssn_msg_destroy((ssn_protocol_ctx_t *)recv);
}

static void test_handle_data_wrong_type(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_PUBLISH, 0, 0, "news", "x", 1, buf);
    cb_ctx_t ctx;
    ssn_msg_recv_t *recv = ssn_msg_recv_create(msg_cb, &ctx);

    cb_ctx_reset(&ctx);
    ASSERT(ssn_msg_handle_data(recv, hdr) == 0, "data: 非 MESSAGE 帧 -> 0");
    ASSERT(ctx.fired == false, "data: 非 MESSAGE 帧不触发回调");

    ssn_msg_destroy((ssn_protocol_ctx_t *)recv);
}

static void test_handle_data_no_callback(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_MESSAGE, 0, 1, NULL, "x", 1, buf);
    ssn_msg_recv_t *recv = ssn_msg_recv_create(NULL, NULL);

    ASSERT(ssn_msg_handle_data(recv, hdr) == 0, "data: 无 on_message -> 0");

    ssn_msg_destroy((ssn_protocol_ctx_t *)recv);
}

static void test_handle_data_dispatch(void)
{
    uint8_t buf[SSN_MAX_PACKET_SIZE];
    const char body[] = "ping";
    ssn_header_t *hdr = make_frame(SSN_MSG_TYPE_MESSAGE, 0, 1, NULL,
                                   body, sizeof(body) - 1, buf);
    cb_ctx_t ctx;
    ssn_msg_recv_t *recv = ssn_msg_recv_create(msg_cb, &ctx);

    cb_ctx_reset(&ctx);
    ASSERT(ssn_msg_handle_data(recv, hdr) == 1, "data: 分发 -> 1");
    ASSERT(ctx.fired == true, "data: 触发 on_message");
    ASSERT(strcmp(ctx.data, body) == 0, "data: on_message data 正确");

    ssn_msg_destroy((ssn_protocol_ctx_t *)recv);
}

/* ========================================================================
 * 无 transport 可用性（纯状态机，无 I/O）
 * ======================================================================== */

static void test_handle_no_transport_needed(void)
{
    ssn_rpc_req_t *req = ssn_rpc_req_create(reply_cb, NULL);
    ssn_rpc_rep_t *rep = ssn_rpc_rep_create(request_cb, NULL);
    ssn_pubsub_sub_t *sub = ssn_pubsub_sub_create(pubsub_cb, NULL);
    ssn_msg_recv_t *recv = ssn_msg_recv_create(msg_cb, NULL);

    /* 四个对象均未绑定/连接 transport */
    ASSERT(req->base.transport == NULL, "no-transport: rpc_req 未绑定");
    ASSERT(rep->base.transport == NULL, "no-transport: rpc_rep 未绑定");
    ASSERT(sub->base.transport == NULL, "no-transport: pubsub_sub 未绑定");
    ASSERT(recv->base.transport == NULL, "no-transport: msg_recv 未绑定");

    ssn_rpc_destroy((ssn_protocol_ctx_t *)req);
    ssn_rpc_destroy((ssn_protocol_ctx_t *)rep);
    ssn_pubsub_destroy((ssn_protocol_ctx_t *)sub);
    ssn_msg_destroy((ssn_protocol_ctx_t *)recv);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("========================================\n");
    printf("SSN Protocol Handle Primitives Tests\n");
    printf("========================================\n\n");

    printf("Running handle_reply tests...\n");
    test_handle_reply_args();
    test_handle_reply_wrong_type();
    test_handle_reply_no_pending();
    test_handle_reply_dispatch();

    printf("\nRunning handle_request tests...\n");
    test_handle_request_args();
    test_handle_request_wrong_type();
    test_handle_request_no_onrequest();
    test_handle_request_dispatch();

    printf("\nRunning handle_message (pubsub) tests...\n");
    test_handle_message_args();
    test_handle_message_wrong_type();
    test_handle_message_no_callback();
    test_handle_message_dispatch();

    printf("\nRunning handle_data (msg) tests...\n");
    test_handle_data_args();
    test_handle_data_wrong_type();
    test_handle_data_no_callback();
    test_handle_data_dispatch();

    printf("\nRunning no-transport availability tests...\n");
    test_handle_no_transport_needed();

    printf("\n========================================\n");
    printf("Test Results: %d passed, %d failed\n",
           g_tests_passed, g_tests_failed);
    printf("========================================\n");

    return g_tests_failed > 0 ? 1 : 0;
}
