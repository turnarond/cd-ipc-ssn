/*
 * ipc_client.h
 */

#ifndef IPC_CLIENT_H
#define IPC_CLIENT_H

#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include "ipc_protocol.h"
#include "ipc_global.h"

/* Client timer period (ms) */
#define IPC_CLIENT_TIMER_PERIOD  10

/* Client default timeout (ms) */
#define IPC_CLIENT_DEF_TIMEOUT  60000

/* Client default send timeout (ms) */
#define IPC_CLIENT_DEF_SEND_TIMEOUT  500

#ifdef __cplusplus
extern "C" {
#endif

/* Client on message callback (received subscribed messages) */
typedef void (*ipc_client_msg_func_t)(void *arg, ipc_client_t *client, ipc_url_ref_t *url, ipc_payload_ref_t *payload);

/* Client RPC callback (`ipc_hdr` NULL means server not responding)
 * The memory pointed to by `ipc_hdr` and `payload` will be invalidated when the callback function returns */
typedef void (*ipc_client_rpc_func_t)(void *arg, ipc_client_t *client, ipc_header_t *ipc_hdr, ipc_payload_ref_t *payload);

/* Client subscribe, unsubscribe and ping callback */
typedef void (*ipc_client_res_func_t)(void *arg, ipc_client_t *client, bool success);

/* Client on datagram callback same as on message */
typedef void (*ipc_client_dat_func_t)(void *arg, ipc_client_t *client, ipc_url_ref_t *url, ipc_payload_ref_t *payload);

/* Create IPC client 
 * Warning: This function must be mutually exclusive with the ipc_client_close() call */
ipc_client_t *ipc_client_create(ipc_client_msg_func_t onmsg, void *arg);

/* Close IPC client 
 * Warning: This function must be mutually exclusive with the ipc_client_create() call */
void ipc_client_close(ipc_client_t *client);

/* Connect to server (Synchronous) */
bool ipc_client_connect(ipc_client_t *client, const char* ipc_path,
                         const struct timespec *timeout);

/* Disconnect from server
 * After disconnect, the `ipc_client_connect` function can be called again */
bool ipc_client_disconnect(ipc_client_t *client);

/* IPC client is connect with server */
bool ipc_client_is_connect(ipc_client_t *client);

/* IPC client send timeout
 * `timeout` NULL means use IPC_CLIENT_DEF_SEND_TIMEOUT */
bool ipc_client_send_timeout(ipc_client_t *client, const int timeout_ms);

int ipc_client_poll(ipc_client_t *client, uint64_t timeout_ms);

void ipc_client_run(ipc_client_t *client);

/* Subscribe URL */
bool ipc_client_subscribe(ipc_client_t *client, const ipc_url_ref_t *url,
                           ipc_client_res_func_t callback, void *arg, const struct timespec *timeout);

/* Unsubscribe URL */
bool ipc_client_unsubscribe(ipc_client_t *client, const ipc_url_ref_t *url,
                             ipc_client_res_func_t callback, void *arg, const struct timespec *timeout);

/* RPC call
 * This function is an asynchronous RPC call.
 * If you need a synchronous RPC call,
 * you can use RPC call synchronization extension interface. */
bool ipc_client_call(ipc_client_t *client, const ipc_url_ref_t *url, const ipc_payload_ref_t *payload,
                      ipc_client_rpc_func_t callback, void *arg, const struct timespec *timeout);

/* Send datagram to server */
bool ipc_client_datagram(ipc_client_t *client, const ipc_url_ref_t *url, const ipc_payload_ref_t *payload);

/* IPC client set on datagram callback */
void ipc_client_set_on_datagram(ipc_client_t *client, ipc_client_dat_func_t callback, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* IPC_CLIENT_H */
/*
 * end
 */
