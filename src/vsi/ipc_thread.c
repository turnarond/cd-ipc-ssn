#include "ipc_platform.h"
#include <stdlib.h>

#ifdef IPC_PLATFORM_WINDOWS
    #include <windows.h>
    struct ipc_thread { HANDLE handle; };
#else
    #include <pthread.h>
    struct ipc_thread { pthread_t tid; };
#endif

int ipc_thread_create(ipc_thread_t **out, void *(*func)(void *), void *arg)
{
    ipc_thread_t *thr = malloc(sizeof(ipc_thread_t));
    if (!thr) return -1;

#ifdef IPC_PLATFORM_WINDOWS
    thr->handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    if (!thr->handle) {
        free(thr);
        return -1;
    }
#else
    if (pthread_create(&thr->tid, NULL, func, arg) != 0) {
        free(thr);
        return -1;
    }
#endif

    *out = thr;
    return 0;
}

int ipc_thread_join(ipc_thread_t *thr)
{
    if (!thr) return -1;
#ifdef IPC_PLATFORM_WINDOWS
    WaitForSingleObject(thr->handle, INFINITE);
    CloseHandle(thr->handle);
#else
    pthread_join(thr->tid, NULL);
#endif
    free(thr);
    return 0;
}

void ipc_thread_exit(void)
{
#ifdef IPC_PLATFORM_WINDOWS
    ExitThread(0);
#else
    pthread_exit(NULL);
#endif
}

void ipc_thread_msleep(unsigned int ms)
{
#ifdef IPC_PLATFORM_SYLIXOS
    Lw_Time_MSleep(ms);
#elif defined(IPC_PLATFORM_WINDOWS)
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}