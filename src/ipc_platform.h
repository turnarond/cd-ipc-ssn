/*
 * IPC platform
 */

#ifndef IPC_PLATFORM_H
#define IPC_PLATFORM_H

#include <time.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <net/if.h>
#endif

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

#ifdef _WIN32
#include <windows.h>

typedef CRITICAL_SECTION        ipc_spin_t;
#define ipc_spin_init(spin)     (InitializeCriticalSectionEx(spin, 4000, CRITICAL_SECTION_NO_DEBUG_INFO) ? 0 : - 1)
#define ipc_spin_destroy(spin)  DeleteCriticalSection(spin)
#define ipc_spin_lock(spin)     EnterCriticalSection(spin)
#define ipc_spin_unlock(spin)   LeaveCriticalSection(spin)

typedef CRITICAL_SECTION        ipc_mutex_t;
#define ipc_mutex_init(mtx)     (InitializeCriticalSectionEx(mtx, 8192, CRITICAL_SECTION_NO_DEBUG_INFO) ? 0 : -1)
#define ipc_mutex_destroy(mtx)  DeleteCriticalSection(mtx)
#define ipc_mutex_lock(mtx)     EnterCriticalSection(mtx)
#define ipc_mutex_unlock(mtx)   LeaveCriticalSection(mtx)

#else
#include <pthread.h>
#include <sys/time.h> 

typedef pthread_spinlock_t      ipc_spin_t;
#define IPC_SPIN_INITIALIZER    PTHREAD_SPINLOCK_INITIALIZER
#define ipc_spin_init(spin)     pthread_spin_init(spin, 0)
#define ipc_spin_destroy(spin)  pthread_spin_destroy(spin)
#define ipc_spin_lock(spin)     pthread_spin_lock(spin)
#define ipc_spin_unlock(spin)   pthread_spin_unlock(spin)

typedef pthread_mutex_t         ipc_mutex_t;
#define IPC_MUTEX_INITIALIZER   PTHREAD_MUTEX_INITIALIZER
#define ipc_mutex_init(mtx)     (pthread_mutex_init(mtx, 0) ? -1 : 0)
#define ipc_mutex_destroy(mtx)  (pthread_mutex_destroy(mtx) ? -1 : 0)
#define ipc_mutex_lock(mtx)     pthread_mutex_lock(mtx)
#define ipc_mutex_unlock(mtx)   pthread_mutex_unlock(mtx)
#define ipc_mutex_trylock(mtx)  (pthread_mutex_trylock(mtx) ? -1 : 0)

#endif

/* Multi-threading */
#ifdef _WIN32
#include <windows.h>
typedef HANDLE ipc_thread_t;
#define ipc_thread_create(id, f, a) \
            ((*(id) = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(f), (a), 0, NULL)) != NULL ? 0 : -1)
#define ipc_thread_wait(id)         WaitForSingleObject(*(id), INFINITE)
#define ipc_thread_exit()           ExitThread(0)

#else
typedef pthread_t                      ipc_thread_t;
#define ipc_thread_create(id, f, a)    pthread_create(id, NULL, f, a)
#define ipc_thread_wait(id)            pthread_join(*(id), NULL)
#define ipc_thread_exit()              pthread_exit(NULL)

#endif

#if defined(SYLIXOS)
#define ipc_thread_msleep(ms)           Lw_Time_MSleep(ms)

#elif defined(_WIN32)
#define ipc_thread_msleep(ms)           Sleep(ms)

#else
#define ipc_thread_msleep(ms)           usleep((ms) * 1000)

#endif

/* Some OS unsupport this flag */
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT    0
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL    0
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define close_socket(s)     closesocket(s)
#define shutdown_socket(s)  shutdown(s, SD_BOTH)

#ifndef SHUT_RD
#define SHUT_RD   SD_RECEIVE
#define SHUT_WR   SD_SEND
#define SHUT_RDWR SD_BOTH
#endif

#else
#define close_socket(s)  close(s)
#define shutdown_socket(s)  shutdown(s, SHUT_RDWR)

#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
static inline int ipc_platform_init(void) {
    WSADATA wsaData;
    return (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) ? 0 : -1;
}
static inline void ipc_platform_cleanup(void) {
    WSACleanup();
}
#else
static inline int ipc_platform_init(void) { return 0; }
static inline void ipc_platform_cleanup(void) { }
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
static inline bool ipc_event_pair_create(int evtfd[2])
{
#if defined(SYLIXOS) || defined(__linux__)
    // Linux: use eventfd
    evtfd[0] = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (evtfd[0] < 0) {
        return false;
    }
    evtfd[1] = evtfd[0];  // same fd for read/write
    return true;

#elif !defined(_WIN32)
    // Other Unix: use pipe (POSIX compliant, efficient, no network dependency)
    if (pipe(evtfd) != 0) {
        return false;
    }

    // Set both ends to non-blocking
    int flags = fcntl(evtfd[0], F_GETFL, 0);
    fcntl(evtfd[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(evtfd[1], F_GETFL, 0);
    fcntl(evtfd[1], F_SETFL, flags | O_NONBLOCK);

    return true;

#else
    // Windows: keep UDP loopback as fallback (or consider WSAEvent, but more complex)
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    evtfd[0] = create_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, true);
    if (evtfd[0] < 0) return false;

    evtfd[1] = create_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, true);
    if (evtfd[1] < 0) {
        close_socket(evtfd[0]);
        return false;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(evtfd[0], (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close_socket(evtfd[0]);
        close_socket(evtfd[1]);
        return false;
    }

    getsockname(evtfd[0], (struct sockaddr*)&addr, &addr_len);
    if (connect(evtfd[1], (struct sockaddr*)&addr, addr_len) < 0) {
        close_socket(evtfd[0]);
        close_socket(evtfd[1]);
        return false;
    }

    return true;
#endif
}

static inline void ipc_event_pair_close(int evtfd[2])
{
#if defined(SYLIXOS) || defined(__linux__)
    close_socket(evtfd[0]);
#elif !defined(_WIN32)
    close_socket(evtfd[0]);
    close_socket(evtfd[1]);
#else
    close_socket(evtfd[0]);
    close_socket(evtfd[1]);
#endif
    evtfd[0] = evtfd[1] = -1;
}

static inline void ipc_event_pair_signal(int wfd)
{
#if defined(SYLIXOS) || defined(__linux__)
    eventfd_t val = 1;
    eventfd_write(wfd, val);
#elif !defined(_WIN32)
    uint64_t val = 1;
    write(wfd, &val, sizeof(val));  // write full word to avoid SIGPIPE on partial write
#else
    uint8_t val = 1;
    send(wfd, &val, 1, MSG_NOSIGNAL);
#endif
}

static inline int ipc_event_pair_fetch(int rfd)
{
#if defined(SYLIXOS) || defined(__linux__)
    eventfd_t val;
    eventfd_read(rfd, &val);
#elif !defined(_WIN32)
    uint64_t val;
    return read(rfd, &val, sizeof(val));  // consume entire signal
#else
    uint8_t val;
    return recv(rfd, &val, 1, MSG_DONTWAIT);
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* IPC_PLATFORM_H */

/*
 * end
 */
