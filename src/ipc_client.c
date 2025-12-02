/*
 * IPC client
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "ipc_list.h"
#include "ipc_client.h"
#include "ipc_parser.h"
#include "ipc_platform.h"

/* Client max pending */
#define IPC_CLIENT_MAX_PENDING  0xff
#define IPC_CLIENT_MAX_POFFSET  8

/* Callback function type */
#define IPC_CLIENT_FTYPE_RPC  1
#define IPC_CLIENT_FTYPE_RES  2

/* Client callback union. */
typedef union {
    ipc_client_msg_func_t msg;
    ipc_client_rpc_func_t rpc;
    ipc_client_res_func_t res;
    ipc_client_dat_func_t dat;
} ipc_client_callback_u;

/* Client pending queue */
typedef struct ipc_client_pendq {
    struct ipc_client_pendq *next;
    struct ipc_client_pendq *prev;
    int16_t alive;
    uint16_t seqno;
    uint32_t ftype;
    ipc_client_callback_u callback;
    void *arg;
} ipc_client_pendq_t;

/* Client */
struct ipc_client {
    bool valid;
    bool connected;
    ipc_client_t *next;
    ipc_client_t *prev;
    ipc_client_pendq_t *head;
    ipc_client_pendq_t *tail;
    ipc_client_pendq_t *free;
    ipc_client_pendq_t *pool;
    void *sendbuf;
    void *recvbuf;
    ipc_recv_t recv;
    bool cid_valid;
    uint32_t rpc_pending;
    uint32_t cid;
    uint16_t seqno;
    uint16_t seqno_nq;
    int sock;
    int evtfd[2];
    struct timeval send_timeout;
    ipc_spin_t spin;
    ipc_mutex_t lock;
    ipc_client_msg_func_t onmsg;
    void *marg;
    ipc_client_dat_func_t ondat;
    void *darg;
};


/* Client fast pending buffer */
#define IPC_CLIENT_FAST_PENDING_POOL  8

/* Client pending is in fast pending buffer */
#define IPC_CLIENT_PENDING_IS_IN_POOL(client, pending) \
        ((pending) >= client->pool && \
        (pending) <= client->pool + IPC_CLIENT_FAST_PENDING_POOL - 1)

/* Connect input argument */
struct conn_input_arg {
    ipc_client_t *client;
    int packet_cnt;
    char *info;
    size_t sz_info;
};

/* Default send timeout */
static const struct timeval ipc_client_def_send_timeout = {
    .tv_sec  = (IPC_CLIENT_DEF_SEND_TIMEOUT / 1000),
    .tv_usec = (IPC_CLIENT_DEF_SEND_TIMEOUT % 1000) * 1000
};

/* Clients list lock, header, timer thread */
static ipc_thread_t  ipc_client_timer;
static ipc_client_t *ipc_client_list = NULL;
static ipc_mutex_t   ipc_client_lock = IPC_MUTEX_INITIALIZER;

/*
* IPC timer thread
*/
static void *ipc_client_timer_handle (void *arg)
{
    bool emit;
    ipc_client_t *client;
    ipc_client_pendq_t *pendq, *tmp;

    (void)arg;

    do {
        ipc_thread_msleep(IPC_CLIENT_TIMER_PERIOD);

        ipc_mutex_lock(&ipc_client_lock);

        if (!ipc_client_list) {
            ipc_mutex_unlock(&ipc_client_lock);
            break;
        }

        LIST_FOREACH(client, ipc_client_list) {
            emit = false;

            ipc_mutex_lock(&client->lock);

            LIST_FOREACH_SAFE(pendq, tmp, client->head) {
                if (pendq->alive > IPC_CLIENT_TIMER_PERIOD) {
                    pendq->alive -= IPC_CLIENT_TIMER_PERIOD;
                } else {
                    pendq->alive = 0;
                    emit = true;
                    break; // only deal with one timeout pend of a client;
                }
            }

            ipc_mutex_unlock(&client->lock);
            if (emit) {
                ipc_event_pair_signal(client->evtfd[1]);
            }
        }

        ipc_mutex_unlock(&ipc_client_lock);

    } while (true);

    ipc_thread_exit();

    return  (NULL);
}

