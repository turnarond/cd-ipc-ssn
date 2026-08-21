/*
 * ssn_client.h
 */

#ifndef SSN_CLIENT_H
#define SSN_CLIENT_H

#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include "ssn_export.h"
#include "ssn_frame.h"
#include "ssn_global.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Client RPC callback (`ipc_hdr` NULL means server not responding)
 * The memory pointed to by `ipc_hdr` and `data` will be invalidated when the callback function returns */
typedef void (*ssn_client_rpcreply_handler_t)(ssn_client_t *client, ssn_header_t *ipc_hdr, ssn_data_ref_t *data, void *arg);

/* Client subscribe, unsubscribe and ping callback */
typedef void (*ssn_client_result_handler_t)(ssn_client_t *client, bool success, void *arg);

/* Client on message callback, for subscribe and onmessage. */
typedef void (*ssn_client_msg_handler_t)(ssn_client_t *client, ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg);

/* Create SSN client, Callback for subscribed (published) messages */
SSN_API ssn_client_t *ssn_client_create();

/* Close SSN client */
SSN_API void ssn_client_close(ssn_client_t *client);

/* 引用计数接口（内部使用：node 层 poll 保活等；公开以便跨模块正确调用，
 * 缺陷背景：未声明导致 node 层隐式声明（UB 风险）） */
SSN_API void ssn_client_ref(ssn_client_t *client);
SSN_API void ssn_client_unref(ssn_client_t *client);

/* Connect to server (Synchronous) */
SSN_API bool ssn_client_connect(ssn_client_t *client, const char* ipc_path,
                         const struct timespec *timeout);

/* Disconnect from server
 * After disconnect, the `ssn_client_connect` function can be called again */
SSN_API bool ssn_client_disconnect(ssn_client_t *client);

/* SSN client is connect with server */
SSN_API bool ssn_client_is_connect(ssn_client_t *client);

/* SSN client send timeout
 * `timeout` NULL means use SSN_DEF_SEND_TIMEOUT */
SSN_API bool ssn_client_send_timeout(ssn_client_t *client, const int timeout_ms);

SSN_API int ssn_client_poll(ssn_client_t *client, uint64_t timeout_ms);

SSN_API void ssn_client_run(ssn_client_t *client);

/* Subscribe URL */
SSN_API bool ssn_client_subscribe(ssn_client_t *client, const ssn_url_ref_t *url,
                           ssn_client_msg_handler_t callback, void *arg, uint64_t timeout_ms);

/* Unsubscribe URL */
SSN_API bool ssn_client_unsubscribe(ssn_client_t *client, const ssn_url_ref_t *url, uint64_t timeout_ms);

/* RPC call */
SSN_API int ssn_client_call(ssn_client_t *client, const ssn_url_ref_t *url, const ssn_data_ref_t *data,
                      ssn_client_rpcreply_handler_t callback, void *arg, uint64_t timeout_ms);

/* Send message to server */
SSN_API int ssn_client_message(ssn_client_t *client, const ssn_url_ref_t *url, const ssn_data_ref_t *data);

/* SSN client set MESSAGE-type handler (for error / unhandled messages) */
SSN_API void ssn_client_set_on_message(ssn_client_t *client, ssn_client_msg_handler_t callback, void *arg);

/* SSN client set PUBLISH-type handler (for incoming publish messages).
 * This is typically set internally by ssn_client_auto, not by user code. */
SSN_API void ssn_client_set_on_publish(ssn_client_t *client, ssn_client_msg_handler_t callback, void *arg);

/* 保活 ping：发送 PING_ECHO 并登记 pending 等待服务端应答（用于半开连接检测）。
 * 返回 true 表示请求已发出且应答已收到（连接存活）；false 表示未连接/发送失败/
 * 超时无应答（连接可能已死）。timeout_ms 为等待应答的窗口。 */
SSN_API bool ssn_client_ping(ssn_client_t *client, uint64_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* SSN_CLIENT_H */
/*
 * end
 */
