/*
 * IPC client
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "ipc_list.h"
#include "ipc_client.h"
#include "ipc_global.h"
#include "ipc_protocol.h"
#include "util/ipc_log.h"

/* Client max pending */
#define IPC_CLIENT_MAX_PENDING  0xff
#define IPC_CLIENT_MAX_POFFSET  8

/* Callback function type */
#define IPC_CLIENT_FTYPE_RPC  1
#define IPC_CLIENT_FTYPE_RES  2

/* Client callback union. */
typedef union {
    ipc_client_sub_func_t sub;
    ipc_client_rpc_func_t rpc;
    ipc_client_res_func_t res;
    ipc_client_msg_func_t msg;
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
    ipc_stream_ctx_t recv;
    bool cid_valid;
    uint32_t rpc_pending;
    uint32_t cid;
    uint16_t seqno;
    uint16_t seqno_nq;
    int sock;
    ipc_event_pair_t *evtfd;
    int send_timeout;
    ipc_spinlock_t *spin;
    ipc_mutex_t *lock;
    ipc_client_sub_func_t onsub;
    void *sub_arg;
    ipc_client_msg_func_t onmsg;
    void *msg_arg;
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

/*
* IPC client thread
*/
void *ipc_client_timer_handle (void *arg)
{
    bool emit;
    ipc_client_t *client;
    ipc_client_pendq_t *pendq, *tmp;

    (void)arg;

    do {
        ipc_thread_msleep(IPC_TIMER_PERIOD);

        ipc_mutex_lock(g_ipc_client_lock);

        if (!g_ipc_client_list) {
            ipc_mutex_unlock(g_ipc_client_lock);
            break;
        }

        LIST_FOREACH(client, g_ipc_client_list) {
            emit = false;

            ipc_mutex_lock(client->lock);

            LIST_FOREACH_SAFE(pendq, tmp, client->head) {
                if (pendq->alive > IPC_TIMER_PERIOD) {
                    pendq->alive -= IPC_TIMER_PERIOD;
                } else {
                    pendq->alive = 0;
                    emit = true;
                    LOG_INFO("pend %d of client %d emit", client->cid, pendq->seqno);
                    break; // only deal with one timeout pend of a client;
                }
            }

            ipc_mutex_unlock(client->lock);
            if (emit) {
                ipc_event_pair_signal(client->evtfd);
            }
        }

        ipc_mutex_unlock(g_ipc_client_lock);

    } while (true);

    ipc_thread_exit();

    return (NULL);
}

/*
 * Pendq free
 */
static void ipc_client_pendq_free (ipc_client_t *client, ipc_client_pendq_t *pendq)
{
    if (IPC_CLIENT_PENDING_IS_IN_POOL(client, pendq)) {
        ipc_mutex_lock(client->lock);
        INSERT_TO_HEADER(pendq, client->free);
        ipc_mutex_unlock(client->lock);

    } else {
        free(pendq);
    }
}

/*
* Create IPC client
* Warning: This function must be mutually exclusive with the ipc_client_close() call
*/
ipc_client_t *ipc_client_create (ipc_client_sub_func_t onmsg, void *arg)
{
    int i, err = 0;
    ipc_client_t *client;

    client = (ipc_client_t *)malloc(sizeof(ipc_client_t) + 
                                    (sizeof(ipc_client_pendq_t) * IPC_CLIENT_FAST_PENDING_POOL));
    if (!client) {
        LOG_ERROR("ipc client create: malloc errno %d", errno);
        return (NULL);
    }

    bzero(client, sizeof(ipc_client_t));

    client->sock   = -1;

    if (ipc_event_pair_create(&client->evtfd) != 0) {
        LOG_ERROR("ipc client create: event pair create faield, errno %d", errno);
        goto error;
    }

    client->sendbuf = malloc(IPC_MAX_PACKET_SIZE * 2);
    if (!client->sendbuf) {
        err = 1;
        LOG_ERROR("ipc client create: sendbuf malloc, errno %d", errno);
        goto error;
    }

    client->pool = (ipc_client_pendq_t *)(client + 1);
    for (i = 0; i < IPC_CLIENT_FAST_PENDING_POOL; i++) {
        INSERT_TO_HEADER(&client->pool[i], client->free);
    }

    ipc_spinlock_init(&client->spin);

    if (ipc_mutex_init(&client->lock)) {
        err = 2;
        LOG_ERROR("ipc client create: sendbuf malloc, errno %d", errno);
        goto error;
    }

    ipc_stream_init(&client->recv);
    client->recvbuf      = (uint8_t *)client->sendbuf + IPC_MAX_PACKET_SIZE;
    client->onsub        = onmsg;
    client->sub_arg      = arg;
    client->send_timeout = IPC_DEF_SEND_TIMEOUT;
    client->valid        = true;

    ipc_mutex_lock(g_ipc_client_lock);

    INSERT_TO_HEADER(client, g_ipc_client_list);

    ipc_mutex_unlock(g_ipc_client_lock);

    LOG_DEBUG("ipc client create success.");
    return (client);

error:
    if (err > 2) {
        ipc_mutex_destroy(client->lock);
    }
    if (err > 1) {
        free(client->sendbuf);
    }
    if (err > 0) {
        ipc_event_pair_destroy(client->evtfd);
    }

    free(client);
    return (NULL);
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

    ipc_mutex_lock(g_ipc_client_lock);

    DELETE_FROM_LIST(client, g_ipc_client_list);

    ipc_mutex_unlock(g_ipc_client_lock);

    ipc_mutex_lock(client->lock);

    client->connected = false;
    ipc_memory_barrier();

    if (client->sock >= 0) {
        ipc_socket_close(client->sock);
        client->sock = -1;
    }

    ipc_event_pair_destroy(client->evtfd);
    free(client->sendbuf);

    LIST_FOREACH_SAFE(pendq, temp, client->head) {
        if (pendq->ftype == IPC_CLIENT_FTYPE_RPC &&
            pendq->callback.rpc) {
                pendq->callback.rpc(client, NULL, NULL, pendq->arg);
            }
            DELETE_FROM_FIFO(pendq, client->head, client->tail);
            ipc_client_pendq_free(client, pendq);
        }
        
    ipc_mutex_unlock(client->lock);

    ipc_mutex_destroy(client->lock);
    ipc_spinlock_destroy(client->spin);
    free(client);
    LOG_DEBUG("ip client close success.");
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
            ipc_socket_shutdown(client->sock);
            break;
        }
    } while (total < len);

    return (total == len);
}