/*
* Pendq free
*/
static void ipc_client_pendq_free (ipc_client_t *client, ipc_client_pendq_t *pendq)
{
    if (IPC_CLIENT_PENDING_IS_IN_POOL(client, pendq)) {
        ipc_mutex_lock(&client->lock);
        INSERT_TO_HEADER(pendq, client->free);
        ipc_mutex_unlock(&client->lock);

    } else {
        free(pendq);
    }
}

/*
* Create IPC client
* Warning: This function must be mutually exclusive with the ipc_client_close() call
*/
ipc_client_t *ipc_client_create (ipc_client_msg_func_t onmsg, void *arg)
{
    int i, err = 0;
    ipc_client_t *client;

    client = (ipc_client_t *)malloc(sizeof(ipc_client_t) + 
                                    (sizeof(ipc_client_pendq_t) * IPC_CLIENT_FAST_PENDING_POOL));
    if (!client) {
        return  (NULL);
    }

    bzero(client, sizeof(ipc_client_t));

    client->sock   = -1;

    if (!ipc_event_pair_create(client->evtfd)) {
        goto    error;
    }

    client->sendbuf = malloc(IPC_MAX_PACKET_LENGTH * 2);
    if (!client->sendbuf) {
        err = 1;
        goto    error;
    }

    client->pool = (ipc_client_pendq_t *)(client + 1);
    for (i = 0; i < IPC_CLIENT_FAST_PENDING_POOL; i++) {
        INSERT_TO_HEADER(&client->pool[i], client->free);
    }

    ipc_spin_init(&client->spin);

    if (ipc_mutex_init(&client->lock)) {
        err = 2;
        goto    error;
    }

    ipc_parser_init_recv(&client->recv);
    client->recvbuf      = (uint8_t *)client->sendbuf + IPC_MAX_PACKET_LENGTH;
    client->onmsg        = onmsg;
    client->marg         = arg;
    client->send_timeout = ipc_client_def_send_timeout;
    client->valid        = true;

    ipc_mutex_lock(&ipc_client_lock);

    if (ipc_client_list == NULL) {
        if (ipc_thread_create(&ipc_client_timer, ipc_client_timer_handle, NULL)) {
            ipc_mutex_unlock(&ipc_client_lock);
            err = 3;
            goto    error;
        }
    }

    INSERT_TO_HEADER(client, ipc_client_list);

    ipc_mutex_unlock(&ipc_client_lock);

    return  (client);

error:
    if (err > 2) {
        ipc_mutex_destroy(&client->lock);
    }
    if (err > 1) {
        free(client->sendbuf);
    }
    if (err > 0) {
        ipc_event_pair_close(client->evtfd);
    }

    free(client);
    return  (NULL);
}

/*
* Close IPC client
* Warning: This function must be mutually exclusive with the ipc_client_create() call
*/
void ipc_client_close (ipc_client_t *client)
{
    ipc_client_pendq_t *pendq, *temp;

    if (!client->valid) {
        return;
    }

    /* * Set client to invalid state, so that no new operations can be performed.
     * This is necessary to ensure that the client is not used after closing.
     */
    client->valid     = false;

    ipc_mutex_lock(&ipc_client_lock);

    DELETE_FROM_LIST(client, ipc_client_list);
    bool should_wait = (ipc_client_list == NULL);

    ipc_mutex_unlock(&ipc_client_lock);

    ipc_mutex_lock(&client->lock);

    client->connected = false;
    ipc_memory_barrier();

    if (client->sock >= 0) {
        close_socket(client->sock);
        client->sock = -1;
    }

    ipc_event_pair_close(client->evtfd);
    free(client->sendbuf);

    LIST_FOREACH_SAFE(pendq, temp, client->head) {
        if (pendq->ftype == IPC_CLIENT_FTYPE_RPC &&
            pendq->callback.rpc) {
                pendq->callback.rpc(pendq->arg, client, NULL, NULL);
            }
            DELETE_FROM_FIFO(pendq, client->head, client->tail);
            ipc_client_pendq_free(client, pendq);
        }
        
    ipc_mutex_unlock(&client->lock);

    if (should_wait) {
        ipc_thread_wait(&ipc_client_timer);
    }

    ipc_mutex_destroy(&client->lock);
    ipc_spin_destroy(&client->spin);
    free(client);
}

