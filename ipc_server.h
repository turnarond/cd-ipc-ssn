/*
 * IPC server
 */

#ifndef IPC_SERVER_H
#define IPC_SERVER_H

#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include "ipc_parser.h"

/* IPC server backlog */
#define IPC_SERVER_BACKLOG  32

/* IPC server default send timeout (ms) */
#define IPC_SERVER_DEF_SEND_TIMEOUT  100

/* IPC server default handshake timeout (ms) */
#define IPC_SERVER_DEF_HANDSHAKE_TIMEOUT  5000

/* IPC stream keepalive timeout seconds */
#define IPC_SERVER_KEEPALIVE_TIMEOUT  10

#ifdef __cplusplus
extern "C" {
#endif

/* Structure declaration */
struct ipc_server;

/* Server type */
typedef struct ipc_server ipc_server_t;

/* Remote client ID */
typedef uint32_t  ipc_cli_id_t;

/* Server on client connect or lost callback */
typedef void (*ipc_server_cli_func_t)(void *arg, ipc_server_t *server, ipc_cli_id_t id, bool connect);

/* Server command callback
 * NOTICE: Can not remove listener in callback
 *         `ipc_hdr`, `url` and `payload` are invalid when this function returns */
typedef void (*ipc_server_cmd_func_t)(void *arg, ipc_server_t *server, ipc_cli_id_t id,
                                       ipc_header_t *ipc_hdr, ipc_url_t *url, ipc_payload_t *payload);

                                       /* Server on datagram callback */
typedef void (*ipc_server_dat_func_t)(void *arg, ipc_server_t *server, ipc_cli_id_t id,
                                       ipc_url_t *url, ipc_payload_t *payload);

/* Create IPC server 
 * Warning: This function must be mutually exclusive with the ipc_server_close() call */
ipc_server_t *ipc_server_create(const char* server_info);

/* Close IPC server
 * Warning: This function must be mutually exclusive with the ipc_server_create() call */
void ipc_server_close(ipc_server_t *server);

/* Start IPC server */
bool ipc_server_start(ipc_server_t *server, const char* ipc_path);

/* Get IPC server address (must be called after `ipc_server_start`) */
bool ipc_server_address(ipc_server_t *server, struct sockaddr *addr, socklen_t *namelen);

/* Bind IPC server to specified network interface */
bool ipc_server_bind_if(ipc_server_t *server, const char *ifname);

/* IPC server set on client callback */
void ipc_server_on_cli(ipc_server_t *server, ipc_server_cli_func_t oncli, void *arg);

/* IPC server remote clients count */
int ipc_server_count(ipc_server_t *server);

/* IPC server set this server send packet to client timeout.
 * `timeout` NULL means use default: IPC_SERVER_DEF_SEND_TIMEOUT */
bool ipc_server_send_timeout(ipc_server_t *server, bool cur_clis, const struct timespec *timeout);

/* IPC server is subscribed */
bool ipc_server_is_subscribed(ipc_server_t *server, const ipc_url_t *url);

/* IPC server publish */
bool ipc_server_publish(ipc_server_t *server, const ipc_url_t *url, const ipc_payload_t *payload);

/* IPC server add RPC listener */
bool ipc_server_add_listener(ipc_server_t *server,
                              const ipc_url_t *url, ipc_server_cmd_func_t callback, void *arg);

/* IPC server remove RPC listener */
void ipc_server_remove_listener(ipc_server_t *server, const ipc_url_t *url);

/* IPC server close a client */
bool ipc_server_cli_close(ipc_server_t *server, ipc_cli_id_t id);

/* IPC remote client is subscribed */
bool ipc_server_cli_is_subscribed(ipc_server_t *server, ipc_cli_id_t id, const ipc_url_t *url);

/* IPC remote client address */
bool ipc_server_cli_address(ipc_server_t *server, ipc_cli_id_t id, struct sockaddr *addr, socklen_t *namelen);

/* IPC server RPC reply */
bool ipc_server_cli_reply(ipc_server_t *server, ipc_cli_id_t id,
                           uint8_t status, uint16_t seqno, const ipc_payload_t *payload);

/* IPC remote client keepalive */
bool ipc_server_cli_keepalive(ipc_server_t *server, ipc_cli_id_t id, int keepalive);

/* IPC server get remote client id array */
int ipc_server_cli_array(ipc_server_t *server, ipc_cli_id_t ids[], int max_cnt);

/* IPC server set send packet to specified client timeout.
 * `timeout` NULL means use server global send timeout setting */
bool ipc_server_cli_send_timeout(ipc_server_t *server, ipc_cli_id_t id, const struct timespec *timeout);

/* IPC server send datagram */
bool ipc_server_cli_datagram(ipc_server_t *server, ipc_cli_id_t id, const ipc_url_t *url, const ipc_payload_t *payload);

/* IPC server set on datagram callback */
void ipc_server_on_datagram(ipc_server_t *server, ipc_server_dat_func_t callback, void *arg);

/* IPC server checking event */
int ipc_server_fds(ipc_server_t *server, fd_set *rfds);

/* IPC server input event */
void ipc_server_input_fds(ipc_server_t *server, const fd_set *rfds);

#ifdef __cplusplus
}
#endif

#endif /* IPC_SERVER_H */
/*
 * end
 */
