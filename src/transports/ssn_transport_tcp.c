/*
 * TCP Transport Adapter Implementation
 */

#include "ssn_transport.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

typedef struct tcp_transport_impl {
    int sock_fd;
    struct sockaddr_in addr;
    struct sockaddr_in6 addr6;
    bool is_server;
    bool non_blocking;
    bool ipv6_enabled;
    time_t last_activity;
} tcp_transport_impl_t;

static bool tcp_transport_bind(ssn_transport_t* transport,
                              const ssn_address_t* addr)
{
    tcp_transport_impl_t* impl = (tcp_transport_impl_t*)transport->impl_data;

    if (addr->type == SSN_TRANSPORT_TCP6) {
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

static bool tcp_transport_connect(ssn_transport_t* transport,
                                 const ssn_address_t* addr,
                                 int timeout_ms)
{
    tcp_transport_impl_t* impl = (tcp_transport_impl_t*)transport->impl_data;

    int family;
    if (addr->type == SSN_TRANSPORT_TCP6) {
        family = AF_INET6;
        impl->ipv6_enabled = true;
    } else {
        family = AF_INET;
        impl->ipv6_enabled = false;
    }

    impl->sock_fd = socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (impl->sock_fd < 0) {
        LOG_ERROR("Failed to create TCP socket: %s", strerror(errno));
        return false;
    }

    int optval = 1;
    setsockopt(impl->sock_fd, SOL_SOCKET, SO_REUSEADDR,
               &optval, sizeof(optval));

    if (!transport->config.enable_nagle) {
        optval = 1;
        setsockopt(impl->sock_fd, IPPROTO_TCP, TCP_NODELAY,
                   &optval, sizeof(optval));
    }

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

    struct sockaddr* sockaddr_ptr;
    socklen_t sockaddr_len;

    if (impl->ipv6_enabled) {
        memcpy(&impl->addr6, &addr->addr.inet6_addr,
               sizeof(struct sockaddr_in6));
        sockaddr_ptr = (struct sockaddr*)&impl->addr6;
        sockaddr_len = sizeof(impl->addr6);
    } else {
        memcpy(&impl->addr, &addr->addr.inet_addr,
               sizeof(struct sockaddr_in));
        sockaddr_ptr = (struct sockaddr*)&impl->addr;
        sockaddr_len = sizeof(impl->addr);
    }

    if (connect(impl->sock_fd, sockaddr_ptr, sockaddr_len) < 0) {
        if (errno != EINPROGRESS) {
            LOG_ERROR("Failed to connect TCP socket: %s", strerror(errno));
            close(impl->sock_fd);
            impl->sock_fd = -1;
            return false;
        }

        if (timeout_ms > 0) {
            fd_set write_fds;
            struct timeval tv;

            FD_ZERO(&write_fds);
            FD_SET(impl->sock_fd, &write_fds);

            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;

            int ret = select(impl->sock_fd + 1, NULL, &write_fds,
                             NULL, &tv);
            if (ret <= 0) {
                LOG_ERROR("TCP socket connect timeout");
                close(impl->sock_fd);
                impl->sock_fd = -1;
                return false;
            }
        }
    }

    impl->is_server = false;
    impl->last_activity = time(NULL);
    transport->stats.connection_count++;

    char addr_str[INET6_ADDRSTRLEN];
    if (impl->ipv6_enabled) {
        inet_ntop(AF_INET6, &impl->addr6.sin6_addr,
                  addr_str, sizeof(addr_str));
        LOG_DEBUG("Connected to TCP server [%s]:%d",
                  addr_str, ntohs(impl->addr6.sin6_port));
    } else {
        inet_ntop(AF_INET, &impl->addr.sin_addr,
                  addr_str, sizeof(addr_str));
        LOG_DEBUG("Connected to TCP server %s:%d",
                  addr_str, ntohs(impl->addr.sin_port));
    }

    return true;
}

static bool tcp_transport_disconnect(ssn_transport_t* transport)
{
    tcp_transport_impl_t* impl = (tcp_transport_impl_t*)transport->impl_data;

    if (impl->sock_fd >= 0) {
        shutdown(impl->sock_fd, SHUT_RDWR);
        close(impl->sock_fd);
        impl->sock_fd = -1;
    }

    return true;
}

static bool tcp_transport_is_connected(const ssn_transport_t* transport)
{
    const tcp_transport_impl_t* impl =
        (const tcp_transport_impl_t*)transport->impl_data;

    return (impl->sock_fd >= 0);
}

static int tcp_transport_send(ssn_transport_t* transport,
                             const void* data,
                             size_t len)
{
    tcp_transport_impl_t* impl = (tcp_transport_impl_t*)transport->impl_data;

    if (!transport->valid || impl->sock_fd < 0) {
        LOG_ERROR("TCP socket not valid for sending");
        return -1;
    }

    ssize_t sent = send(impl->sock_fd, data, len, MSG_NOSIGNAL);
    if (sent < 0) {
        LOG_ERROR("Failed to send data via TCP socket: %s", strerror(errno));
        transport->stats.send_errors++;
        return -1;
    }

    transport->stats.bytes_sent += sent;
    transport->stats.packets_sent++;
    impl->last_activity = time(NULL);

    return (int)sent;
}

static int tcp_transport_recv(ssn_transport_t* transport,
                             void* buffer,
                             size_t size,
                             int timeout_ms)
{
    tcp_transport_impl_t* impl = (tcp_transport_impl_t*)transport->impl_data;

    if (!transport->valid || impl->sock_fd < 0) {
        LOG_ERROR("TCP socket not valid for receiving");
        return -1;
    }

    if (timeout_ms > 0) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(impl->sock_fd, SOL_SOCKET, SO_RCVTIMEO,
                   &tv, sizeof(tv));
    }

    ssize_t received = recv(impl->sock_fd, buffer, size, 0);
    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("Failed to receive data via TCP socket: %s",
                      strerror(errno));
            transport->stats.recv_errors++;
        }
        return -1;
    }

    if (received > 0) {
        transport->stats.bytes_received += received;
        transport->stats.packets_received++;
        impl->last_activity = time(NULL);
    }

    return (int)received;
}