static bool ipc_client_sendmsg (ipc_client_t *client, ipc_header_t *ipc_hdr, 
    const ipc_url_ref_t *url, const ipc_payload_ref_t *payload)
{
    ssize_t len;
    uint64_t total = (uint64_t)IPC_HEADER_SIZE;

    if (url) {
        total += url->url_len;
        ipc_hdr->url_len = htons((uint16_t)url->url_len);
    }

    if (payload) {
        total += payload->length;
        ipc_hdr->data_len =  htonl(payload->length);
    } 

    if (total > IPC_MAX_PACKET_SIZE || total < IPC_HEADER_SIZE) {
        LOG_ERROR("ipc client sendmsg failed: length %lu invalid", total);
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
            iov[msg.msg_iovlen].iov_len = payload->length;
            msg.msg_iovlen++;
        }
    }

    len = sendmsg(client->sock, &msg, 0); 

    if (len < 0) {
        ipc_socket_shutdown(client->sock);
        LOG_ERROR("ipc client sendmsg faield, errno %d", errno);
        return false;
    }
    LOG_DEBUG("ipc client sendmsg success, length is %lu", len);
    return true;
}

/*
* All RPC callback timeout.
*/
static void ipc_client_timeout_all (ipc_client_t *client)
{
    ipc_client_pendq_t *pendq, *to_head, *temp;

    ipc_mutex_lock(client->lock);

    to_head = client->head;
    client->head = client->tail = NULL;
    client->rpc_pending = 0;

    ipc_mutex_unlock(client->lock);

    if (to_head) {
        LIST_FOREACH_SAFE(pendq, temp, to_head) {
            if (pendq->ftype == IPC_CLIENT_FTYPE_RPC &&
                pendq->callback.rpc) {
                pendq->callback.rpc(client, NULL, NULL, pendq->arg);
            }
            DELETE_FROM_LIST(pendq, to_head);
            ipc_client_pendq_free(client, pendq);
        }
    }
}

