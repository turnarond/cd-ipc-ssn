/*
 * ipc_platform.h - Cross-platform IPC utility library
 *
 * Features:
 *   - Thread management
 *   - Mutex & spinlock
 *   - Socket I/O abstraction
 *   - Event signaling (for select/poll wake-up)
 *   - Platform init/cleanup
 */

#ifndef IPC_PLATFORM_H
#define IPC_PLATFORM_H

#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif
/* ==================== Platform Detection ==================== */
#if defined(_WIN32)
    #define IPC_PLATFORM_WINDOWS 1
#elif defined(__linux__)
    #define IPC_PLATFORM_LINUX 1
#elif defined(SYLIXOS)
    #define IPC_PLATFORM_SYLIXOS 1
#else
    #error "Unsupported platform"
#endif

/* ==================== Configuration ==================== */
#ifndef IF_NAMESIZE
    #define IF_NAMESIZE 16
#endif

#ifndef SRV_NAME_LEN
    #define SRV_NAME_LEN 64
#endif

/* Server timer period (ms) */
#define IPC_TIMER_PERIOD                    10
#define IPC_SERVER_BACKLOG                  32
#define IPC_DEF_SEND_TIMEOUT                100     // ms
#define IPC_SERVER_DEF_HANDSHAKE_TIMEOUT    5000    // ms
#define IPC_SERVER_KEEPALIVE_TIMEOUT        10      // seconds

#if defined(IPC_PLATFORM_SYLIXOS) || defined(IPC_PLATFORM_LINUX)
    #define IPC_INHERIT_NODELAY 1
#endif
#if defined(IPC_PLATFORM_SYLIXOS)
    #define IPC_HAS_SIN_LEN 1
#endif

/* ==================== Types ==================== */
// Opaque handles (hide implementation)
typedef struct ipc_mutex     ipc_mutex_t;
typedef struct ipc_spinlock  ipc_spinlock_t;
typedef struct ipc_thread    ipc_thread_t;

/* ==================== Platform Init/Cleanup ==================== */
int  ipc_platform_init(void);
void ipc_platform_cleanup(void);

/* ==================== Threading ==================== */
int  ipc_thread_create(ipc_thread_t **thread, void *(*func)(void *), void *arg);
int  ipc_thread_join(ipc_thread_t *thread);
void ipc_thread_exit(void);
void ipc_thread_msleep(unsigned int ms);

/* ==================== Synchronization ==================== */
int  ipc_mutex_init(ipc_mutex_t **mutex);
int  ipc_mutex_destroy(ipc_mutex_t *mutex);
int  ipc_mutex_lock(ipc_mutex_t *mutex);
int  ipc_mutex_unlock(ipc_mutex_t *mutex);

int  ipc_spinlock_init(ipc_spinlock_t **spin);
int  ipc_spinlock_destroy(ipc_spinlock_t *spin);
int  ipc_spinlock_lock(ipc_spinlock_t *spin);
int  ipc_spinlock_unlock(ipc_spinlock_t *spin);

/* ==================== Socket ==================== */
typedef int ipc_socket_t;

ipc_socket_t ipc_socket_create(int family, int type, int protocol, bool nonblocking);
void         ipc_socket_close(ipc_socket_t sock);
void         ipc_socket_shutdown(ipc_socket_t sock);
void         ipc_socket_set_send_timeout(ipc_socket_t sock, int timeout_ms);
bool         ipc_socket_bind_to_interface(ipc_socket_t sock, const char *ifname);

/* ==================== Event Pair ==================== */
typedef struct ipc_event_pair ipc_event_pair_t;

int  ipc_event_pair_create(ipc_event_pair_t **pair);
void ipc_event_pair_destroy(ipc_event_pair_t *pair);
void ipc_event_pair_signal(ipc_event_pair_t *pair);
void ipc_event_pair_drain(ipc_event_pair_t *pair);
int  ipc_event_pair_get_read_fd(const ipc_event_pair_t *pair);   // for select/poll

/* ==================== Memory Barrier ==================== */
#if defined(__GNUC__)
    #define ipc_memory_barrier() __sync_synchronize()
#else
    #define ipc_memory_barrier() ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* IPC_PLATFORM_H */