static bool tcp_transport_listen(ssn_transport_t* transport, int backlog)
{
    tcp_transport_impl_t* impl = (tcp_transport_impl_t*)transport->impl_data;

    int family = impl->ipv6_enabled ? AF_INET6 : AF_INET;

    if (bind(impl->sock_fd, (struct sockaddr*)&impl->addr,
             family == AF_INET6 ?
             sizeof(struct sockaddr_in6) :
             sizeof(struct sockaddr_in)) < 0) {
        LOG_ERROR("Failed to bind TCP socket: %s", strerror(errno));
        return false;
    }

    if (listen(impl->sock_fd, backlog) < 0) {
        LOG_ERROR("Failed to listen on TCP socket: %s", strerror(errno));
        return false;
    }

    impl->is_server = true;
    LOG_DEBUG("TCP server listening");

    return true;
}

static ssn_transport_t* tcp_transport_accept(ssn_transport_t* transport,
                                             ssn_address_t* client_addr,
                                             int timeout_ms)
{
    tcp_transport_impl_t* impl = (tcp_transport_impl_t*)transport->impl_data;

    if (!transport->valid || impl->sock_fd < 0) {
        LOG_ERROR("TCP socket not valid for accepting");
        return NULL;
    }

    if (timeout_ms > 0) {
        fd_set read_fds;
        struct timeval tv;

        FD_ZERO(&read_fds);
        FD_SET(impl->sock_fd, &read_fds);

        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int ret = select(impl->sock_fd + 1, &read_fds, NULL, NULL, &tv);
        if (ret <= 0) {
            return NULL;
        }
    }

    struct sockaddr_storage client_addr_storage;
    socklen_t addr_len = sizeof(client_addr_storage);

    int client_fd = accept(impl->sock_fd,
                           (struct sockaddr*)&client_addr_storage,
                           &addr_len);
    if (client_fd < 0) {
        LOG_ERROR("Failed to accept TCP connection: %s", strerror(errno));
        return NULL;
    }

    ssn_transport_config_t config = transport->config;
    config.non_blocking = impl->non_blocking;

    ssn_transport_t* client_transport =
        ssn_transport_create(impl->ipv6_enabled ?
                             SSN_TRANSPORT_TCP6 : SSN_TRANSPORT_TCP,
                             &config);
    if (!client_transport) {
        close(client_fd);
        return NULL;
    }

    tcp_transport_impl_t* client_impl =
        (tcp_transport_impl_t*)client_transport->impl_data;
    client_impl->sock_fd = client_fd;
    client_impl->is_server = false;
    client_impl->ipv6_enabled = impl->ipv6_enabled;
    client_impl->last_activity = time(NULL);

    if (client_addr) {
        if (client_addr_storage.ss_family == AF_INET6) {
            client_addr->type = SSN_TRANSPORT_TCP6;
            memcpy(&client_addr->addr.inet6_addr,
                   &client_addr_storage,
                   sizeof(struct sockaddr_in6));
            char addr_str[INET6_ADDRSTRLEN];
            struct sockaddr_in6* addr6 =
                (struct sockaddr_in6*)&client_addr_storage;
            inet_ntop(AF_INET6, &addr6->sin6_addr,
                      addr_str, sizeof(addr_str));
            snprintf(client_addr->address_str,
                     SSN_TRANSPORT_MAX_ADDRESS_LEN,
                     "tcp6://[%s]:%d", addr_str, ntohs(addr6->sin6_port));
        } else {
            client_addr->type = SSN_TRANSPORT_TCP;
            memcpy(&client_addr->addr.inet_addr,
                   &client_addr_storage,
                   sizeof(struct sockaddr_in));
            char addr_str[INET_ADDRSTRLEN];
            struct sockaddr_in* addr4 =
                (struct sockaddr_in*)&client_addr_storage;
            inet_ntop(AF_INET, &addr4->sin_addr,
                      addr_str, sizeof(addr_str));
            snprintf(client_addr->address_str,
                     SSN_TRANSPORT_MAX_ADDRESS_LEN,
                     "tcp://%s:%d", addr_str, ntohs(addr4->sin_port));
        }
    }

    LOG_DEBUG("Accepted TCP connection");

    return client_transport;
}