/*
* Connect input callback
*/
static bool ipc_client_conn_input (ipc_header_t *ipc_hdr, void *varg)
{
    struct conn_input_arg *arg = (struct conn_input_arg *)varg;
    ipc_payload_ref_t payload;
    size_t length;
    uint8_t *nid;

    if (ipc_hdr->msg_type != IPC_MSG_TYPE_SERVICE_INFO) {
        return (true);
    }
    if (ipc_hdr->status) {
        return (true);
    }

    if (!ipc_get_payload(ipc_hdr, &payload) || !payload.length) {
        return (true);
    }

    arg->packet_cnt++;

    if (payload.length >= sizeof(uint32_t)) {
        nid = (uint8_t *)payload.data;
        arg->client->cid = ((uint32_t)nid[0] << 24) + ((uint32_t)nid[1] << 16)
                        + ((uint32_t)nid[2] << 8)  +  (uint32_t)nid[3];
        arg->client->cid_valid = true;
    }

    return (true);
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
    ipc_payload_ref_t payload;
    struct sockaddr_un server;
    struct conn_input_arg arg;

    if (!client || !client->valid) {
        LOG_ERROR("ipc client connect failed: invalid client handle.");
        return (false);
    }

    client->connected = false;
    ipc_memory_barrier();

    if (client->sock >= 0) {
        ipc_socket_close(client->sock);
        client->sock = -1;
    }

    ipc_client_timeout_all(client);

    client->sock = ipc_socket_create(AF_UNIX, SOCK_STREAM, 0, true);
    if (client->sock < 0) {
        LOG_ERROR("ipc client connect faield: create socket failed, errno %d.", errno);
        return (false);
    }

    memset(&server, 0, sizeof(server));
    strcpy(server.sun_path, ipc_path);
    server.sun_family = AF_UNIX;
    ipc_hdr = ipc_create_header(client->sendbuf, IPC_MSG_TYPE_SERVICE_INFO, 0, 0);

    ret = connect(client->sock, (struct sockaddr*)&server, sizeof(struct sockaddr_un));
    if (ret) {
        errcode = errno;
        if (errcode != EINPROGRESS && errcode != EWOULDBLOCK) {
            LOG_ERROR("ipc client connect failed: connect failed, errno %d.", errno);
            return (false);
        }
    }

    FD_ZERO(&fds);
    FD_SET(client->sock, &fds);

    ret = pselect(client->sock + 1, NULL, &fds, NULL, timeout, NULL);
    if (ret <= 0 || !FD_ISSET(client->sock, &fds)) {
        LOG_ERROR("ipc client connect failed: pselect failed, errno %d.", errno);
        return (false);
    }

    if (!ipc_client_send(client, sizeof(ipc_header_t))) {
        LOG_ERROR("ipc client connect failed: send failed, errno %d.", errno);
        return (false);
    }

    ret = pselect(client->sock + 1, &fds, NULL, NULL, timeout, NULL);
    if (ret <= 0 || !FD_ISSET(client->sock, &fds)) {
        LOG_ERROR("ipc client connect failed: pselect failed, errno %d.", errno);
        return (false);
    }

    num = recv(client->sock, client->recvbuf, IPC_MAX_PACKET_SIZE, 0);

    if (num > 0) {
        arg.client     = client;
        arg.packet_cnt = 0;
        if (!ipc_stream_feed(&client->recv, client->recvbuf,
                            num, ipc_client_conn_input, &arg)) {
            num = -1;
        }
    }
    if (num <= 0 || !arg.packet_cnt) {
        LOG_ERROR("ipc client connect failed: recv failed, errno %d.", errno);
        return (false);
    }

    client->connected = true;
    ipc_memory_barrier();

    /* Set send timeout */
    ipc_socket_set_send_timeout(client->sock, client->send_timeout);
    LOG_DEBUG("ipc client connect success.");

    return (true);
}