/*
* Client send packet
*/
static bool ipc_client_send (ipc_client_t *client, size_t len)
{
    uint8_t *buffer = (uint8_t *)client->sendbuf;
    ssize_t num, total = 0;

    do {
        num = send(client->sock, &buffer[total], len - total, MSG_NOSIGNAL);
        if (num > 0) {
            total += num;
        } else {
            shutdown_socket(client->sock);
            break;
        }
    } while (total < len);

    return  (total == len);
}

static bool ipc_client_sendmsg (ipc_client_t *client, ipc_header_t *ipc_hdr, 
    const ipc_url_t *url, const ipc_payload_t *payload)
{
    ssize_t len;
    if (url->url_len > IPC_MAX_DATA_LENGTH) {
        return  (false);
    }

    ipc_hdr->url_len = htons((uint16_t)url->url_len);

    if (payload->data_len) {
        ipc_hdr->data_len = htonl(payload->data_len);
    } else {
        ipc_hdr->data_len = 0;
    }

    uint64_t total = (uint64_t)IPC_HDR_LENGTH + ipc_hdr->url_len + ipc_hdr->data_len; // 防溢出

    if (total > IPC_MAX_PACKET_LENGTH || total < IPC_HDR_LENGTH) {
        return false;
    }

    struct iovec iov[3] = {
        {
            .iov_base = (void*)ipc_hdr,
            .iov_len = sizeof(ipc_header_t)
        },
        {
            .iov_base = url->url,
            .iov_len = url->url_len
        }
    };

    struct msghdr msg = {0};
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    if (payload) {
        if (payload->data) {
            iov[msg.msg_iovlen].iov_base = payload->data;
            iov[msg.msg_iovlen].iov_len = payload->data_len;
            msg.msg_iovlen++;
        }
    }

    len = sendmsg(client->sock, &msg, 0); 

    if (len < 0) {
        shutdown_socket(client->sock);
        return false;
    }
    return true;
}

/*
* All RPC callback timeout.
*/
static void ipc_client_timeout_all (ipc_client_t *client)
{
    ipc_client_pendq_t *pendq, *to_head, *temp;

    ipc_mutex_lock(&client->lock);

    to_head = client->head;
    client->head = client->tail = NULL;
    client->rpc_pending = 0;

    ipc_mutex_unlock(&client->lock);

    if (to_head) {
        LIST_FOREACH_SAFE(pendq, temp, to_head) {
            if (pendq->ftype == IPC_CLIENT_FTYPE_RPC &&
                pendq->callback.rpc) {
                pendq->callback.rpc(pendq->arg, client, NULL, NULL);
            }
            DELETE_FROM_LIST(pendq, to_head);
            ipc_client_pendq_free(client, pendq);
        }
    }
}

/*
* Connect input callback
*/
static bool ipc_client_conn_input (void *varg, ipc_header_t *ipc_hdr)
{
    struct conn_input_arg *arg = (struct conn_input_arg *)varg;
    ipc_payload_t payload;
    size_t length;
    uint8_t *nid;

    if (ipc_hdr->type != IPC_TYPE_SERVINFO) {
        return  (true);
    }
    if (ipc_hdr->status) {
        return  (true);
    }

    if (!ipc_parser_get_payload(ipc_hdr, &payload) || !payload.data_len) {
        return  (true);
    }

    arg->packet_cnt++;

    if (payload.data_len >= sizeof(uint32_t)) {
        nid = (uint8_t *)payload.data;
        arg->client->cid = ((uint32_t)nid[0] << 24) + ((uint32_t)nid[1] << 16)
                        + ((uint32_t)nid[2] << 8)  +  (uint32_t)nid[3];
        arg->client->cid_valid = true;
    }

    return  (true);
}