static bool tcp_transport_enable_keepalive(ssn_transport_t* transport)
{
    tcp_transport_impl_t* impl = (tcp_transport_impl_t*)transport->impl_data;

    if (impl->sock_fd < 0) {
        return false;
    }

    int optval = 1;
    if (setsockopt(impl->sock_fd, SOL_SOCKET, SO_KEEPALIVE,
                   &optval, sizeof(optval)) < 0) {
        LOG_WARN("Failed to enable TCP keepalive: %s", strerror(errno));
        return false;
    }

    optval = transport->config.keepalive_idle_sec;
    setsockopt(impl->sock_fd, IPPROTO_TCP, TCP_KEEPIDLE,
               &optval, sizeof(optval));

    optval = transport->config.keepalive_interval_sec;
    setsockopt(impl->sock_fd, IPPROTO_TCP, TCP_KEEPINTVL,
               &optval, sizeof(optval));

    optval = transport->config.keepalive_count;
    setsockopt(impl->sock_fd, IPPROTO_TCP, TCP_KEEPCNT,
               &optval, sizeof(optval));

    LOG_DEBUG("TCP keepalive enabled: idle=%ds, interval=%ds, count=%d",
              transport->config.keepalive_idle_sec,
              transport->config.keepalive_interval_sec,
              transport->config.keepalive_count);

    return true;
}

static bool tcp_transport_set_option(ssn_transport_t* transport,
                                     int option,
                                     const void* value)
{
    tcp_transport_impl_t* impl = (tcp_transport_impl_t*)transport->impl_data;

    switch (option) {
        default:
            return false;
    }

    return true;
}

