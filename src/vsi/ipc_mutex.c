/**
 * @file ipc_mutex.c
 * @brief 互斥锁和自旋锁实现
 */

#include "ipc_platform.h"
#include <stdlib.h>

// ===== Mutex =====
#ifdef IPC_PLATFORM_WINDOWS
    #include <windows.h>
    struct ipc_mutex { CRITICAL_SECTION cs; };
    struct ipc_spinlock { CRITICAL_SECTION cs; };
    struct ipc_thread { HANDLE handle; };
#else
    #include <pthread.h>
    struct ipc_mutex { pthread_mutex_t mtx; };
    struct ipc_spinlock { pthread_spinlock_t lock; };
    struct ipc_thread { pthread_t tid; };
#endif

/**
 * @brief 初始化互斥锁
 * @param out 输出参数，返回初始化后的互斥锁指针
 * @return 0 成功，-1 失败
 */
int ipc_mutex_init(ipc_mutex_t **out)
{
    ipc_mutex_t *mutex = malloc(sizeof(ipc_mutex_t));
    if (!mutex) return -1;

#ifdef IPC_PLATFORM_WINDOWS
    if (!InitializeCriticalSectionEx(&mutex->cs, 8192, CRITICAL_SECTION_NO_DEBUG_INFO)) {
        free(mutex);
        return -1;
    }
#else
    if (pthread_mutex_init(&mutex->mtx, NULL) != 0) {
        free(mutex);
        return -1;
    }
#endif
    *out = mutex;
    return 0;
}

/**
 * @brief 销毁互斥锁
 * @param mutex 互斥锁指针
 * @return 0 成功，-1 失败
 */
int ipc_mutex_destroy(ipc_mutex_t *mutex)
{
    if (!mutex) return -1;
#ifdef IPC_PLATFORM_WINDOWS
    DeleteCriticalSection(&mutex->cs);
#else
    pthread_mutex_destroy(&mutex->mtx);
#endif
    free(mutex);
    return 0;
}

/**
 * @brief 加锁互斥锁
 * @param mutex 互斥锁指针
 * @return 0 成功
 */
int ipc_mutex_lock(ipc_mutex_t *mutex)
{
#ifdef IPC_PLATFORM_WINDOWS
    EnterCriticalSection(&mutex->cs);
#else
    pthread_mutex_lock(&mutex->mtx);
#endif
    return 0;
}

/**
 * @brief 解锁互斥锁
 * @param mutex 互斥锁指针
 * @return 0 成功
 */
int ipc_mutex_unlock(ipc_mutex_t *mutex)
{
#ifdef IPC_PLATFORM_WINDOWS
    LeaveCriticalSection(&mutex->cs);
#else
    pthread_mutex_unlock(&mutex->mtx);
#endif
    return 0;
}

// ===== Spinlock =====

/**
 * @brief 初始化自旋锁
 * @param out 输出参数，返回初始化后的自旋锁指针
 * @return 0 成功，-1 失败
 */
int ipc_spinlock_init(ipc_spinlock_t **out)
{
    ipc_spinlock_t *spinlock = malloc(sizeof(ipc_spinlock_t));
    if (!spinlock) return -1;

#ifdef IPC_PLATFORM_WINDOWS
    if (!InitializeCriticalSectionEx(&spinlock->cs, 4000, CRITICAL_SECTION_NO_DEBUG_INFO)) {
        free(spinlock);
        return -1;
    }
#else
    if (pthread_spin_init(&spinlock->lock, 0) != 0) {
        free(spinlock);
        return -1;
    }
#endif
    *out = spinlock;
    return 0;
}

/**
 * @brief 销毁自旋锁
 * @param spinlock 自旋锁指针
 * @return 0 成功，-1 失败
 */
int ipc_spinlock_destroy(ipc_spinlock_t *spinlock)
{
    if (!spinlock) return -1;
#ifdef IPC_PLATFORM_WINDOWS
    DeleteCriticalSection(&spinlock->cs);
#else
    pthread_spin_destroy(&spinlock->lock);
#endif
    free(spinlock);
    return 0;
}

/**
 * @brief 加锁自旋锁
 * @param spinlock 自旋锁指针
 * @return 0 成功
 */
int ipc_spinlock_lock(ipc_spinlock_t *spinlock)
{
#ifdef IPC_PLATFORM_WINDOWS
    EnterCriticalSection(&spinlock->cs);
#else
    pthread_spin_lock(&spinlock->lock);
#endif
    return 0;
}

/**
 * @brief 解锁自旋锁
 * @param spinlock 自旋锁指针
 * @return 0 成功
 */
int ipc_spinlock_unlock(ipc_spinlock_t *spinlock)
{
#ifdef IPC_PLATFORM_WINDOWS
    LeaveCriticalSection(&spinlock->cs);
#else
    pthread_spin_unlock(&spinlock->lock);
#endif
    return 0;
}