/*
* Connect to server (Synchronous)
*/
bool ipc_client_connect (ipc_client_t *client, const char* ipc_path,
                        const struct timespec *timeout)
{
    int errcode, ret, on = 1, off = 0;
    bool suc;
    char *opt;
    fd_set fds;
    size_t len = 0;
    ssize_t num;
    ipc_header_t *ipc_hdr;
    ipc_payload_t payload;
    struct sockaddr_un server;
    struct conn_input_arg arg;

    if (!client || !client->valid) {
        return  (false);
    }

    client->connected = false;
    ipc_memory_barrier();

    if (client->sock >= 0) {
        close_socket(client->sock);
        client->sock = -1;
    }

    ipc_client_timeout_all(client);

    client->sock = create_socket(AF_UNIX, SOCK_STREAM, 0, true);
    if (client->sock < 0) {
        return  (false);
    }

    memset(&server, 0, sizeof(server));
    strcpy(server.sun_path, ipc_path);
    server.sun_family = AF_UNIX;
    ipc_hdr = ipc_parser_init_header(client->sendbuf, IPC_TYPE_SERVINFO, 0, 0);

    ret = connect(client->sock, (struct sockaddr*)&server, sizeof(struct sockaddr_un));
    if (ret) {
        errcode = errno;
        if (errcode != EINPROGRESS && errcode != EWOULDBLOCK) {
            return  (false);
        }
    }

    ioctl(client->sock, FIONBIO, &off);

    FD_ZERO(&fds);
    FD_SET(client->sock, &fds);

    ret = pselect(client->sock + 1, NULL, &fds, NULL, timeout, NULL);
    if (ret <= 0 || !FD_ISSET(client->sock, &fds)) {
        return  (false);
    }

    if (!ipc_client_send(client, len)) {
        return  (false);
    }

    ret = pselect(client->sock + 1, &fds, NULL, NULL, timeout, NULL);
    if (ret <= 0 || !FD_ISSET(client->sock, &fds)) {
        return  (false);
    }

    num = recv(client->sock, client->recvbuf, IPC_MAX_PACKET_LENGTH, 0);

    if (num > 0) {
        arg.client     = client;
        arg.packet_cnt = 0;
        if (!ipc_parser_input(&client->recv, client->recvbuf,
                            num, ipc_client_conn_input, &arg)) {
            num = -1;
        }
    }
    if (num <= 0 || !arg.packet_cnt) {
        return  (false);
    }

    client->connected = true;
    ipc_memory_barrier();

    /* Set send timeout */
    ipc_socket_sndto(client->sock, &client->send_timeout);

    return  (true);
}

/* Disconnect from server
* After disconnect, the `ipc_client_connect` function can be called again */
bool ipc_client_disconnect (ipc_client_t *client)
{
    if (!client || !client->valid || !client->connected) {
        return  (false);
    }

    ipc_mutex_lock(&client->lock);

    client->connected = false;
    ipc_memory_barrier();

    if (client->sock >= 0) {
        shutdown_socket(client->sock);
    }

    ipc_mutex_unlock(&client->lock);

    ipc_client_timeout_all(client);

    return  (true);
}

/*
* IPC client is connect with server
*/
bool ipc_client_is_connect (ipc_client_t *client)
{
    return  (client ? (client->valid && client->connected) : false);
}

/*
* IPC client send timeout
*/
bool ipc_client_send_timeout (ipc_client_t *client, const struct timespec *timeout)
{
    if (!client || !client->valid) {
        return  (false);
    }

    if (timeout) {
        client->send_timeout.tv_sec  = timeout->tv_sec;
        client->send_timeout.tv_usec = timeout->tv_nsec / 1000;
    } else {
        client->send_timeout = ipc_client_def_send_timeout;
    }

    if (client->connected && client->sock >= 0) {
        ipc_socket_sndto(client->sock, &client->send_timeout);
    }

    return  (true);
}

/*
* IPC client checking event
*/
int ipc_client_fds (ipc_client_t *client, fd_set *rfds)
{
    int max_fd;

    if (!client || !client->valid) {
        return  (-1);
    }

    if (!client->connected) {
        FD_SET(client->evtfd[0], rfds);
        return  (client->evtfd[0]);
    }

    FD_SET(client->sock, rfds);
    max_fd = client->sock;

    FD_SET(client->evtfd[0], rfds);
    if (max_fd < client->evtfd[0]) {
        max_fd = client->evtfd[0];
    }

    return  (max_fd);
}