/* Disconnect from server
* After disconnect, the `ipc_client_connect` function can be called again */
bool ipc_client_disconnect (ipc_client_t *client)
{
    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ipc client disconnect failed: invalid client handle.");
        return (false);
    }

    ipc_mutex_lock(client->lock);

    client->connected = false;
    ipc_memory_barrier();

    if (client->sock >= 0) {
        ipc_socket_shutdown(client->sock);
    }

    ipc_mutex_unlock(client->lock);

    ipc_client_timeout_all(client);

    LOG_DEBUG("ipc client disconnect success.");

    return (true);
}

/*
* IPC client is connect with server
*/
bool ipc_client_is_connect (ipc_client_t *client)
{
    return (client ? (client->valid && client->connected) : false);
}

/*
* IPC client send timeout
*/
bool ipc_client_send_timeout (ipc_client_t *client, const int timeout_ms)
{
    if (!client || !client->valid) {
        LOG_ERROR("ipc client send timeout failed: invalid client handle.");
        return (false);
    }

    if (timeout_ms > 0) {
        client->send_timeout  = timeout_ms;
    } else {
        client->send_timeout = IPC_DEF_SEND_TIMEOUT;
    }

    if (client->connected && client->sock >= 0) {
        ipc_socket_set_send_timeout(client->sock, client->send_timeout);
    }

    LOG_DEBUG("set ipc client send timeout success.");
    return (true);
}

/*
* IPC client checking event
*/
int ipc_client_fds (ipc_client_t *client, fd_set *rfds)
{
    int max_fd;
    int evt_fd = ipc_event_pair_get_read_fd(client->evtfd);

    if (!client || !client->valid) {
        LOG_ERROR("ipc client fds failed: invalid client handle.");
        return (-1);
    }

    if (!client->connected) {
        FD_SET(evt_fd, rfds);
        LOG_ERROR("ipc client fds faield: client not connected.");
        return (evt_fd);
    }

    FD_SET(client->sock, rfds);
    max_fd = client->sock;

    FD_SET(evt_fd, rfds);
    if (max_fd < evt_fd) {
        max_fd = evt_fd;
    }

    LOG_DEBUG("ipc client fds is %d.", max_fd);

    return (max_fd);
}

