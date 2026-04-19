/*
 * Unix Socket Transport Adapter Implementation
 */

#include "ssn_transport.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>

typedef struct unix_transport_impl {
    int sock_fd;
    struct sockaddr_un addr;
    bool is_server;
    bool non_blocking;
    char socket_path[108];
    time_t last_activity;
} unix_transport_impl_t;

static bool unix_transport_bind(ssn_transport_t* transport,
                               const ssn_address_t* addr)
{
    unix_transport_impl_t* impl = (unix_transport_impl_t*)transport->impl_data;

    memcpy(&impl->addr, &addr->addr.unix_addr, sizeof(struct sockaddr_un));
    strncpy(impl->socket_path, addr->addr.unix_addr.sun_path, 107);
    impl->socket_path[107] = '\0';

    return true;
}

static bool unix_transport_connect(ssn_transport_t* transport,
                                   const ssn_address_t* addr,
                                   int timeout_ms)
{
    unix_transport_impl_t* impl = (unix_transport_impl_t*)transport->impl_data;

    impl->sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (impl->sock_fd < 0) {
        LOG_ERROR("Failed to create Unix socket: %s", strerror(errno));
        return false;
    }

    if (impl->non_blocking) {
        int flags = fcntl(impl->sock_fd, F_GETFL, 0);
        fcntl(impl->sock_fd, F_SETFL, flags | O_NONBLOCK);
    }

    memcpy(&impl->addr, &addr->addr.unix_addr, sizeof(struct sockaddr_un));

    if (connect(impl->sock_fd, (struct sockaddr*)&impl->addr,
                sizeof(struct sockaddr_un)) < 0) {
        if (errno != EINPROGRESS) {
            LOG_ERROR("Failed to connect Unix socket: %s", strerror(errno));
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

            int ret = select(impl->sock_fd + 1, NULL, &write_fds, NULL, &tv);
            if (ret <= 0) {
                LOG_ERROR("Unix socket connect timeout");
                close(impl->sock_fd);
                impl->sock_fd = -1;
                return false;
            }
        }
    }

    impl->is_server = false;
    impl->last_activity = time(NULL);
    strncpy(impl->socket_path, addr->addr.unix_addr.sun_path, 107);
    impl->socket_path[107] = '\0';
    LOG_DEBUG("Connected to Unix socket: %s", impl->socket_path);

    return true;
}

static bool unix_transport_disconnect(ssn_transport_t* transport)
{
    unix_transport_impl_t* impl = (unix_transport_impl_t*)transport->impl_data;

    if (impl->sock_fd >= 0) {
        shutdown(impl->sock_fd, SHUT_RDWR);
        close(impl->sock_fd);
        impl->sock_fd = -1;
    }

    return true;
}

static bool unix_transport_is_connected(const ssn_transport_t* transport)
{
    const unix_transport_impl_t* impl =
        (const unix_transport_impl_t*)transport->impl_data;

    return (impl->sock_fd >= 0);
}

static int unix_transport_send(ssn_transport_t* transport,
                               const void* data,
                               size_t len)
{
    unix_transport_impl_t* impl = (unix_transport_impl_t*)transport->impl_data;

    if (!transport->valid || impl->sock_fd < 0) {
        LOG_ERROR("Unix socket not valid for sending");
        return -1;
    }

    ssize_t sent = send(impl->sock_fd, data, len, MSG_NOSIGNAL);
    if (sent < 0) {
        LOG_ERROR("Failed to send data via Unix socket: %s", strerror(errno));
        transport->stats.send_errors++;
        return -1;
    }

    transport->stats.bytes_sent += sent;
    transport->stats.packets_sent++;
    impl->last_activity = time(NULL);

    return (int)sent;
}

