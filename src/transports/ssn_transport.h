/*
 * SSN Transport Layer - Unified Transport Interface
 */

#ifndef SSN_TRANSPORT_H
#define SSN_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "ssn_export.h"
#include "util/ssn_log.h"
#include "util/ssn_mutex.h"
#include "util/ssn_hash_table.h"

#define SSN_TRANSPORT_MAX_ADDRESS_LEN 256

typedef enum {
    SSN_TRANSPORT_UNIX,
    SSN_TRANSPORT_TCP,
    SSN_TRANSPORT_TCP6,
    SSN_TRANSPORT_UDP,
    SSN_TRANSPORT_UDP6,
    SSN_TRANSPORT_TLS,
    SSN_TRANSPORT_DTLS
} ssn_transport_type_t;

typedef struct {
    ssn_transport_type_t type;
    union {
        struct sockaddr_un unix_addr;
        struct sockaddr_in inet_addr;
        struct sockaddr_in6 inet6_addr;
    } addr;
    char address_str[SSN_TRANSPORT_MAX_ADDRESS_LEN];
} ssn_address_t;

typedef struct {
    ssn_transport_type_t type;
    bool non_blocking;
    int send_timeout_ms;
    int recv_timeout_ms;
    int connect_timeout_ms;
    bool enable_keepalive;
    int keepalive_idle_sec;
    int keepalive_interval_sec;
    int keepalive_count;
    bool enable_nagle;
    int send_buffer_size;
    int recv_buffer_size;
    bool reuse_address;
} ssn_transport_config_t;

typedef struct {
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t send_errors;
    uint32_t recv_errors;
    uint32_t connection_count;
    uint32_t failed_connections;
    uint32_t avg_latency_ms;
    uint32_t max_latency_ms;
    float loss_rate;
} ssn_transport_stats_t;

typedef struct ssn_transport ssn_transport_t;

typedef struct ssn_transport_ops {
    bool (*bind)(ssn_transport_t* transport,
                 const ssn_address_t* addr);
    bool (*connect)(ssn_transport_t* transport,
                    const ssn_address_t* addr,
                    int timeout_ms);
    bool (*disconnect)(ssn_transport_t* transport);
    bool (*is_connected)(const ssn_transport_t* transport);
    int (*send)(ssn_transport_t* transport,
                const void* data,
                size_t len);
    int (*recv)(ssn_transport_t* transport,
                void* buffer,
                size_t size,
                int timeout_ms);
    bool (*listen)(ssn_transport_t* transport, int backlog);
    ssn_transport_t* (*accept)(ssn_transport_t* transport,
                               ssn_address_t* client_addr,
                               int timeout_ms);
    bool (*set_option)(ssn_transport_t* transport,
                       int option,
                       const void* value);
    bool (*get_option)(const ssn_transport_t* transport,
                        int option,
                        void* value);
    bool (*get_stats)(const ssn_transport_t* transport,
                       ssn_transport_stats_t* stats);
    bool (*get_address)(const ssn_transport_t* transport,
                        ssn_address_t* addr);
    void (*destroy)(ssn_transport_t* transport);
} ssn_transport_ops_t;

struct ssn_transport {
    ssn_transport_type_t type;
    ssn_transport_ops_t ops;
    void* impl_data;
    ssn_transport_config_t config;
    ssn_transport_stats_t stats;
    ssn_mutex_t* lock;
    bool valid;
};

SSN_API bool ssn_address_parse(const char* address_str, ssn_address_t* addr);
SSN_API bool ssn_address_to_string(const ssn_address_t* addr,
                            char* buffer,
                            size_t size);

SSN_API ssn_transport_t* ssn_transport_create(ssn_transport_type_t type,
                                       const ssn_transport_config_t* config);
SSN_API void ssn_transport_destroy(ssn_transport_t* transport);

SSN_API bool ssn_transport_bind(ssn_transport_t* transport,
                        const ssn_address_t* addr);
SSN_API bool ssn_transport_connect(ssn_transport_t* transport,
                           const ssn_address_t* addr,
                           int timeout_ms);
SSN_API bool ssn_transport_disconnect(ssn_transport_t* transport);
SSN_API bool ssn_transport_is_connected(const ssn_transport_t* transport);
SSN_API int ssn_transport_send(ssn_transport_t* transport,
                       const void* data,
                       size_t len);
SSN_API int ssn_transport_recv(ssn_transport_t* transport,
                       void* buffer,
                       size_t size,
                       int timeout_ms);
SSN_API bool ssn_transport_listen(ssn_transport_t* transport, int backlog);
SSN_API ssn_transport_t* ssn_transport_accept(ssn_transport_t* transport,
                                       ssn_address_t* client_addr,
                                       int timeout_ms);
SSN_API bool ssn_transport_set_option(ssn_transport_t* transport,
                               int option,
                               const void* value);
SSN_API bool ssn_transport_get_option(const ssn_transport_t* transport,
                              int option,
                              void* value);
SSN_API bool ssn_transport_get_stats(const ssn_transport_t* transport,
                             ssn_transport_stats_t* stats);
SSN_API bool ssn_transport_get_address(const ssn_transport_t* transport,
                               ssn_address_t* addr);
SSN_API int ssn_transport_get_fd(const ssn_transport_t* transport);

SSN_API const char* ssn_transport_type_to_string(ssn_transport_type_t type);
SSN_API ssn_transport_type_t ssn_transport_type_from_string(const char* type_str);

#endif

