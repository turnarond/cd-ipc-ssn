/*
 * IPC server
 */

#ifndef IPC_SERVER_H
#define IPC_SERVER_H

#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include "ipc_protocol.h"
#include "ipc_global.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Remote client ID */
typedef uint32_t  cli_id_t;

typedef struct {
    uint64_t send_timeout_ms; 
    uint64_t conn_timeout_ms; 
    uint64_t idle_timeout_sec;
    char ifname[IF_NAMESIZE];     // 指定绑定的硬件接口名
} server_options_t;

/* Server on client connect or lost callback */
typedef void (*ipc_on_connect_t)(ipc_server_t *server, cli_id_t id, bool connect, void *arg);

/* Server command callback
 * NOTICE: Can not remove listener in callback
 *         `ipc_hdr`, `url` and `payload` are invalid when this function returns */
typedef void (*ipc_rpc_handler_t)(ipc_server_t *server, cli_id_t id, ipc_header_t *ipc_hdr, 
                                    ipc_url_ref_t *url, ipc_payload_ref_t *payload, void *arg);

/* Server on datagram callback */
typedef void (*ipc_datagram_handler_t)(ipc_server_t *server, cli_id_t id,
                                       ipc_url_ref_t *url, ipc_payload_ref_t *payload, void *arg);

/* Lifecycle */
ipc_server_t *ipc_server_create(const char* server_info);
ipc_server_t *ipc_server_create_with_options(const char *name, const server_options_t *opts);
void ipc_server_destroy(ipc_server_t *server);

/* Start IPC server */
bool ipc_server_start(ipc_server_t *server);

/* Event Loop */
int ipc_server_poll(ipc_server_t *server, int timeout_ms);
void ipc_server_run(ipc_server_t *server);

/* Callback Setup */
void ipc_server_set_connect_handler(ipc_server_t *server, ipc_on_connect_t oncli, void *arg);
void ipc_server_set_datagram_handler(ipc_server_t *server, ipc_datagram_handler_t callback, void *arg);

/* RPC Registeation*/
bool ipc_server_add_method(ipc_server_t *server,
                              const ipc_url_ref_t *url, ipc_rpc_handler_t callback, void *arg);
void ipc_server_remove_method(ipc_server_t *server, const ipc_url_ref_t *url);
bool ipc_server_response(ipc_server_t *server, cli_id_t id,
                           uint8_t status, uint16_t seqno, const ipc_payload_ref_t *payload);

/* Connection Management */
int ipc_server_peer_count(ipc_server_t *server);
bool ipc_server_peer_close(ipc_server_t *server, cli_id_t id);
int ipc_server_peer_list(ipc_server_t *server, cli_id_t ids[], int max_cnt);

/* Get address (must be called after `ipc_server_start`) */
bool ipc_server_address(ipc_server_t *server, struct sockaddr *addr, socklen_t *namelen);
bool ipc_server_peer_address(ipc_server_t *server, cli_id_t id, struct sockaddr *addr, socklen_t *namelen);

/* Publish Management */
bool ipc_server_is_subscribed(ipc_server_t *server, const ipc_url_ref_t *url);
bool ipc_server_publish(ipc_server_t *server, const ipc_url_ref_t *url, const ipc_payload_ref_t *payload);

/* IPC server send datagram */
bool ipc_server_datagram(ipc_server_t *server, cli_id_t id, const ipc_url_ref_t *url, const ipc_payload_ref_t *payload);

#ifdef __cplusplus
}
#endif

#endif /* IPC_SERVER_H */

/*
 * end
 */