static int unix_transport_recv(ssn_transport_t* transport,
                               void* buffer,
                               size_t size,
                               int timeout_ms)
{
    unix_transport_impl_t* impl = (unix_transport_impl_t*)transport->impl_data;

    if (!transport->valid || impl->sock_fd < 0) {
        LOG_ERROR("Unix socket not valid for receiving");
        return -1;
    }

    if (timeout_ms > 0) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(impl->sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    ssize_t received = recv(impl->sock_fd, buffer, size, 0);
    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("Failed to receive data via Unix socket: %s",
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

static bool unix_transport_listen(ssn_transport_t* transport, int backlog)
{
    unix_transport_impl_t* impl = (unix_transport_impl_t*)transport->impl_data;

    struct stat st;
    if (stat(impl->socket_path, &st) == 0) {
        if (S_ISSOCK(st.st_mode)) {
            unlink(impl->socket_path);
            LOG_INFO("Removed existing Unix socket: %s", impl->socket_path);
        } else {
            LOG_ERROR("Path %s is not a socket file", impl->socket_path);
            return false;
        }
    }

    if (bind(impl->sock_fd, (struct sockaddr*)&impl->addr,
             sizeof(struct sockaddr_un)) < 0) {
        LOG_ERROR("Failed to bind Unix socket: %s", strerror(errno));
        return false;
    }

    if (listen(impl->sock_fd, backlog) < 0) {
        LOG_ERROR("Failed to listen on Unix socket: %s", strerror(errno));
        return false;
    }

    impl->is_server = true;
    LOG_DEBUG("Unix socket server listening on: %s", impl->socket_path);

    return true;
}

static ssn_transport_t* unix_transport_accept(ssn_transport_t* transport,
                                              ssn_address_t* client_addr,
                                              int timeout_ms)
{
    unix_transport_impl_t* impl = (unix_transport_impl_t*)transport->impl_data;

    if (!transport->valid || impl->sock_fd < 0) {
        LOG_ERROR("Unix socket not valid for accepting");
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

    struct sockaddr_un client_addr_un;
    socklen_t addr_len = sizeof(client_addr_un);

    int client_fd = accept(impl->sock_fd, (struct sockaddr*)&client_addr_un,
                           &addr_len);
    if (client_fd < 0) {
        LOG_ERROR("Failed to accept Unix socket connection: %s",
                  strerror(errno));
        return NULL;
    }

    ssn_transport_config_t config = transport->config;
    config.non_blocking = impl->non_blocking;

    ssn_transport_t* client_transport = ssn_transport_create(SSN_TRANSPORT_UNIX,
                                                              &config);
    if (!client_transport) {
        close(client_fd);
        return NULL;
    }

    unix_transport_impl_t* client_impl =
        (unix_transport_impl_t*)client_transport->impl_data;
    client_impl->sock_fd = client_fd;
    client_impl->is_server = false;
    memcpy(&client_impl->addr, &client_addr_un, sizeof(struct sockaddr_un));
    client_impl->last_activity = time(NULL);

    if (client_addr) {
        client_addr->type = SSN_TRANSPORT_UNIX;
        memcpy(&client_addr->addr.unix_addr, &client_addr_un,
               sizeof(struct sockaddr_un));
        snprintf(client_addr->address_str,
                 SSN_TRANSPORT_MAX_ADDRESS_LEN,
                 "unix://%s", client_addr_un.sun_path);
    }

    LOG_DEBUG("Accepted connection from Unix socket");

    return client_transport;
}

static bool unix_transport_set_option(ssn_transport_t* transport,
                                      int option,
                                      const void* value)
{
    unix_transport_impl_t* impl = (unix_transport_impl_t*)transport->impl_data;

    switch (option) {
        default:
            return false;
    }

    return true;
}

static bool unix_transport_get_option(const ssn_transport_t* transport,
                                      int option,
                                      void* value)
{
    const unix_transport_impl_t* impl =
        (const unix_transport_impl_t*)transport->impl_data;

    switch (option) {
        case 0: // 特殊选项，用于获取文件描述符
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

static bool unix_transport_get_stats(const ssn_transport_t* transport,
                                     ssn_transport_stats_t* stats)
{
    if (!transport || !stats) {
        return false;
    }

    memcpy(stats, &transport->stats, sizeof(ssn_transport_stats_t));
    return true;
}

static bool unix_transport_get_address(const ssn_transport_t* transport,
                                       ssn_address_t* addr)
{
    const unix_transport_impl_t* impl =
        (const unix_transport_impl_t*)transport->impl_data;

    if (!transport || !addr) {
        return false;
    }

    addr->type = SSN_TRANSPORT_UNIX;
    memcpy(&addr->addr.unix_addr, &impl->addr, sizeof(struct sockaddr_un));
    snprintf(addr->address_str, SSN_TRANSPORT_MAX_ADDRESS_LEN,
             "unix://%s", impl->socket_path);

    return true;
}

static void unix_transport_destroy(ssn_transport_t* transport)
{
    if (!transport) {
        return;
    }

    unix_transport_impl_t* impl = (unix_transport_impl_t*)transport->impl_data;

    if (impl->sock_fd >= 0) {
        close(impl->sock_fd);
        impl->sock_fd = -1;
    }

    if (impl->is_server && impl->socket_path[0] != '\0') {
        unlink(impl->socket_path);
    }

    free(impl);
    transport->impl_data = NULL;
}

static ssn_transport_ops_t unix_transport_ops = {
    .bind = unix_transport_bind,
    .connect = unix_transport_connect,
    .disconnect = unix_transport_disconnect,
    .is_connected = unix_transport_is_connected,
    .send = unix_transport_send,
    .recv = unix_transport_recv,
    .listen = unix_transport_listen,
    .accept = unix_transport_accept,
    .set_option = unix_transport_set_option,
    .get_option = unix_transport_get_option,
    .get_stats = unix_transport_get_stats,
    .get_address = unix_transport_get_address,
    .destroy = unix_transport_destroy
};

ssn_transport_t* unix_transport_create(const ssn_transport_config_t* config)
{
    ssn_transport_t* transport = (ssn_transport_t*)calloc(1,
                                                            sizeof(ssn_transport_t));
    if (!transport) {
        LOG_ERROR("Failed to allocate memory for Unix transport");
        return NULL;
    }

    unix_transport_impl_t* impl =
        (unix_transport_impl_t*)calloc(1, sizeof(unix_transport_impl_t));
    if (!impl) {
        LOG_ERROR("Failed to allocate memory for Unix transport impl");
        free(transport);
        return NULL;
    }

    impl->sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (impl->sock_fd < 0) {
        LOG_ERROR("Failed to create Unix socket: %s", strerror(errno));
        free(impl);
        free(transport);
        return NULL;
    }

    int optval = 1;
    setsockopt(impl->sock_fd, SOL_SOCKET, SO_REUSEADDR,
               &optval, sizeof(optval));

    impl->is_server = false;
    impl->non_blocking = config ? config->non_blocking : false;
    impl->socket_path[0] = '\0';
    impl->last_activity = time(NULL);

    transport->type = SSN_TRANSPORT_UNIX;
    transport->ops = unix_transport_ops;
    transport->impl_data = impl;
    transport->valid = true;

    if (config) {
        transport->config = *config;
    }

    memset(&transport->stats, 0, sizeof(ssn_transport_stats_t));

    return transport;
}

