/**
 * @file ipc_socket.c
 * @brief 套接字操作实现
 */

#include "ipc_platform.h"
#include <string.h>
#include <errno.h>

#ifndef IPC_PLATFORM_WINDOWS
    #include <sys/ioctl.h>
    #include <net/if.h>
    #include <unistd.h>
#endif

/**
 * @brief 创建套接字
 * @param family 地址族
 * @param type 套接字类型
 * @param protocol 协议
 * @param nonblocking 是否非阻塞
 * @return 套接字描述符，失败返回-1
 */
ipc_socket_t ipc_socket_create(int family, int type, int protocol, bool nonblocking)
{
#ifdef IPC_PLATFORM_LINUX
    int flags = SOCK_CLOEXEC;
    if (nonblocking) flags |= SOCK_NONBLOCK;
    return socket(family, type | flags, protocol);
#elif defined(IPC_PLATFORM_SYLIXOS)
    int flags = SOCK_CLOEXEC;
    if (nonblocking) flags |= SOCK_NONBLOCK;
    return socket(family, type | flags, protocol);
#else
    ipc_socket_t sock = socket(family, type, protocol);
    if (sock >= 0 && nonblocking) {
#ifdef IPC_PLATFORM_WINDOWS
        u_long on = 1;
        ioctlsocket(sock, FIONBIO, &on);
#else
        int on = 1;
        ioctl(sock, FIONBIO, &on);
#endif
    }
    return sock;
#endif
}

/**
 * @brief 关闭套接字
 * @param sock 套接字描述符
 */
void ipc_socket_close(ipc_socket_t sock)
{
#ifdef IPC_PLATFORM_WINDOWS
    closesocket(sock);
#else
    close(sock);
#endif
}