/*
* Client input callback
*/
static bool ipc_client_input (ipc_header_t *ipc_hdr, void *varg)
{
    ipc_client_t *client = (ipc_client_t *)varg;
    ipc_client_pendq_t *pendq, *temp;
    uint16_t seqno;
    ipc_url_ref_t url;
    ipc_payload_ref_t payload;

    if (ipc_hdr->msg_type == IPC_MSG_TYPE_PUBLISH || ipc_hdr->msg_type == IPC_MSG_TYPE_MESSAGE) {
        ipc_get_url(ipc_hdr, &url);
        ipc_get_payload(ipc_hdr, &payload);

        if (ipc_hdr->msg_type == IPC_MSG_TYPE_PUBLISH) {
            LOG_DEBUG("ipc client input: get publish msg.");
            if (client->onsub) {
                client->onsub(client, &url, &payload, client->sub_arg);
            }
        } else if (client->onmsg) {
            LOG_DEBUG("ipc client input: get message msg.");
            client->onmsg(client, &url, &payload, client->msg_arg);
         }
        goto  out;

    } else if (ipc_hdr->msg_type == IPC_MSG_TYPE_REPLY_FLAG) {
        LOG_DEBUG("ipc client input: get reply msg.");
        goto  out;
    }

    seqno = ipc_get_seqno(ipc_hdr);

    ipc_mutex_lock(client->lock);

    LIST_FOREACH_SAFE(pendq, temp, client->head) {
        if (pendq->seqno == seqno) {
            if (pendq->ftype == IPC_CLIENT_FTYPE_RPC) {
                client->rpc_pending--;
            }
            DELETE_FROM_FIFO(pendq, client->head, client->tail);
            break;
        }
    }

    ipc_mutex_unlock(client->lock);

    if (pendq) {
        switch (ipc_hdr->msg_type) {

        case IPC_MSG_TYPE_SUBSCRIBE:
        case IPC_MSG_TYPE_UNSUBSCRIBE:
        case IPC_MSG_TYPE_PING_ECHO:
            if (pendq->ftype == IPC_CLIENT_FTYPE_RES) {
                if (pendq->callback.res) {
                    pendq->callback.res(client, ipc_hdr->status == 0, pendq->arg);
                }
            }
            break;

        case IPC_MSG_TYPE_RPC_REQUEST:
            if (pendq->ftype == IPC_CLIENT_FTYPE_RPC) {
                if (pendq->callback.rpc) {
                    ipc_get_payload(ipc_hdr, &payload);
                    pendq->callback.rpc( client, ipc_hdr, &payload, pendq->arg);
                }
            }
            break;

        default:
            break;
        }

        ipc_client_pendq_free(client, pendq);
    }

    LOG_DEBUG("ipc client input finished.");

out:
    return (client->valid);
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
        LOG_DEBUG("ipc client process event failed: invalid client handle.");
        return (false);
    }

    if (client->connected) 
    {
        if (FD_ISSET(client->sock, rfds)) {
            pkt_e = false;
            num = recv(client->sock, client->recvbuf, IPC_MAX_PACKET_SIZE, MSG_DONTWAIT);
            if (num > 0) {
                // TODO: deal recv msg;
                if (!ipc_stream_feed(&client->recv, client->recvbuf,
                                    num, ipc_client_input, client)) {
                    LOG_ERROR("ipc client process event failed: stream feed failed.");
                    pkt_e = true;
                }
            }

            if (pkt_e || num == 0 || (num < 0 && errno != EWOULDBLOCK)) {
                client->connected = false;
                ipc_memory_barrier();

                ipc_client_timeout_all(client);
                LOG_ERROR("ipc client process event failed: process stream failed.");
                return (false);
            }
        }
    }

    int evt_fd = ipc_event_pair_get_read_fd(client->evtfd);
    if (FD_ISSET(evt_fd, rfds)) 
    {
        to_head = to_tail = NULL;

        ipc_event_pair_drain(client->evtfd);

        ipc_mutex_lock(client->lock);

        LIST_FOREACH_SAFE(pendq, temp, client->head) {
            if (pendq->alive <= 0) {
                if (pendq->ftype == IPC_CLIENT_FTYPE_RPC) {
                    client->rpc_pending--;
                }
                DELETE_FROM_FIFO(pendq, client->head, client->tail);
                INSERT_TO_FIFO(pendq, to_head, to_tail);
            }
        }

        ipc_mutex_unlock(client->lock);

        if (to_head) {
            LIST_FOREACH_SAFE(pendq, temp, to_head) {
                if (pendq->callback.msg) {
                    pendq->callback.msg(client, NULL, NULL, pendq->arg);
                }
                DELETE_FROM_FIFO(pendq, to_head, to_tail);
                ipc_client_pendq_free(client, pendq);
            }
        }
    }

    return (true);
}

/*
* Prepare a non-queued seqno
*/
static uint16_t ipc_client_prepare_seqno (ipc_client_t *client)
{
    uint16_t seqno;

    ipc_spinlock_lock(client->spin);

    if (client->seqno_nq == 0) {
        seqno = 1;
        client->seqno_nq = 2;
    } else {
        seqno = client->seqno_nq;
        client->seqno_nq++;
    }

    ipc_spinlock_unlock(client->spin);

    return (seqno << IPC_CLIENT_MAX_POFFSET);
}

