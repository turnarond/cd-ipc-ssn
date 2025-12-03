#include "ipc_platform.h"
#include <stdlib.h>

#ifdef IPC_PLATFORM_WINDOWS
    #include <windows.h>
    struct ipc_mutex { CRITICAL_SECTION cs; };
    struct ipc_spinlock { CRITICAL_SECTION cs; }; // Windows: no true spinlock in user mode
#else
    #include <pthread.h>
    struct ipc_mutex { pthread_mutex_t mtx; };
    struct ipc_spinlock { pthread_spinlock_t lock; };
#endif

// ===== Mutex =====
int ipc_mutex_init(ipc_mutex_t **out)
{
    ipc_mutex_t *m = malloc(sizeof(ipc_mutex_t));
    if (!m) return -1;

#ifdef IPC_PLATFORM_WINDOWS
    if (!InitializeCriticalSectionEx(&m->cs, 8192, CRITICAL_SECTION_NO_DEBUG_INFO)) {
        free(m);
        return -1;
    }
#else
    if (pthread_mutex_init(&m->mtx, NULL) != 0) {
        free(m);
        return -1;
    }
#endif
    *out = m;
    return 0;
}

int ipc_mutex_destroy(ipc_mutex_t *m)
{
    if (!m) return -1;
#ifdef IPC_PLATFORM_WINDOWS
    DeleteCriticalSection(&m->cs);
#else
    pthread_mutex_destroy(&m->mtx);
#endif
    free(m);
    return 0;
}

int ipc_mutex_lock(ipc_mutex_t *m) {
#ifdef IPC_PLATFORM_WINDOWS
    EnterCriticalSection(&m->cs);
#else
    pthread_mutex_lock(&m->mtx);
#endif
    return 0;
}

int ipc_mutex_unlock(ipc_mutex_t *m) {
#ifdef IPC_PLATFORM_WINDOWS
    LeaveCriticalSection(&m->cs);
#else
    pthread_mutex_unlock(&m->mtx);
#endif
    return 0;
}

// ===== Spinlock =====
int ipc_spinlock_init(ipc_spinlock_t **out)
{
    ipc_spinlock_t *s = malloc(sizeof(ipc_spinlock_t));
    if (!s) return -1;

#ifdef IPC_PLATFORM_WINDOWS
    if (!InitializeCriticalSectionEx(&s->cs, 4000, CRITICAL_SECTION_NO_DEBUG_INFO)) {
        free(s);
        return -1;
    }
#else
    if (pthread_spin_init(&s->lock, 0) != 0) {
        free(s);
        return -1;
    }
#endif
    *out = s;
    return 0;
}

int ipc_spinlock_destroy(ipc_spinlock_t *s)
{
    if (!s) return -1;
#ifdef IPC_PLATFORM_WINDOWS
    DeleteCriticalSection(&s->cs);
#else
    pthread_spin_destroy(&s->lock);
#endif
    free(s);
    return 0;
}

int ipc_spinlock_lock(ipc_spinlock_t *s) {
#ifdef IPC_PLATFORM_WINDOWS
    EnterCriticalSection(&s->cs);
#else
    pthread_spin_lock(&s->lock);
#endif
    return 0;
}

int ipc_spinlock_unlock(ipc_spinlock_t *s) {
#ifdef IPC_PLATFORM_WINDOWS
    LeaveCriticalSection(&s->cs);
#else
    pthread_spin_unlock(&s->lock);
#endif
    return 0;
}