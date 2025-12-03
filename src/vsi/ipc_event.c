#include "ipc_platform.h"
#include <stdlib.h>
#include <string.h>

#ifdef IPC_PLATFORM_WINDOWS
    #include <winsock2.h>
#elif !defined(IPC_PLATFORM_SYLIXOS)
    #include <unistd.h>
    #include <fcntl.h>
#endif

#if defined(IPC_PLATFORM_LINUX) || defined(IPC_PLATFORM_SYLIXOS)
    #include <sys/eventfd.h>
#endif

struct ipc_event_pair {
#ifdef IPC_PLATFORM_LINUX
    int fd; // eventfd
#elif defined(IPC_PLATFORM_SYLIXOS)
    int fd; // eventfd
#elif defined(IPC_PLATFORM_WINDOWS)
    ipc_socket_t read_fd;
    ipc_socket_t write_fd;
#else
    int read_fd;
    int write_fd;
#endif
};

int ipc_event_pair_create(ipc_event_pair_t **out)
{
    ipc_event_pair_t *pair = calloc(1, sizeof(ipc_event_pair_t));
    if (!pair) return -1;

#if defined(IPC_PLATFORM_LINUX) || defined(IPC_PLATFORM_SYLIXOS)
    pair->fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (pair->fd < 0) {
        free(pair);
        return -1;
    }
#elif defined(IPC_PLATFORM_WINDOWS)
    struct sockaddr_in addr = {0};
    socklen_t len = sizeof(addr);

    pair->read_fd = ipc_socket_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP, true);
    pair->write_fd = ipc_socket_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP, true);
    if (pair->read_fd < 0 || pair->write_fd < 0) goto fail_win;

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(pair->read_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        getsockname(pair->read_fd, (struct sockaddr*)&addr, &len) < 0 ||
        connect(pair->write_fd, (struct sockaddr*)&addr, len) < 0) {
        goto fail_win;
    }
    *out = pair;
    return 0;

fail_win:
    if (pair->read_fd >= 0) ipc_socket_close(pair->read_fd);
    if (pair->write_fd >= 0) ipc_socket_close(pair->write_fd);
    free(pair);
    return -1;
#else // POSIX pipe
    if (pipe(&pair->read_fd) != 0) {
        free(pair);
        return -1;
    }
    fcntl(pair->read_fd, F_SETFL, fcntl(pair->read_fd, F_GETFL) | O_NONBLOCK);
    fcntl(pair->write_fd, F_SETFL, fcntl(pair->write_fd, F_GETFL) | O_NONBLOCK);
#endif

    *out = pair;
    return 0;
}

void ipc_event_pair_destroy(ipc_event_pair_t *pair)
{
    if (!pair) return;

#if defined(IPC_PLATFORM_LINUX) || defined(IPC_PLATFORM_SYLIXOS)
    if (pair->fd >= 0) ipc_socket_close(pair->fd);
#elif defined(IPC_PLATFORM_WINDOWS)
    ipc_socket_close(pair->read_fd);
    ipc_socket_close(pair->write_fd);
#else
    ipc_socket_close(pair->read_fd);
    ipc_socket_close(pair->write_fd);
#endif
    free(pair);
}

void ipc_event_pair_signal(ipc_event_pair_t *pair)
{
#if defined(IPC_PLATFORM_LINUX) || defined(IPC_PLATFORM_SYLIXOS)
    eventfd_t val = 1;
    eventfd_write(pair->fd, val);
#elif defined(IPC_PLATFORM_WINDOWS)
    uint8_t val = 1;
    send(pair->write_fd, &val, 1, MSG_NOSIGNAL);
#else
    uint64_t val = 1;
    write(pair->write_fd, &val, sizeof(val));
#endif
}

void ipc_event_pair_drain(ipc_event_pair_t *pair)
{
#if defined(IPC_PLATFORM_LINUX) || defined(IPC_PLATFORM_SYLIXOS)
    eventfd_t val;
    eventfd_read(pair->fd, &val);
#elif defined(IPC_PLATFORM_WINDOWS)
    uint8_t val;
    while (recv(pair->read_fd, &val, 1, MSG_DONTWAIT) > 0);
#else
    uint64_t val;
    while (read(pair->read_fd, &val, sizeof(val)) > 0);
#endif
}

int ipc_event_pair_get_read_fd(const ipc_event_pair_t *pair)
{
#if defined(IPC_PLATFORM_LINUX) || defined(IPC_PLATFORM_SYLIXOS)
    return pair->fd;
#elif defined(IPC_PLATFORM_WINDOWS)
    return pair->read_fd;
#else
    return pair->read_fd;
#endif
}