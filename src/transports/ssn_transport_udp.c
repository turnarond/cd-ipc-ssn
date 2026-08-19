/*
 * UDP Transport Adapter Implementation
 */

/* 限制：UDP 为无连接传输，不支持 accept/server 模式握手；仅适用于对等/客户端模式收发 */

#include "ssn_transport.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

#define UDP_MAX_PACKET_SIZE 65507

typedef struct udp_transport_impl {
    int sock_fd;
    struct sockaddr_in addr;
    struct sockaddr_in6 addr6;
    bool is_server;
    bool non_blocking;
    bool ipv6_enabled;
    bool multicast_enabled;
    char multicast_group[INET_ADDRSTRLEN];
    time_t last_activity;
} udp_transport_impl_t;

static bool udp_transport_bind(ssn_transport_t* transport,
                              const ssn_address_t* addr)
{
    udp_transport_impl_t* impl = (udp_transport_impl_t*)transport->impl_data;

    if (addr->type == SSN_TRANSPORT_UDP6) {
        impl->ipv6_enabled = true;
        memcpy(&impl->addr6, &addr->addr.inet6_addr,
               sizeof(struct sockaddr_in6));
    } else {
        impl->ipv6_enabled = false;
        memcpy(&impl->addr, &addr->addr.inet_addr,
               sizeof(struct sockaddr_in));
    }

    return true;
}

static bool udp_transport_connect(ssn_transport_t* transport,
                                 const ssn_address_t* addr,
                                 int timeout_ms)
{
    udp_transport_impl_t* impl = (udp_transport_impl_t*)transport->impl_data;

    (void)timeout_ms;

    int family;
    if (addr->type == SSN_TRANSPORT_UDP6) {
        family = AF_INET6;
        impl->ipv6_enabled = true;
    } else {
        family = AF_INET;
        impl->ipv6_enabled = false;
    }

    /* udp_transport_create 构造时已创建 socket（impl->sock_fd）；connect 重新
     * 创建前必须先关闭旧的，否则构造 fd 永久泄漏（Issue #10，与 tcp/unix 同源）。 */
    if (impl->sock_fd >= 0) {
        close(impl->sock_fd);
        impl->sock_fd = -1;
    }

    impl->sock_fd = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (impl->sock_fd < 0) {
        LOG_ERROR("Failed to create UDP socket: %s", strerror(errno));
        return false;
    }

    int optval = 1;
    setsockopt(impl->sock_fd, SOL_SOCKET, SO_REUSEADDR,
               &optval, sizeof(optval));

    if (transport->config.send_buffer_size > 0) {
        setsockopt(impl->sock_fd, SOL_SOCKET, SO_SNDBUF,
                   &transport->config.send_buffer_size,
                   sizeof(transport->config.send_buffer_size));
    }

    if (transport->config.recv_buffer_size > 0) {
        setsockopt(impl->sock_fd, SOL_SOCKET, SO_RCVBUF,
                   &transport->config.recv_buffer_size,
                   sizeof(transport->config.recv_buffer_size));
    }

    if (impl->non_blocking) {
        int flags = fcntl(impl->sock_fd, F_GETFL, 0);
        fcntl(impl->sock_fd, F_SETFL, flags | O_NONBLOCK);
    }

    if (impl->ipv6_enabled) {
        memcpy(&impl->addr6, &addr->addr.inet6_addr,
               sizeof(struct sockaddr_in6));
    } else {
        memcpy(&impl->addr, &addr->addr.inet_addr,
               sizeof(struct sockaddr_in));
    }

    impl->is_server = false;
    impl->last_activity = time(NULL);
    transport->stats.connection_count++;

    char addr_str[INET6_ADDRSTRLEN];
    if (impl->ipv6_enabled) {
        inet_ntop(AF_INET6, &impl->addr6.sin6_addr,
                  addr_str, sizeof(addr_str));
        LOG_DEBUG("UDP socket configured for [%s]:%d",
                  addr_str, ntohs(impl->addr6.sin6_port));
    } else {
        inet_ntop(AF_INET, &impl->addr.sin_addr,
                  addr_str, sizeof(addr_str));
        LOG_DEBUG("UDP socket configured for %s:%d",
                  addr_str, ntohs(impl->addr.sin_port));
    }

    return true;
}

static bool udp_transport_disconnect(ssn_transport_t* transport)
{
    udp_transport_impl_t* impl = (udp_transport_impl_t*)transport->impl_data;

    if (impl->sock_fd >= 0) {
        close(impl->sock_fd);
        impl->sock_fd = -1;
    }

    return true;
}

static bool udp_transport_is_connected(const ssn_transport_t* transport)
{
    const udp_transport_impl_t* impl =
        (const udp_transport_impl_t*)transport->impl_data;

    return (impl->sock_fd >= 0);
}

