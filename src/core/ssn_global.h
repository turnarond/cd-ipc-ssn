/*
 * SSN Global Header
 */

#ifndef SSN_GLOBAL_H
#define SSN_GLOBAL_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define SSN_MAX_PACKET_SIZE 8192
#define SSN_TIMER_PERIOD 100
#define SSN_DEF_SEND_TIMEOUT 5000
#define SSN_SERVER_BACKLOG 128
#define SSN_SERVER_DEF_HANDSHAKE_TIMEOUT 30000
#define SSN_SERVER_KEEPALIVE_TIMEOUT 60000

#define SSN_MAX_CLIENTS 4096
#define SSN_MAX_SERVERS 256

typedef uint32_t cli_id_t;

#define CLI_ID_INVALID ((cli_id_t)-1)

typedef struct ssn_client ssn_client_t;
typedef struct ssn_server ssn_server_t;

typedef void (*ssn_server_msg_handler_t)(ssn_server_t* server,
                                        cli_id_t id,
                                        const char* url,
                                        const void* data,
                                        size_t len,
                                        void* arg);
typedef void (*ssn_server_rpc_handler_t)(ssn_server_t* server,
                                         cli_id_t id,
                                         const void* hdr,
                                         const char* url,
                                         const void* data,
                                         size_t len,
                                         void* arg);
typedef void (*ssn_on_connect_t)(ssn_server_t* server,
                                cli_id_t id,
                                bool connected,
                                void* arg);
typedef void (*ssn_client_msg_handler_t)(ssn_client_t* client,
                                        const char* url,
                                        const void* data,
                                        size_t len,
                                        void* arg);
typedef void (*ssn_client_rpcreply_handler_t)(ssn_client_t* client,
                                             const void* hdr,
                                             const void* data,
                                             size_t len,
                                             void* arg);
typedef void (*ssn_client_result_handler_t)(ssn_client_t* client,
                                           bool success,
                                           void* arg);

typedef struct server_options {
    int send_timeout_ms;
    int conn_timeout_ms;
    int idle_timeout_sec;
    char ifname[64];
} server_options_t;

ssn_client_t* ssn_client_create(void);
void ssn_client_close(ssn_client_t* client);
bool ssn_client_connect(ssn_client_t* client,
                        const char* address,
                        int timeout_ms);
bool ssn_client_disconnect(ssn_client_t* client);
bool ssn_client_is_connect(ssn_client_t* client);
bool ssn_client_call(ssn_client_t* client,
                     const char* url,
                     const void* data,
                     size_t len,
                     ssn_client_rpcreply_handler_t callback,
                     void* arg,
                     int timeout_ms);
bool ssn_client_message(ssn_client_t* client,
                       const char* url,
                       const void* data,
                       size_t len);
bool ssn_client_subscribe(ssn_client_t* client,
                         const char* url,
                         ssn_client_msg_handler_t callback,
                         void* arg,
                         int timeout_ms);
bool ssn_client_unsubscribe(ssn_client_t* client,
                           const char* url,
                           int timeout_ms);
void ssn_client_set_on_message(ssn_client_t* client,
                               ssn_client_msg_handler_t callback,
                               void* arg);
int ssn_client_poll(ssn_client_t* client, int timeout_ms);
void ssn_client_run(ssn_client_t* client);

ssn_server_t* ssn_server_create(const char* name);
ssn_server_t* ssn_server_create_with_options(const char* name,
                                             const server_options_t* opts);
bool ssn_server_start(ssn_server_t* server, const char* address);
void ssn_server_destroy(ssn_server_t* server);
bool ssn_server_publish(ssn_server_t* server,
                        const char* url,
                        const void* data,
                        size_t len);
bool ssn_server_add_method(ssn_server_t* server,
                           const char* url,
                           ssn_server_rpc_handler_t callback,
                           void* arg);
void ssn_server_remove_method(ssn_server_t* server, const char* url);
bool ssn_server_response(ssn_server_t* server,
                         cli_id_t id,
                         int status,
                         uint16_t seqno,
                         const void* data,
                         size_t len);
bool ssn_server_peer_close(ssn_server_t* server, cli_id_t id);
int ssn_server_peer_count(ssn_server_t* server);
int ssn_server_peer_list(ssn_server_t* server, cli_id_t* ids, int max_cnt);
bool ssn_server_is_subscribed(ssn_server_t* server, const char* url);
void ssn_server_set_connect_handler(ssn_server_t* server,
                                   ssn_on_connect_t oncli,
                                   void* arg);
void ssn_server_set_message_handler(ssn_server_t* server,
                                   ssn_server_msg_handler_t callback,
                                   void* arg);
int ssn_server_poll(ssn_server_t* server, int timeout_ms);
void ssn_server_run(ssn_server_t* server);

int ssn_server_address(ssn_server_t* server,
                      char* address,
                      size_t max_len);

#endif

