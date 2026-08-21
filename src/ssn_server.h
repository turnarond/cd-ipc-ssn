/*
 * IPC server
 */

#ifndef SSN_SERVER_H
#define SSN_SERVER_H

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

/* Remote client ID */
typedef uint32_t  ssn_peer_id_t;

typedef struct {
    uint64_t send_timeout_ms; 
    uint64_t conn_timeout_ms; 
    uint64_t idle_timeout_sec;
    char ifname[IF_NAMESIZE];     /* Not used for AF_UNIX. */
} server_options_t;

/* 命名规范别名（缺陷背景：公开类型无 ssn_ 前缀违反「类型 ssn_<module>_t」规范；
 * 直接重命名 server_options_t 属破坏性变更，故新增别名——既有代码不受影响，
 * 新代码应使用 ssn_server_options_t） */
typedef server_options_t ssn_server_options_t;

/* Server on client connect or lost callback */
typedef void (*ssn_on_connect_t)(ssn_server_t *server, ssn_peer_id_t id, bool connect, void *arg);

/* Server command callback
 * NOTICE: Can not remove listener in callback
 *         `ipc_hdr`, `url` and `data` are invalid when this function returns */
typedef void (*ssn_server_rpc_handler_t)(ssn_server_t *server, ssn_peer_id_t id, ssn_header_t *ipc_hdr, 
                                    ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg);

/* Server on message callback */
typedef void (*ssn_server_msg_handler_t)(ssn_server_t *server, ssn_peer_id_t id,
                                       ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg);

/* Lifecycle */
SSN_API ssn_server_t *ssn_server_create(const char* server_info);
SSN_API ssn_server_t *ssn_server_create_with_options(const char *name, const server_options_t *opts);
SSN_API void ssn_server_destroy(ssn_server_t *server);

/* Start IPC server */
SSN_API bool ssn_server_start(ssn_server_t *server);

/* Event Loop */
SSN_API int ssn_server_poll(ssn_server_t *server, int timeout_ms);
SSN_API void ssn_server_run(ssn_server_t *server);

/* Callback Setup */
SSN_API void ssn_server_set_connect_handler(ssn_server_t *server, ssn_on_connect_t oncli, void *arg);
SSN_API void ssn_server_set_message_handler(ssn_server_t *server, ssn_server_msg_handler_t callback, void *arg);

/* RPC Registration */
SSN_API bool ssn_server_add_method(ssn_server_t *server,
                              const ssn_url_ref_t *url, ssn_server_rpc_handler_t callback, void *arg);
SSN_API void ssn_server_remove_method(ssn_server_t *server, const ssn_url_ref_t *url);
SSN_API int ssn_server_response(ssn_server_t *server, ssn_peer_id_t id,
                           uint32_t status, uint16_t seqno, const ssn_data_ref_t *data);

/* Connection Management */
SSN_API int ssn_server_peer_count(ssn_server_t *server);
SSN_API bool ssn_server_peer_close(ssn_server_t *server, ssn_peer_id_t id);
SSN_API int ssn_server_peer_list(ssn_server_t *server, ssn_peer_id_t ids[], int max_cnt);

/* Get address (must be called after `ssn_server_start`) */
SSN_API int ssn_server_address(ssn_server_t *server, struct sockaddr *addr, socklen_t *namelen);
SSN_API int ssn_server_peer_address(ssn_server_t *server, ssn_peer_id_t id, struct sockaddr *addr, socklen_t *namelen);

/* Publish Management */
SSN_API bool ssn_server_is_subscribed(ssn_server_t *server, const ssn_url_ref_t *url);
SSN_API int ssn_server_publish(ssn_server_t *server, const ssn_url_ref_t *url, const ssn_data_ref_t *data);

/* IPC server send message */
SSN_API int ssn_server_message(ssn_server_t *server, ssn_peer_id_t id, const ssn_url_ref_t *url, const ssn_data_ref_t *data);

#ifdef __cplusplus
}
#endif

#endif /* SSN_SERVER_H */

/*
 * end
 */