static int udp_transport_send(ssn_transport_t* transport,
                             const void* data,
                             size_t len)
{
    udp_transport_impl_t* impl = (udp_transport_impl_t*)transport->impl_data;

    if (!transport->valid || impl->sock_fd < 0) {
        LOG_ERROR("UDP socket not valid for sending");
        return -1;
    }

    if (len > UDP_MAX_PACKET_SIZE) {
        LOG_ERROR("UDP packet too large: %zu bytes (max %d)",
                  len, UDP_MAX_PACKET_SIZE);
        return -1;
    }

    struct sockaddr* sockaddr_ptr;
    socklen_t sockaddr_len;

    if (impl->ipv6_enabled) {
        sockaddr_ptr = (struct sockaddr*)&impl->addr6;
        sockaddr_len = sizeof(impl->addr6);
    } else {
        sockaddr_ptr = (struct sockaddr*)&impl->addr;
        sockaddr_len = sizeof(impl->addr);
    }

    ssize_t sent = sendto(impl->sock_fd, data, len, 0,
                         sockaddr_ptr, sockaddr_len);

    if (sent < 0) {
        /* EAGAIN/EWOULDBLOCK 是非阻塞发送的正常「缓冲区满」信号，不是错误 */
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -1;
        }
        LOG_ERROR("Failed to send UDP packet: %s", strerror(errno));
        transport->stats.send_errors++;
        return -1;
    }

    transport->stats.bytes_sent += sent;
    transport->stats.packets_sent++;
    impl->last_activity = time(NULL);

    return (int)sent;
}

static int udp_transport_recv(ssn_transport_t* transport,
                             void* buffer,
                             size_t size,
                             int timeout_ms)
{
    udp_transport_impl_t* impl = (udp_transport_impl_t*)transport->impl_data;

    if (!transport->valid || impl->sock_fd < 0) {
        LOG_ERROR("UDP socket not valid for receiving");
        return -1;
    }

    if (timeout_ms > 0) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(impl->sock_fd, SOL_SOCKET, SO_RCVTIMEO,
                   &tv, sizeof(tv));
    }

    struct sockaddr_storage from_addr;
    socklen_t from_len = sizeof(from_addr);

    ssize_t received = recvfrom(impl->sock_fd, buffer, size, 0,
                              (struct sockaddr*)&from_addr, &from_len);

    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("Failed to receive UDP packet: %s", strerror(errno));
            transport->stats.recv_errors++;
        }
        return -1;
    }

    if (received > 0) {
        transport->stats.bytes_received += received;
        transport->stats.packets_received++;
        impl->last_activity = time(NULL);

        char from_str[INET6_ADDRSTRLEN];
        if (from_addr.ss_family == AF_INET6) {
            struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&from_addr;
            inet_ntop(AF_INET6, &addr6->sin6_addr,
                      from_str, sizeof(from_str));
            LOG_DEBUG("Received %zd bytes from [%s]:%d",
                      received, from_str, ntohs(addr6->sin6_port));
        } else {
            struct sockaddr_in* addr = (struct sockaddr_in*)&from_addr;
            inet_ntop(AF_INET, &addr->sin_addr,
                      from_str, sizeof(from_str));
            LOG_DEBUG("Received %zd bytes from %s:%d",
                      received, from_str, ntohs(addr->sin_port));
        }
    }

    return (int)received;
}

static bool udp_transport_listen(ssn_transport_t* transport, int backlog)
{
    udp_transport_impl_t* impl = (udp_transport_impl_t*)transport->impl_data;

    (void)backlog;

    int family = impl->ipv6_enabled ? AF_INET6 : AF_INET;

    struct sockaddr* sockaddr_ptr;
    socklen_t sockaddr_len;

    if (impl->ipv6_enabled) {
        sockaddr_ptr = (struct sockaddr*)&impl->addr6;
        sockaddr_len = sizeof(impl->addr6);
    } else {
        sockaddr_ptr = (struct sockaddr*)&impl->addr;
        sockaddr_len = sizeof(impl->addr);
    }

    if (bind(impl->sock_fd, sockaddr_ptr, sockaddr_len) < 0) {
        LOG_ERROR("Failed to bind UDP socket: %s", strerror(errno));
        return false;
    }

    impl->is_server = true;
    LOG_DEBUG("UDP server listening");

    return true;
}

static ssn_transport_t* udp_transport_accept(ssn_transport_t* transport,
                                            ssn_address_t* client_addr,
                                            int timeout_ms)
{
    udp_transport_impl_t* impl = (udp_transport_impl_t*)transport->impl_data;

    (void)impl;
    (void)timeout_ms;

    if (!transport->valid || impl->sock_fd < 0) {
        LOG_ERROR("UDP socket not valid");
        return NULL;
    }

    if (client_addr) {
        LOG_ERROR("UDP does not support accept - connectionless protocol");
        return NULL;
    }

    return transport;
}