static bool tcp_transport_get_option(const ssn_transport_t* transport,
                                    int option,
                                    void* value)
{
    const tcp_transport_impl_t* impl =
        (const tcp_transport_impl_t*)transport->impl_data;

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

static bool tcp_transport_get_stats(const ssn_transport_t* transport,
                                   ssn_transport_stats_t* stats)
{
    if (!transport || !stats) {
        return false;
    }

    memcpy(stats, &transport->stats, sizeof(ssn_transport_stats_t));
    return true;
}

static bool tcp_transport_get_address(const ssn_transport_t* transport,
                                     ssn_address_t* addr)
{
    const tcp_transport_impl_t* impl =
        (const tcp_transport_impl_t*)transport->impl_data;

    if (!transport || !addr) {
        return false;
    }

    if (impl->ipv6_enabled) {
        addr->type = SSN_TRANSPORT_TCP6;
        memcpy(&addr->addr.inet6_addr, &impl->addr6,
               sizeof(struct sockaddr_in6));
        char addr_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &impl->addr6.sin6_addr,
                  addr_str, sizeof(addr_str));
        snprintf(addr->address_str, SSN_TRANSPORT_MAX_ADDRESS_LEN,
                 "tcp6://[%s]:%d", addr_str, ntohs(impl->addr6.sin6_port));
    } else {
        addr->type = SSN_TRANSPORT_TCP;
        memcpy(&addr->addr.inet_addr, &impl->addr,
               sizeof(struct sockaddr_in));
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &impl->addr.sin_addr,
                  addr_str, sizeof(addr_str));
        snprintf(addr->address_str, SSN_TRANSPORT_MAX_ADDRESS_LEN,
                 "tcp://%s:%d", addr_str, ntohs(impl->addr.sin_port));
    }

    return true;
}

static void tcp_transport_destroy(ssn_transport_t* transport)
{
    if (!transport) {
        return;
    }

    tcp_transport_impl_t* impl = (tcp_transport_impl_t*)transport->impl_data;

    if (impl->sock_fd >= 0) {
        close(impl->sock_fd);
        impl->sock_fd = -1;
    }

    free(impl);
    transport->impl_data = NULL;
}

static ssn_transport_ops_t tcp_transport_ops = {
    .bind = tcp_transport_bind,
    .connect = tcp_transport_connect,
    .disconnect = tcp_transport_disconnect,
    .is_connected = tcp_transport_is_connected,
    .send = tcp_transport_send,
    .recv = tcp_transport_recv,
    .listen = tcp_transport_listen,
    .accept = tcp_transport_accept,
    .set_option = tcp_transport_set_option,
    .get_option = tcp_transport_get_option,
    .get_stats = tcp_transport_get_stats,
    .get_address = tcp_transport_get_address,
    .destroy = tcp_transport_destroy
};

ssn_transport_t* tcp_transport_create(const ssn_transport_config_t* config,
                                      bool ipv6_enabled)
{
    ssn_transport_t* transport = (ssn_transport_t*)calloc(1,
                                                          sizeof(ssn_transport_t));
    if (!transport) {
        LOG_ERROR("Failed to allocate memory for TCP transport");
        return NULL;
    }

    tcp_transport_impl_t* impl = (tcp_transport_impl_t*)calloc(
        1, sizeof(tcp_transport_impl_t));
    if (!impl) {
        LOG_ERROR("Failed to allocate memory for TCP transport impl");
        free(transport);
        return NULL;
    }

    int family = ipv6_enabled ? AF_INET6 : AF_INET;
    impl->sock_fd = socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (impl->sock_fd < 0) {
        LOG_ERROR("Failed to create TCP socket: %s", strerror(errno));
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
    impl->last_activity = time(NULL);

    transport->type = ipv6_enabled ? SSN_TRANSPORT_TCP6 : SSN_TRANSPORT_TCP;
    transport->ops = tcp_transport_ops;
    transport->impl_data = impl;
    transport->valid = true;

    if (config) {
        transport->config = *config;
    }

    memset(&transport->stats, 0, sizeof(ssn_transport_stats_t));

    if (config && config->enable_keepalive) {
        tcp_transport_enable_keepalive(transport);
    }

    return transport;
}

