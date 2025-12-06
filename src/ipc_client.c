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

/* Callback function type */
#define IPC_CLIENT_FTYPE_RPC  1  // RPC
#define IPC_CLIENT_FTYPE_RES  2  // 订阅、PING 的应答

/* Client fast pending buffer */
#define IPC_CLIENT_MAX_PENDING  1024
#define IPC_CLIENT_MAX_PENDING_BITMAP (IPC_CLIENT_MAX_PENDING / 32)

/* Client callback union. */
typedef union {
    ipc_client_rpcreply_handler_t rpc;
    ipc_client_result_handler_t res;
    ipc_client_msg_handler_t msg;
} ipc_client_callback_u;

/* Client pending queue */
typedef struct ipc_pending_request {
    uint16_t seqno;          // 请求序列号（唯一标识）
    uint32_t timeout_ms;     // 超时值
    uint32_t ftype;          // 回调类型：RPC / RES（subscribe等）
    ipc_client_callback_u callback;
    void *arg;
} ipc_pending_request_t;

/* Client */
struct ipc_client {
    bool valid;
    bool connected;
    ipc_client_t *next;
    ipc_client_t *prev;
    ipc_pending_request_t pending_pool[IPC_CLIENT_MAX_PENDING];
    uint32_t pending_bitmap[IPC_CLIENT_MAX_PENDING_BITMAP];
    void *sendbuf;
    void *recvbuf;
    ipc_stream_ctx_t recv;
    bool cid_valid;
    uint32_t cid;
    uint16_t seqno;
    uint16_t seqno_nq;
    int sock;
    ipc_event_pair_t *evtfd;
    int send_timeout;
    ipc_spinlock_t *spin;
    ipc_mutex_t *lock;
    ipc_client_msg_handler_t onsub;
    void *sub_arg;
    ipc_client_msg_handler_t onmsg;
    void *msg_arg;
};

/* Connect input argument */
struct conn_input_arg {
    ipc_client_t *client;
    int packet_cnt;
    char *info;
    size_t sz_info;
};

static bool is_bit_set(uint32_t *bit_map, int i) {
    return (bit_map[i / 32] & (1U << (i % 32)));
}