/*
* Client input callback
*/
static bool ipc_client_input (void *varg, ipc_header_t *ipc_hdr)
{
    ipc_client_t *client = (ipc_client_t *)varg;
    ipc_client_pendq_t *pendq, *temp;
    uint16_t seqno;
    ipc_url_t url;
    ipc_payload_t payload;

    if (ipc_hdr->type == IPC_TYPE_PUBLISH || ipc_hdr->type == IPC_TYPE_DATAGRAM) {
        ipc_parser_get_url(ipc_hdr, &url);
        ipc_parser_get_payload(ipc_hdr, &payload);

        if (ipc_hdr->type == IPC_TYPE_PUBLISH) {
            if (client->onmsg) {
                client->onmsg(client->marg, client, &url, &payload);
            }
        } else if (client->ondat) {
             client->ondat(client->darg, client, &url, &payload);
         }
        goto  out;

    } else if (ipc_hdr->type == IPC_FLAG_REPLY) {
        goto  out;
    }

    seqno = ipc_parser_get_seqno(ipc_hdr);

    ipc_mutex_lock(&client->lock);

    LIST_FOREACH_SAFE(pendq, temp, client->head) {
        if (pendq->seqno == seqno) {
            if (pendq->ftype == IPC_CLIENT_FTYPE_RPC) {
                client->rpc_pending--;
            }
            DELETE_FROM_FIFO(pendq, client->head, client->tail);
            break;
        }
    }

    ipc_mutex_unlock(&client->lock);

    if (pendq) {
        switch (ipc_hdr->type) {

        case IPC_TYPE_SUBSCRIBE:
        case IPC_TYPE_UNSUBSCRIBE:
        case IPC_TYPE_PINGECHO:
            if (pendq->ftype == IPC_CLIENT_FTYPE_RES) {
                if (pendq->callback.res) {
                    pendq->callback.res(pendq->arg, client, ipc_hdr->status == 0);
                }
            }
            break;

        case IPC_TYPE_RPC:
            if (pendq->ftype == IPC_CLIENT_FTYPE_RPC) {
                if (pendq->callback.rpc) {
                    ipc_parser_get_payload(ipc_hdr, &payload);
                    pendq->callback.rpc(pendq->arg, client, ipc_hdr, &payload);
                }
            }
            break;

        default:
            break;
        }

        ipc_client_pendq_free(client, pendq);
    }

out:
    return  (client->valid);
}

/*
* IPC client input event
*/
static bool ipc_client_process_events (ipc_client_t *client, const fd_set *rfds)
{
    bool pkt_e;
    ssize_t num;
    ipc_header_t *ipc_hdr;
    ipc_client_pendq_t *pendq, *to_head, *to_tail, *temp;

    if (!client || !client->valid) {
        return  (false);
    }
    if (client->connected) {
        if (FD_ISSET(client->sock, rfds)) {
            pkt_e = false;
            num = recv(client->sock, client->recvbuf, IPC_MAX_PACKET_LENGTH, MSG_DONTWAIT);
            if (num > 0) {
                // TODO: deal recv msg;
                if (!ipc_parser_input(&client->recv, client->recvbuf,
                                    num, ipc_client_input, client)) {
                    pkt_e = true;
                }
            }

            if (pkt_e || num == 0 || (num < 0 && errno != EWOULDBLOCK)) {
                client->connected = false;
                ipc_memory_barrier();

                ipc_client_timeout_all(client);
                return  (false);
            }
        }
    }

    if (FD_ISSET(client->evtfd[0], rfds)) {
        to_head = to_tail = NULL;

        ipc_event_pair_fetch(client->evtfd[0]);

        ipc_mutex_lock(&client->lock);

        LIST_FOREACH_SAFE(pendq, temp, client->head) {
            if (pendq->alive <= 0) {
                if (pendq->ftype == IPC_CLIENT_FTYPE_RPC) {
                    client->rpc_pending--;
                }
                DELETE_FROM_FIFO(pendq, client->head, client->tail);
                INSERT_TO_FIFO(pendq, to_head, to_tail);
            }
        }

        ipc_mutex_unlock(&client->lock);

        if (to_head) {
            LIST_FOREACH_SAFE(pendq, temp, to_head) {
                if (pendq->callback.dat) {
                    pendq->callback.dat(pendq->arg, client, NULL, NULL);
                }
                DELETE_FROM_FIFO(pendq, to_head, to_tail);
                ipc_client_pendq_free(client, pendq);
            }
        }
    }

    return  (true);
}