/*
* Prepare a pendq
*/
static ipc_client_pendq_t *ipc_client_prepare_pendq (ipc_client_t *client, bool fast, void *arg, 
                                                    uint32_t ftype, uint64_t timeout_ms)
{
    uint16_t i, seqno;
    ipc_client_pendq_t *pendq = NULL, *queued;

    if (fast) {
        if (client->free) {
            ipc_mutex_lock(client->lock);
            pendq = client->free;
            DELETE_FROM_LIST(pendq, client->free);
            ipc_mutex_unlock(client->lock);
        }
    }

    if (!pendq) {
        pendq = (ipc_client_pendq_t *)malloc(sizeof(ipc_client_pendq_t));
        if (!pendq) {
            return (NULL);
        }
    }
    
    ipc_mutex_lock(client->lock);
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
    ipc_mutex_unlock(client->lock);

    if (i >= IPC_CLIENT_MAX_PENDING) {
        ipc_client_pendq_free(client, pendq);
        return (NULL);
    }

    pendq->arg = arg;
    pendq->ftype = ftype;

    if (timeout_ms > 0) {
        pendq->alive = timeout_ms;
    } else {
        pendq->alive = IPC_DEF_SEND_TIMEOUT;
    }

    return (pendq);
}

/*
* Request
*/
static bool ipc_client_request (ipc_client_t *client, uint8_t type, 
                                const ipc_url_ref_t *url, const ipc_payload_ref_t *payload,
                                ipc_client_res_func_t callback, void *arg, uint64_t timeout_ms)
{
    size_t len;
    uint16_t seqno;
    ipc_header_t *ipc_hdr;
    ipc_client_pendq_t *pendq;

    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ipc client request: invalid client handle.");
        return (false);
    }

    if (callback) {
        pendq = ipc_client_prepare_pendq(client, type == IPC_MSG_TYPE_PING_ECHO,
                                        arg, IPC_CLIENT_FTYPE_RES, timeout_ms);
        if (!pendq) {
            LOG_ERROR("ipc client request: prepare pendq failed.");
            return (false);
        }
        pendq->callback.res = callback;
        seqno = pendq->seqno;
    } else {
        pendq = NULL;
        seqno = ipc_client_prepare_seqno(client);
    }

    ipc_mutex_lock(client->lock);

    ipc_hdr = ipc_create_header(client->sendbuf, type, 0, seqno);

    if (!ipc_client_sendmsg(client, ipc_hdr, url, payload)) {
        LOG_ERROR("ipc client request: sendmsg failed.");
        goto error;
    }

    if (pendq) {
        INSERT_TO_FIFO(pendq, client->head, client->tail);
    }

    ipc_mutex_unlock(client->lock);

    LOG_DEBUG("ipc client request success.");
    return (true);

error:
    ipc_mutex_unlock(client->lock);

    if (pendq) {
        ipc_client_pendq_free(client, pendq);
    }

    return (false);
}

/*
* Subscribe URL
*/
bool ipc_client_subscribe (ipc_client_t *client, const ipc_url_ref_t *url,
                            ipc_client_res_func_t callback, void *arg, uint64_t timeout_ms)
{
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ipc client subscribe failed: invalid client handle.");
        return (false);
    }

    return (ipc_client_request(client, IPC_MSG_TYPE_SUBSCRIBE, url, NULL, callback, arg, timeout_ms));
}

/*
* Unsubscribe URL
*/
bool ipc_client_unsubscribe (ipc_client_t *client, const ipc_url_ref_t *url,
                            ipc_client_res_func_t callback, void *arg, uint64_t timeout_ms)
{
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ipc client ipc_client_unsubscribe failed: invalid client handle.");
        return (false);
    }

    return (ipc_client_request(client, IPC_MSG_TYPE_UNSUBSCRIBE, url, NULL, callback, arg, timeout_ms));
}

