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

/**
 * @brief 关闭套接字的读写
 * @param sock 套接字描述符
 */
void ipc_socket_shutdown(ipc_socket_t sock)
{
#ifdef IPC_PLATFORM_WINDOWS
    shutdown(sock, SD_BOTH);
#else
    shutdown(sock, SHUT_RDWR);
#endif
}

/**
 * @brief 设置套接字发送超时
 * @param sock 套接字描述符
 * @param timeout_ms 超时时间（毫秒）
 */
void ipc_socket_set_send_timeout(ipc_socket_t sock, int timeout_ms)
{
    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000
    };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/**
 * @brief 将套接字绑定到指定网络接口
 * @param sock 套接字描述符
 * @param ifname 网络接口名称
 * @return 成功返回true，失败返回false
 */
bool ipc_socket_bind_to_interface(ipc_socket_t sock, const char *ifname)
{
#if defined(SO_BINDTODEVICE) && (defined(IPC_PLATFORM_LINUX) || defined(IPC_PLATFORM_SYLIXOS))
    if (!ifname || strlen(ifname) >= IF_NAMESIZE) return false;
    struct ifreq req = {0};
    strncpy(req.ifr_name, ifname, IF_NAMESIZE - 1);
    return setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, &req, sizeof(req)) == 0;
#else
    (void)sock; (void)ifname;
    return false;
#endif
}