/*
* IPC client thread
*/
void *ipc_client_timer_handle (void *arg)
{
    bool emit;
    ipc_client_t *client;
    ipc_pending_request_t *pendq, *tmp;

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
            for (int i = 0 ; i < IPC_CLIENT_MAX_PENDING ; i++) {
                if (is_bit_set(client->pending_bitmap, i)) {
                    pendq = &client->pending_pool[i];
                    if (pendq->timeout_ms > IPC_TIMER_PERIOD) {
                        pendq->timeout_ms -= IPC_TIMER_PERIOD;
                    } else {
                        pendq->timeout_ms = 0;
                        emit = true;
                        LOG_INFO("pend %d of client %d emit", client->cid, pendq->seqno);
                        break; // only deal with one timeout pend of a client;
                    }
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

static ipc_pending_request_t *get_pending_by_seqno(ipc_client_t *client, uint16_t seqno) 
{
    if (seqno >= IPC_CLIENT_MAX_PENDING) return NULL;
    int w = seqno / 32;
    int b = seqno % 32;
    if (!(client->pending_bitmap[w] & (1U << b))) {
        return NULL; // 未分配
    }
    return &client->pending_pool[seqno];
}

/*
 * Pendq free
 */
static void free_pending_index (ipc_client_t *client, uint16_t seqno)
{
    if (seqno > IPC_CLIENT_MAX_PENDING) {
        LOG_ERROR("free pending index: invalid seqno %d", seqno);
        return;
    }

    int w = seqno / 32;
    int b = seqno % 32;
    client->pending_bitmap[w] &= ~(1U << b);
}

/*
* Create IPC client
* Warning: This function must be mutually exclusive with the ipc_client_close() call
*/
ipc_client_t *ipc_client_create (ipc_client_msg_handler_t onmsg, void *arg)
{
    int i, err = 0;
    ipc_client_t *client;

    client = (ipc_client_t *)malloc(sizeof(ipc_client_t));
    if (!client) {
        LOG_ERROR("ipc client create: malloc errno %d", errno);
        return (NULL);
    }

    // 初始化pendings
    for (int i = 0 ; i < IPC_CLIENT_MAX_PENDING ; i++) {
        client->pending_pool[i].seqno = i;
    }

    memset(client, 0, sizeof(ipc_client_t));

    client->sock   = -1;

    if (ipc_event_pair_create(&client->evtfd) != 0) {
        LOG_ERROR("ipc client create: event pair create failed, errno %d", errno);
        goto error;
    }

    client->sendbuf = malloc(IPC_MAX_PACKET_SIZE * 2);
    if (!client->sendbuf) {
        err = 1;
        LOG_ERROR("ipc client create: sendbuf malloc, errno %d", errno);
        goto error;
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
    ipc_pending_request_t *pendq;

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

    client->connected = false;
    ipc_memory_barrier();

    if (client->sock >= 0) {
        ipc_socket_close(client->sock);
        client->sock = -1;
    }

    ipc_event_pair_destroy(client->evtfd);
    free(client->sendbuf);

    ipc_mutex_lock(client->lock);
    for (int i = 0 ; i < IPC_CLIENT_MAX_PENDING ; i++) {
        if (is_bit_set(client->pending_bitmap, i)) {
            pendq = &client->pending_pool[i];
            if (pendq->ftype == IPC_CLIENT_FTYPE_RPC &&
                pendq->callback.rpc) {
                pendq->callback.rpc(client, NULL, NULL, pendq->arg);
                free_pending_index(client, pendq->seqno);
            }
        }
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
    const ipc_url_ref_t *url, const ipc_data_ref_t *data)
{
    ssize_t len;
    uint64_t total = (uint64_t)IPC_HEADER_SIZE;

    if (url) {
        total += url->url_len;
        ipc_hdr->url_len = htons((uint16_t)url->url_len);
    }

    if (data) {
        total += data->length;
        ipc_hdr->data_len =  htonl(data->length);
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
    if (data) {
        if (data->data) {
            iov[msg.msg_iovlen].iov_base = data->data;
            iov[msg.msg_iovlen].iov_len = data->length;
            msg.msg_iovlen++;
        }
    }

    len = sendmsg(client->sock, &msg, 0); 

    if (len < 0) {
        ipc_socket_shutdown(client->sock);
        LOG_ERROR("ipc client sendmsg failed, errno %d", errno);
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
    ipc_pending_request_t *pendq;

    ipc_mutex_lock(client->lock);
    for (int i = 0; i < IPC_CLIENT_MAX_PENDING; i++) {
        if (is_bit_set(client->pending_bitmap, i)) { // used
            pendq = &client->pending_pool[i];
            if (pendq->ftype == IPC_CLIENT_FTYPE_RPC &&
                pendq->callback.rpc) {
                pendq->callback.rpc(client, NULL, NULL, pendq->arg);
            }
            free_pending_index(client, pendq->seqno);
        }
    }
    ipc_mutex_unlock(client->lock);
}

/*
* Connect input callback
*/
static bool ipc_client_conn_input (ipc_header_t *ipc_hdr, void *varg)
{
    struct conn_input_arg *arg = (struct conn_input_arg *)varg;
    ipc_data_ref_t data;
    size_t length;
    uint8_t *nid;

    if (ipc_hdr->msg_type != IPC_MSG_TYPE_SERVICE_INFO) {
        return (true);
    }
    if (ipc_hdr->status) {
        return (true);
    }

    if (!ipc_get_data(ipc_hdr, &data) || !data.length) {
        return (true);
    }

    arg->packet_cnt++;

    if (data.length >= sizeof(uint32_t)) {
        nid = (uint8_t *)data.data;
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
    ipc_data_ref_t data;
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
        LOG_ERROR("ipc client connect failed: create socket failed, errno %d.", errno);
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
        LOG_ERROR("ipc client fds failed: client not connected.");
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
    ipc_pending_request_t *pendq;
    uint16_t seqno;
    ipc_url_ref_t url;
    ipc_data_ref_t data;

    if (ipc_hdr->msg_type == IPC_MSG_TYPE_PUBLISH || ipc_hdr->msg_type == IPC_MSG_TYPE_MESSAGE) {
        ipc_get_url(ipc_hdr, &url);
        ipc_get_data(ipc_hdr, &data);

        if (ipc_hdr->msg_type == IPC_MSG_TYPE_PUBLISH) {
            LOG_DEBUG("ipc client input: get publish msg.");
            if (client->onsub) {
                client->onsub(client, &url, &data, client->sub_arg);
            }
        } else if (client->onmsg) {
            LOG_DEBUG("ipc client input: get message msg.");
            client->onmsg(client, &url, &data, client->msg_arg);
         }
        goto  out;

    } else if (ipc_hdr->msg_type == IPC_MSG_TYPE_REPLY_FLAG) {
        LOG_DEBUG("ipc client input: get reply msg.");
        goto  out;
    }

    seqno = ipc_get_seqno(ipc_hdr);
    pendq = get_pending_by_seqno(client, seqno);

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
                    ipc_get_data(ipc_hdr, &data);
                    pendq->callback.rpc( client, ipc_hdr, &data, pendq->arg);
                }
            }
            break;

        default:
            break;
        }

        free_pending_index(client, pendq->seqno);
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
    ipc_pending_request_t *pendq;

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
        ipc_event_pair_drain(client->evtfd);

        ipc_mutex_lock(client->lock);
        for (int i = 0 ; i < IPC_CLIENT_MAX_PENDING ; i++) {
            if (is_bit_set(client->pending_bitmap, i)) {
                pendq = &client->pending_pool[i];
                if (pendq->timeout_ms == 0) {
                    if (pendq->ftype == IPC_CLIENT_FTYPE_RPC && 
                        pendq->callback.rpc) {
                        pendq->callback.msg(client, NULL, NULL, pendq->arg);
                        free_pending_index(client, pendq->seqno);
                    }
                }
            }
        }
        ipc_mutex_unlock(client->lock);
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

    return (seqno);
}

/*
* Prepare a pendq
*/
static int alloc_pending_index (ipc_client_t *client)
{
    for (int w = 0; w < IPC_CLIENT_MAX_PENDING_BITMAP; w++) {
        uint32_t word = client->pending_bitmap[w];
        if (word != 0xFFFFFFFFU) {  // 有空闲位
            // 找第一个 0 位
            uint32_t mask = 1U;
            for (int b = 0; b < 32; b++) {
                if (!(word & mask)) {
                    int idx = w * 32 + b;
                    client->pending_bitmap[w] |= mask;  // 标记为已用
                    return idx;
                }
                mask <<= 1;
            }
        }
    }
    return -1; // full
}

/*
* Request
*/
static bool ipc_client_request (ipc_client_t *client, uint8_t type, 
                                const ipc_url_ref_t *url, const ipc_data_ref_t *data,
                                ipc_client_result_handler_t callback, void *arg, uint64_t timeout_ms)
{
    size_t len;
    uint16_t seqno;
    ipc_header_t *ipc_hdr;
    ipc_pending_request_t *pendq;

    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ipc client request: invalid client handle.");
        return (false);
    }

    if (callback) {
        int index = alloc_pending_index(client);
        if (index < 0) {
            LOG_ERROR("ipc client request: prepare pendq failed.");
            return (false);
        }
        pendq = &client->pending_pool[index];
        pendq->callback.res = callback;
        seqno = pendq->seqno;
        pendq->timeout_ms = timeout_ms;
        pendq->ftype = type;
        pendq->arg = arg;
    } else {
        pendq = NULL;
        seqno = ipc_client_prepare_seqno(client);
    }

    ipc_mutex_lock(client->lock);

    ipc_hdr = ipc_create_header(client->sendbuf, type, 0, seqno);

    if (!ipc_client_sendmsg(client, ipc_hdr, url, data)) {
        LOG_ERROR("ipc client request: sendmsg failed.");
        goto error;
    }

    ipc_mutex_unlock(client->lock);

    LOG_DEBUG("ipc client request success.");
    return (true);

error:
    ipc_mutex_unlock(client->lock);

    if (pendq) {
        free_pending_index(client, pendq->seqno);
    }

    return (false);
}

/*
* Subscribe URL
*/
bool ipc_client_subscribe (ipc_client_t *client, const ipc_url_ref_t *url,
                            ipc_client_result_handler_t callback, void *arg, uint64_t timeout_ms)
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
                            ipc_client_result_handler_t callback, void *arg, uint64_t timeout_ms)
{
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ipc client ipc_client_unsubscribe failed: invalid client handle.");
        return (false);
    }

    return (ipc_client_request(client, IPC_MSG_TYPE_UNSUBSCRIBE, url, NULL, callback, arg, timeout_ms));
}

/*
* RPC call with externed arguments.
*/
static int ipc_client_call_ex (ipc_client_t *client, const ipc_url_ref_t *url, const ipc_data_ref_t *data,
                        ipc_client_rpcreply_handler_t callback, void *arg, uint64_t timeout_ms, void *arg_ex)
{
    size_t len;
    uint8_t flag;
    uint16_t seqno;
    ipc_header_t *ipc_hdr;
    ipc_pending_request_t *pendq;

    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ipc client call failed: invalid client handle.");
        return -1;
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ipc client call failed: invalid url.");
        return -1;
    }

    if (callback) {
        int index = alloc_pending_index(client);
        if (index < 0) {
            LOG_ERROR("ipc client call failed: prepare pendq failed");
            return -1;
        }
        pendq = &client->pending_pool[index];
        seqno = pendq->seqno;
        pendq->callback.rpc = callback;
        pendq->timeout_ms = timeout_ms;
        pendq->ftype = IPC_CLIENT_FTYPE_RPC;
        pendq->arg = arg;
    } else {
        pendq = NULL;
        seqno = ipc_client_prepare_seqno(client);
    }

    ipc_hdr = ipc_create_header(client->sendbuf, IPC_MSG_TYPE_RPC_REQUEST, 0, seqno);

    if (!ipc_client_sendmsg(client, ipc_hdr, url, data)) {
        LOG_ERROR("ipc client call failed: send msg failed.");
        goto error;
    }

    LOG_DEBUG("ipc client call success.");

    return 0;

error:
    ipc_mutex_unlock(client->lock);

    if (pendq) {
        free_pending_index(client, pendq->seqno);
    }

    return (false);
}

/*
* RPC call
*/
int ipc_client_call (ipc_client_t *client, const ipc_url_ref_t *url, const ipc_data_ref_t *data,
                    ipc_client_rpcreply_handler_t callback, void *arg, uint64_t timeout_ms)
{
    return (ipc_client_call_ex(client, url, data, callback, arg, timeout_ms, NULL));
}

/*
* Send message to server
*/
int ipc_client_message (ipc_client_t *client, const ipc_url_ref_t *url, const ipc_data_ref_t *data)
{
    bool ret;
    size_t len;
    ipc_header_t *ipc_hdr;

    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ipc client message: invalid client handle.");
        return -1;
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ipc client message: invalid url.");
        return -1;
    }
    if (!data) {
        LOG_ERROR("ipc client message: invalid data.");
        return -1;
    }

    ipc_mutex_lock(client->lock);

    ipc_hdr = ipc_create_header(client->sendbuf, IPC_MSG_TYPE_MESSAGE, 0, 0);

    ret = ipc_client_sendmsg(client, ipc_hdr, url, data);

    ipc_mutex_unlock(client->lock);

    LOG_DEBUG("ipc client message success.");

    return 0;

error:
    ipc_mutex_unlock(client->lock);

    return -1;
}

void ipc_client_set_on_message (ipc_client_t *client, ipc_client_msg_handler_t callback, void *arg)
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
            LOG_ERROR("ipc client poll: connection of client %d lost", client->cid);
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

    sigemptyset(&empty_mask);

    while(true) {
        
        FD_ZERO(&fds);
        max_fd = ipc_client_fds(client, &fds);
        if (max_fd < 0) break;

        cnt = pselect(max_fd + 1, &fds, NULL, NULL, NULL, &empty_mask);
        if (cnt > 0) {
            if (!ipc_client_process_events(client, &fds)) {
                ipc_client_close(client);
                LOG_ERROR("ipc client run: connection of client %d lost", client->cid);
                return;
            }
        }
    }
    LOG_ERROR("ipc client run: exit invalid.");
}

/*
* end
*/