/*
* Prepare a non-queued seqno
*/
static uint16_t ipc_client_prepare_seqno (ipc_client_t *client)
{
    uint16_t seqno;

    ipc_spin_lock(&client->spin);

    if (client->seqno_nq == 0) {
        seqno = 1;
        client->seqno_nq = 2;
    } else {
        seqno = client->seqno_nq;
        client->seqno_nq++;
    }

    ipc_spin_unlock(&client->spin);

    return  (seqno << IPC_CLIENT_MAX_POFFSET);
}

/*
* Prepare a pendq
*/
static ipc_client_pendq_t *ipc_client_prepare_pendq (ipc_client_t *client, bool fast, void *arg, 
                                                    uint32_t ftype, const struct timespec *timeout)
{
    uint16_t i, seqno;
    ipc_client_pendq_t *pendq = NULL, *queued;

    if (fast) {
        if (client->free) {
            ipc_mutex_lock(&client->lock);
            pendq = client->free;
            DELETE_FROM_LIST(pendq, client->free);
            ipc_mutex_unlock(&client->lock);
        }
    }

    if (!pendq) {
        pendq = (ipc_client_pendq_t *)malloc(sizeof(ipc_client_pendq_t));
        if (!pendq) {
            return  (NULL);
        }
    }
    
    ipc_mutex_lock(&client->lock);
    for (i = 0; i < IPC_CLIENT_MAX_PENDING; i++) {
        seqno = client->seqno;
        client->seqno = (seqno + 1) & IPC_CLIENT_MAX_PENDING;

        LIST_FOREACH(queued, client->head) {
            if (queued->seqno == seqno) {
                break;
            }
        }

        if (!queued) {
            pendq->seqno = seqno;
            break;
        }
    }
    ipc_mutex_unlock(&client->lock);

    if (i >= IPC_CLIENT_MAX_PENDING) {
        ipc_client_pendq_free(client, pendq);
        return  (NULL);
    }

    pendq->arg = arg;
    pendq->ftype = ftype;

    if (timeout) {
        pendq->alive = (timeout->tv_sec * 1000) + (timeout->tv_nsec / 1000000);
    } else {
        pendq->alive = IPC_CLIENT_DEF_TIMEOUT;
    }

    return  (pendq);
}

/*
* Request
*/
static bool ipc_client_request (ipc_client_t *client, uint8_t type, 
                                const ipc_url_t *url, const ipc_payload_t *payload,
                                ipc_client_res_func_t callback, void *arg, const struct timespec *timeout)
{
    size_t len;
    uint16_t seqno;
    ipc_header_t *ipc_hdr;
    ipc_client_pendq_t *pendq;

    if (!client || !client->valid || !client->connected) {
        return  (false);
    }

    if (callback) {
        pendq = ipc_client_prepare_pendq(client, type == IPC_TYPE_PINGECHO,
                                        arg, IPC_CLIENT_FTYPE_RES, timeout);
        if (!pendq) {
            return  (false);
        }
        pendq->callback.res = callback;
        seqno = pendq->seqno;
    } else {
        pendq = NULL;
        seqno = ipc_client_prepare_seqno(client);
    }

    ipc_mutex_lock(&client->lock);

    ipc_hdr = ipc_parser_init_header(client->sendbuf, type, 0, seqno);

    if (!ipc_client_sendmsg(client, ipc_hdr, url, payload)) {
        goto    error;
    }

    if (pendq) {
        INSERT_TO_FIFO(pendq, client->head, client->tail);
    }

    ipc_mutex_unlock(&client->lock);

    return  (true);

error:
    ipc_mutex_unlock(&client->lock);

    if (pendq) {
        ipc_client_pendq_free(client, pendq);
    }

    return  (false);
}

/*
* Subscribe URL
*/
bool ipc_client_subscribe (ipc_client_t *client, const ipc_url_t *url,
                            ipc_client_res_func_t callback, void *arg, const struct timespec *timeout)
{
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        return  (false);
    }

    return  (ipc_client_request(client, IPC_TYPE_SUBSCRIBE, url, NULL, callback, arg, timeout));
}