/*
* RPC call with externed arguments.
* This functions are special interfaces used by client robots, and users are prohibited from using them!
*/
static int ipc_client_call_ex (ipc_client_t *client, const ipc_url_ref_t *url, const ipc_payload_ref_t *payload,
                        ipc_client_rpc_func_t callback, void *arg, uint64_t timeout_ms, void *arg_ex)
{
    size_t len;
    uint8_t flag;
    uint16_t seqno;
    ipc_header_t *ipc_hdr;
    ipc_client_pendq_t *pendq;

    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ipc client call failed: invalid client handle.");
        return (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ipc client call failed: invalid url.");
        return (false);
    }

    if (callback) {
        pendq = ipc_client_prepare_pendq(client, false, arg, IPC_CLIENT_FTYPE_RPC, timeout_ms);
        if (!pendq) {
            LOG_ERROR("ipc client call failed: prepare pendq failed");
            return (false);
        }
        seqno = pendq->seqno;
        pendq->callback.rpc = callback;
    } else {
        pendq = NULL;
        seqno = ipc_client_prepare_seqno(client);
    }

    ipc_mutex_lock(client->lock);

    ipc_hdr = ipc_create_header(client->sendbuf, IPC_MSG_TYPE_RPC_REQUEST, 0, seqno);

    if (!ipc_client_sendmsg(client, ipc_hdr, url, payload)) {
        LOG_ERROR("ipc client call failed: send msg failed.");
        goto error;
    }

    if (pendq) {
        INSERT_TO_FIFO(pendq, client->head, client->tail);
        client->rpc_pending++;
    }

    ipc_mutex_unlock(client->lock);

    LOG_DEBUG("ipc client call success.");

    return (true);

error:
    ipc_mutex_unlock(client->lock);

    if (pendq) {
        ipc_client_pendq_free(client, pendq);
    }

    return (false);
}

/*
* RPC call
*/
int ipc_client_call (ipc_client_t *client, const ipc_url_ref_t *url, const ipc_payload_ref_t *payload,
                    ipc_client_rpc_func_t callback, void *arg, uint64_t timeout_ms)
{
    return (ipc_client_call_ex(client, url, payload, callback, arg, timeout_ms, NULL));
}

/*
* Send message to server
*/
int ipc_client_message (ipc_client_t *client, const ipc_url_ref_t *url, const ipc_payload_ref_t *payload)
{
    bool ret;
    size_t len;
    ipc_header_t *ipc_hdr;

    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ipc client message: invalid client handle.");
        return (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ipc client message: invalid url.");
        return (false);
    }
    if (!payload) {
        LOG_ERROR("ipc client message: invalid payload.");
        return (false);
    }

    ipc_mutex_lock(client->lock);

    ipc_hdr = ipc_create_header(client->sendbuf, IPC_MSG_TYPE_MESSAGE, 0, 0);

    ret = ipc_client_sendmsg(client, ipc_hdr, url, payload);

    ipc_mutex_unlock(client->lock);

    LOG_DEBUG("ipc client message success.");

    return (ret);

error:
    ipc_mutex_unlock(client->lock);

    return (false);
}

void ipc_client_set_on_message (ipc_client_t *client, ipc_client_msg_func_t callback, void *arg)
{
    if (client) {
        client->onmsg = callback;
        client->msg_arg  = arg;
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
            LOG_ERROR("ipc_client_poll: connection of client %d lost", client->cid);
        }
        return 0;
    }
    return cnt;
}

void ipc_client_run(ipc_client_t *client)
{
    int max_fd, cnt;
    fd_set fds;
    sigset_t empty_mask;

    FD_ZERO(&fds);
    max_fd = ipc_client_fds(client, &fds);

    sigemptyset(&empty_mask);

    while(true) {
        cnt = pselect(max_fd + 1, &fds, NULL, NULL, NULL, &empty_mask);
        if (cnt > 0) {
            if (!ipc_client_process_events(client, &fds)) {
                ipc_client_close(client);
                LOG_ERROR("ipc_client_poll: connection of client %d lost", client->cid);
                return;
            }
        }
    }
}


/*
* end
*/
