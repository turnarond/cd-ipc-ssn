/**
 * @file ipc_thread.c
 * @brief 线程操作实现
 */

#include "ipc_platform.h"
#include <stdlib.h>

#ifdef IPC_PLATFORM_WINDOWS
    #include <windows.h>
    struct ipc_thread { HANDLE handle; };
#else
    #include <pthread.h>
    #include <unistd.h>
    struct ipc_thread { pthread_t tid; };
#endif

/**
 * @brief 创建线程
 * @param out 输出参数，返回创建的线程指针
 * @param func 线程函数
 * @param arg 线程函数参数
 * @return 0 成功，-1 失败
 */
int ipc_thread_create(ipc_thread_t **out, void *(*func)(void *), void *arg)
{
    ipc_thread_t *thread = malloc(sizeof(ipc_thread_t));
    if (!thread) return -1;

#ifdef IPC_PLATFORM_WINDOWS
    thread->handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    if (!thread->handle) {
        free(thread);
        return -1;
    }
#else
    if (pthread_create(&thread->tid, NULL, func, arg) != 0) {
        free(thread);
        return -1;
    }
#endif

    *out = thread;
    return 0;
}

/**
 * @brief 等待线程结束
 * @param thread 线程指针
 * @return 0 成功，-1 失败
 */
int ipc_thread_join(ipc_thread_t *thread)
{
    if (!thread) return -1;
#ifdef IPC_PLATFORM_WINDOWS
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
#else
    pthread_join(thread->tid, NULL);
#endif
    free(thread);
    return 0;
}

/**
 * @brief 线程退出
 */
void ipc_thread_exit(void)
{
#ifdef IPC_PLATFORM_WINDOWS
    ExitThread(0);
#else
    pthread_exit(NULL);
#endif
}

/**
 * @brief 线程睡眠指定毫秒数
 * @param ms 睡眠毫秒数
 */
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