/*
* Unsubscribe URL
*/
bool ipc_client_unsubscribe (ipc_client_t *client, const ipc_url_t *url,
                            ipc_client_res_func_t callback, void *arg, const struct timespec *timeout)
{
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        return  (false);
    }

    return  (ipc_client_request(client, IPC_TYPE_UNSUBSCRIBE, url, NULL, callback, arg, timeout));
}

/*
* RPC call with externed arguments.
* This functions are special interfaces used by client robots, and users are prohibited from using them!
*/
bool ipc_client_call_ex (ipc_client_t *client, const ipc_url_t *url, const ipc_payload_t *payload,
                        ipc_client_rpc_func_t callback, void *arg, const struct timespec *timeout, void *arg_ex)
{
    size_t len;
    uint8_t flag;
    uint16_t seqno;
    ipc_header_t *ipc_hdr;
    ipc_client_pendq_t *pendq;

    if (!client || !client->valid || !client->connected) {
        return  (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        return  (false);
    }

    if (callback) {
        pendq = ipc_client_prepare_pendq(client, false, arg, IPC_CLIENT_FTYPE_RPC, timeout);
        if (!pendq) {
            return  (false);
        }
        seqno = pendq->seqno;
        pendq->callback.rpc = callback;
    } else {
        pendq = NULL;
        seqno = ipc_client_prepare_seqno(client);
    }

    ipc_mutex_lock(&client->lock);

    ipc_hdr = ipc_parser_init_header(client->sendbuf, IPC_TYPE_RPC, 0, seqno);

    if (!ipc_client_sendmsg(client, ipc_hdr, url, payload)) {
        goto    error;
    }

    if (pendq) {
        INSERT_TO_FIFO(pendq, client->head, client->tail);
        client->rpc_pending++;
    }

    ipc_mutex_unlock(&client->lock);

    return  (true);

error:
    ipc_mutex_unlock(&client->lock);

    if (pendq) {
        ipc_client_pendq_free(client, pendq);
    }

    return  (false);
}

/*
* RPC call
*/
bool ipc_client_call (ipc_client_t *client, const ipc_url_t *url, const ipc_payload_t *payload,
                    ipc_client_rpc_func_t callback, void *arg, const struct timespec *timeout)
{
    return  (ipc_client_call_ex(client, url, payload, callback, arg, timeout, NULL));
}

/*
* Send datagram to server
*/
bool ipc_client_datagram (ipc_client_t *client, const ipc_url_t *url, const ipc_payload_t *payload)
{
    bool ret;
    size_t len;
    ipc_header_t *ipc_hdr;

    if (!client || !client->valid || !client->connected) {
        return  (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        return  (false);
    }
    if (!payload) {
        return  (false);
    }

    ipc_mutex_lock(&client->lock);

    ipc_hdr = ipc_parser_init_header(client->sendbuf, IPC_TYPE_DATAGRAM, 0, 0);

    ret = ipc_client_sendmsg(client, ipc_hdr, url, payload);

    ipc_mutex_unlock(&client->lock);

    return  (ret);

error:
    ipc_mutex_unlock(&client->lock);

    return  (false);
}

void ipc_client_set_on_datagram (ipc_client_t *client, ipc_client_dat_func_t callback, void *arg)
{
    if (client) {
        client->ondat = callback;
        client->darg  = arg;
    }
}

int ipc_client_poll(ipc_client_t *client, uint64_t timeout_ms)
{
    int max_fd, cnt;
    fd_set fds;
    sigset_t empty_mask;
    struct timespec timeout = { timeout_ms / 1000, timeout_ms % 1000 };

    FD_ZERO(&fds);
    max_fd = ipc_client_fds(client, &fds);

    sigemptyset(&empty_mask);

    // 阻塞空信号集，可以传递并中断所有信号
    cnt = pselect(max_fd + 1, &fds, NULL, NULL, &timeout, &empty_mask);
    if (cnt > 0) {
        if (!ipc_client_process_events(client, &fds)) {
            ipc_client_close(client);
            fprintf(stderr, "Connection lost!\n");
        }
        return 0;
    }
    return cnt;
}

/*
* end
*/
