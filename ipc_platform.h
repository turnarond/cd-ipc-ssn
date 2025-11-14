/*
 * IPC platform
 */

#ifndef IPC_PLATFORM_H
#define IPC_PLATFORM_H

#include <time.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <net/if.h>
#if !defined(IF_NAMESIZE)
#define IF_NAMESIZE  16
#endif

#if defined(SYLIXOS) || defined(__linux__)
#include <sys/eventfd.h>
#endif

#if defined(SYLIXOS) 
#define IPC_HAS_SIN_LEN  1
#endif

#if defined(SYLIXOS) || defined(__linux__) 
#define IPC_INHERIT_NODELAY 1
#endif

/* Memory barrier */
#if defined(__GNUC__)
#define ipc_memory_barrier()   __sync_synchronize()
#else
#define ipc_memory_barrier()
#endif

typedef pthread_spinlock_t      ipc_spin_t;
#define IPC_SPIN_INITIALIZER   PTHREAD_SPINLOCK_INITIALIZER
#define ipc_spin_init(spin)    pthread_spin_init(spin, 0)
#define ipc_spin_destroy(spin) pthread_spin_destroy(spin)
#define ipc_spin_lock(spin)    pthread_spin_lock(spin)
#define ipc_spin_unlock(spin)  pthread_spin_unlock(spin)

typedef pthread_mutex_t         ipc_mutex_t;
#define IPC_MUTEX_INITIALIZER  PTHREAD_MUTEX_INITIALIZER
#define ipc_mutex_init(m)      pthread_mutex_init(m, NULL)
#define ipc_mutex_destroy(m)   pthread_mutex_destroy(m)
#define ipc_mutex_lock(m)      pthread_mutex_lock(m)
#define ipc_mutex_unlock(m)    pthread_mutex_unlock(m)

/* Multi-threading */
typedef pthread_t                       ipc_thread_t;
#define ipc_thread_create(id, f, a)    pthread_create(id, NULL, f, a)
#define ipc_thread_wait(id)            pthread_join(*(id), NULL)
#define ipc_thread_exit()              pthread_exit(NULL)
#if defined(SYLIXOS)
#define ipc_thread_msleep(ms)          Lw_Time_MSleep(ms)
#else
#define ipc_thread_msleep(ms)          usleep((ms) * 1000)
#endif

/* Some OS unsupport this flag */
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT    0
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL    0
#endif

/*
 * Close socket
 */
#define close_socket(s)  close(s)

/*
 * Shutdown socket
 */
#define shutdown_socket(s)  shutdown(s, SHUT_RDWR)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create socket
 */
static inline int create_socket (int f, int t, int p, bool n)
{
#if defined(SYLIXOS) || defined(__linux__)
    int s;

    if (n) {
        t |= SOCK_NONBLOCK;
    }
    s = socket(f, t | SOCK_CLOEXEC, p);
#else
    int s, on = 1;

    s = socket(f, t, p);
    if (n && s >= 0) {
        ioctl(s, FIONBIO, &on);
    }
#endif

    return  (s);
}

/*
 * Socket send timeout
 */
static inline void ipc_socket_sndto (int s, const struct timeval *timeout)
{
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, timeout, sizeof(struct timeval));
}

/*
 * Bind socket to interface
 */
static inline bool ipc_socket_bindif (int s, const char *ifname)
{
#if defined(SO_BINDTODEVICE)
    struct ifreq ifreq;

    if (!ifname || strlen(ifname) >= IF_NAMESIZE) {
        return  (false);
    }

    strcpy(ifreq.ifr_name, ifname);

    if (setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, (const void *)&ifreq, sizeof(struct ifreq))) {
        return  (false);
    } else {
        return  (true);
    }

#else
    return  (false);
#endif
}

/*
 * Create event pair
 */
static inline bool ipc_event_pair_create (int evtfd[2])
{
#if defined(SYLIXOS) || defined(__linux__)
    evtfd[0] = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (evtfd[0] < 0) {
        return  (false);
    }
    evtfd[1] = evtfd[0];

#else
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    evtfd[0] = create_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, true);
    if (evtfd[0] < 0) {
        return  (false);
    }

    evtfd[1] = create_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, true);
    if (evtfd[1] < 0) {
        close_socket(evtfd[0]);
        return  (false);
    }

    bzero(&addr, sizeof(struct sockaddr_in));

#ifdef IPC_HAS_SIN_LEN
    addr.sin_len = addr_len;
#endif

    addr.sin_family      = AF_INET;
    addr.sin_port        = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    bind(evtfd[0], (struct sockaddr *)&addr, addr_len);
    getsockname(evtfd[0], (struct sockaddr *)&addr, &addr_len);
    connect(evtfd[1], (struct sockaddr *)&addr, addr_len);
#endif

    return  (true);
}

/*
 * Close event pair
 */
static inline void ipc_event_pair_close (int evtfd[2])
{
#if defined(SYLIXOS) || defined(__linux__)
    close(evtfd[0]);
#else
    close_socket(evtfd[0]);
    close_socket(evtfd[1]);
#endif

    evtfd[1] = evtfd[0] = -1;
}

/*
 * Signal event pair
 */
static inline void ipc_event_pair_signal (int wfd)
{
#if defined(SYLIXOS) || defined(__linux__)
    eventfd_t  event = 1;
    eventfd_write(wfd, event);
#else
    uint8_t  event = 1;
    send(wfd, &event, 1, MSG_NOSIGNAL);
#endif
}

/*
 * Read event pair
 */
static inline void ipc_event_pair_fetch (int rfd)
{
#if defined(SYLIXOS) || defined(__linux__)
    eventfd_t  event;
    eventfd_read(rfd, &event);
#else
    uint8_t  event;
    recv(rfd, &event, 1, MSG_DONTWAIT);
#endif
}

/*
 * Multiplexing IO Select
 */
static inline int ipc_select (int w, fd_set *rfds, fd_set *wfds, fd_set *efds, const struct timespec *timeout)
{
    return  (pselect(w, rfds, wfds, efds, timeout, NULL));
}

#ifdef __cplusplus
}
#endif

#endif /* IPC_PLATFORM_H */
/*
 * end
 */
