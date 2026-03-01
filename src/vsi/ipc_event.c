/**
 * @file ipc_event.c
 * @brief 事件对实现
 */

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

/**
 * @brief 创建事件对
 * @param out 输出参数，返回创建的事件对指针
 * @return 0 成功，-1 失败
 */
int ipc_event_pair_create(ipc_event_pair_t **out)
{
    ipc_event_pair_t *event_pair = calloc(1, sizeof(ipc_event_pair_t));
    if (!event_pair) return -1;

#if defined(IPC_PLATFORM_LINUX) || defined(IPC_PLATFORM_SYLIXOS)
    event_pair->fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (event_pair->fd < 0) {
        free(event_pair);
        return -1;
    }
#elif defined(IPC_PLATFORM_WINDOWS)
    struct sockaddr_in addr = {0};
    socklen_t len = sizeof(addr);

    event_pair->read_fd = ipc_socket_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP, true);
    event_pair->write_fd = ipc_socket_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP, true);
    if (event_pair->read_fd < 0 || event_pair->write_fd < 0) goto fail_win;

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(event_pair->read_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        getsockname(event_pair->read_fd, (struct sockaddr*)&addr, &len) < 0 ||
        connect(event_pair->write_fd, (struct sockaddr*)&addr, len) < 0) {
        goto fail_win;
    }
    *out = event_pair;
    return 0;

fail_win:
    if (event_pair->read_fd >= 0) ipc_socket_close(event_pair->read_fd);
    if (event_pair->write_fd >= 0) ipc_socket_close(event_pair->write_fd);
    free(event_pair);
    return -1;
#else // POSIX pipe
    if (pipe(&event_pair->read_fd) != 0) {
        free(event_pair);
        return -1;
    }
    fcntl(event_pair->read_fd, F_SETFL, fcntl(event_pair->read_fd, F_GETFL) | O_NONBLOCK);
    fcntl(event_pair->write_fd, F_SETFL, fcntl(event_pair->write_fd, F_GETFL) | O_NONBLOCK);
#endif

    *out = event_pair;
    return 0;
}

/**
 * @brief 销毁事件对
 * @param event_pair 事件对指针
 */
void ipc_event_pair_destroy(ipc_event_pair_t *event_pair)
{
    if (!event_pair) return;

#if defined(IPC_PLATFORM_LINUX) || defined(IPC_PLATFORM_SYLIXOS)
    if (event_pair->fd >= 0) ipc_socket_close(event_pair->fd);
#elif defined(IPC_PLATFORM_WINDOWS)
    ipc_socket_close(event_pair->read_fd);
    ipc_socket_close(event_pair->write_fd);
#else
    ipc_socket_close(event_pair->read_fd);
    ipc_socket_close(event_pair->write_fd);
#endif
    free(event_pair);
}

/**
 * @brief 发送事件信号
 * @param event_pair 事件对指针
 */
void ipc_event_pair_signal(ipc_event_pair_t *event_pair)
{
#if defined(IPC_PLATFORM_LINUX) || defined(IPC_PLATFORM_SYLIXOS)
    eventfd_t val = 1;
    eventfd_write(event_pair->fd, val);
#elif defined(IPC_PLATFORM_WINDOWS)
    uint8_t val = 1;
    send(event_pair->write_fd, &val, 1, MSG_NOSIGNAL);
#else
    uint64_t val = 1;
    write(event_pair->write_fd, &val, sizeof(val));
#endif
}

/**
 * @brief 清空事件信号
 * @param event_pair 事件对指针
 */
void ipc_event_pair_drain(ipc_event_pair_t *event_pair)
{
#if defined(IPC_PLATFORM_LINUX) || defined(IPC_PLATFORM_SYLIXOS)
    eventfd_t val;
    eventfd_read(event_pair->fd, &val);
#elif defined(IPC_PLATFORM_WINDOWS)
    uint8_t val;
    while (recv(event_pair->read_fd, &val, 1, MSG_DONTWAIT) > 0);
#else
    uint64_t val;
    while (read(event_pair->read_fd, &val, sizeof(val)) > 0);
#endif
}

/**
 * @brief 获取事件对的读文件描述符
 * @param event_pair 事件对指针
 * @return 读文件描述符
 */
int ipc_event_pair_get_read_fd(const ipc_event_pair_t *event_pair)
{
#if defined(IPC_PLATFORM_LINUX) || defined(IPC_PLATFORM_SYLIXOS)
    return event_pair->fd;
#elif defined(IPC_PLATFORM_WINDOWS)
    return event_pair->read_fd;
#else
    return event_pair->read_fd;
#endif
}