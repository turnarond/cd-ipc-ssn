#include "ipc_platform.h"
#include <stdlib.h>

#ifdef IPC_PLATFORM_WINDOWS
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <signal.h>
#endif

int ipc_platform_init(void)
{
#ifdef IPC_PLATFORM_WINDOWS
    WSADATA wsaData;
    return (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) ? 0 : -1;
#else
    // 忽略SIGPIPE信号，防止向已关闭的socket写入时程序崩溃
    signal(SIGPIPE, SIG_IGN);
    return 0;
#endif
}

void ipc_platform_cleanup(void)
{
#ifdef IPC_PLATFORM_WINDOWS
    WSACleanup();
#endif
}