static bool udp_transport_set_option(ssn_transport_t* transport,
                                     int option,
                                     const void* value)
{
    udp_transport_impl_t* impl = (udp_transport_impl_t*)transport->impl_data;

    switch (option) {
        default:
            return false;
    }

    return true;
}

static bool udp_transport_get_option(const ssn_transport_t* transport,
                                    int option,
                                    void* value)
{
    const udp_transport_impl_t* impl =
        (const udp_transport_impl_t*)transport->impl_data;

    switch (option) {
        case 0:
            if (value) {
                *(int*)value = impl->sock_fd;
                return true;
            }
            return false;
        default:
            return false;
    }

    return true;
}

static bool udp_transport_get_stats(const ssn_transport_t* transport,
                                   ssn_transport_stats_t* stats)
{
    if (!transport || !stats) {
        return false;
    }

    memcpy(stats, &transport->stats, sizeof(ssn_transport_stats_t));
    return true;
}

static bool udp_transport_get_address(const ssn_transport_t* transport,
                                     ssn_address_t* addr)
{
    const udp_transport_impl_t* impl =
        (const udp_transport_impl_t*)transport->impl_data;

    if (!transport || !addr) {
        return false;
    }

    if (impl->ipv6_enabled) {
        addr->type = SSN_TRANSPORT_UDP6;
        memcpy(&addr->addr.inet6_addr, &impl->addr6,
               sizeof(struct sockaddr_in6));
        char addr_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &impl->addr6.sin6_addr,
                  addr_str, sizeof(addr_str));
        snprintf(addr->address_str, SSN_TRANSPORT_MAX_ADDRESS_LEN,
                 "udp6://[%s]:%d", addr_str, ntohs(impl->addr6.sin6_port));
    } else {
        addr->type = SSN_TRANSPORT_UDP;
        memcpy(&addr->addr.inet_addr, &impl->addr,
               sizeof(struct sockaddr_in));
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &impl->addr.sin_addr,
                  addr_str, sizeof(addr_str));
        snprintf(addr->address_str, SSN_TRANSPORT_MAX_ADDRESS_LEN,
                 "udp://%s:%d", addr_str, ntohs(impl->addr.sin_port));
    }

    return true;
}

static void udp_transport_destroy(ssn_transport_t* transport)
{
    if (!transport) {
        return;
    }

    udp_transport_impl_t* impl = (udp_transport_impl_t*)transport->impl_data;

    if (impl->sock_fd >= 0) {
        close(impl->sock_fd);
        impl->sock_fd = -1;
    }

    free(impl);
    transport->impl_data = NULL;
}

static ssn_transport_ops_t udp_transport_ops = {
    .bind = udp_transport_bind,
    .connect = udp_transport_connect,
    .disconnect = udp_transport_disconnect,
    .is_connected = udp_transport_is_connected,
    .send = udp_transport_send,
    .recv = udp_transport_recv,
    .listen = udp_transport_listen,
    .accept = udp_transport_accept,
    .set_option = udp_transport_set_option,
    .get_option = udp_transport_get_option,
    .get_stats = udp_transport_get_stats,
    .get_address = udp_transport_get_address,
    .destroy = udp_transport_destroy
};

ssn_transport_t* udp_transport_create(const ssn_transport_config_t* config,
                                      bool ipv6_enabled)
{
    ssn_transport_t* transport = (ssn_transport_t*)calloc(1,
                                                          sizeof(ssn_transport_t));
    if (!transport) {
        LOG_ERROR("Failed to allocate memory for UDP transport");
        return NULL;
    }

    udp_transport_impl_t* impl = (udp_transport_impl_t*)calloc(
        1, sizeof(udp_transport_impl_t));
    if (!impl) {
        LOG_ERROR("Failed to allocate memory for UDP transport impl");
        free(transport);
        return NULL;
    }

    int family = ipv6_enabled ? AF_INET6 : AF_INET;
    impl->sock_fd = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (impl->sock_fd < 0) {
        LOG_ERROR("Failed to create UDP socket: %s", strerror(errno));
        free(impl);
        free(transport);
        return NULL;
    }

    int optval = 1;
    setsockopt(impl->sock_fd, SOL_SOCKET, SO_REUSEADDR,
               &optval, sizeof(optval));

    impl->is_server = false;
    impl->non_blocking = config ? config->non_blocking : false;
    impl->ipv6_enabled = ipv6_enabled;
    impl->multicast_enabled = false;
    impl->last_activity = time(NULL);

    transport->type = ipv6_enabled ? SSN_TRANSPORT_UDP6 : SSN_TRANSPORT_UDP;
    transport->ops = udp_transport_ops;
    transport->impl_data = impl;
    transport->valid = true;

    if (config) {
        transport->config = *config;
    }

    memset(&transport->stats, 0, sizeof(ssn_transport_stats_t));

    return transport;
}

