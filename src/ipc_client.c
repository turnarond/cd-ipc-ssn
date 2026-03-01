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
#include "ipc_error.h"
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
    uint16_t index;          // pending 序号
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
    uint16_t seqno_to_index[65536];
    void *sendbuf;
    void *recvbuf;
    ipc_stream_ctx_t recv;
    bool cid_valid;
    uint32_t cid;
    uint16_t seqno;
    uint16_t next_non_pending_seqno;
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

/**
 * @brief 检查位图中指定位置的位是否设置
 * 
 * @param bit_map 位图指针
 * @param i 要检查的位位置
 * @return true 位已设置，false 位未设置
 */
static bool is_bit_set(uint32_t *bit_map, int i) {
    return (bit_map[i / 32] & (1U << (i % 32)));
}

/**
 * @brief IPC 客户端定时器线程处理函数
 * 
 * @param arg 线程参数（未使用）
 * @return NULL
 */
void *ipc_client_timer_handle(void *arg)
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
                        LOG_INFO("seqno %d of client %d timeout", pendq->seqno, client->cid);
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

/**
 * @brief 根据序列号获取待处理请求
 * 
 * @param client 客户端实例
 * @param seqno 请求序列号
 * @return 待处理请求指针，失败返回 NULL
 */
static ipc_pending_request_t *get_pending_by_seqno(ipc_client_t *client, uint16_t seqno)
{
    // TODO: list mapping to find pending's seqno.
    uint16_t index = client->seqno_to_index[seqno];
    if (index == 0xFFFF || index >= IPC_CLIENT_MAX_PENDING) {
        return NULL;
    }
    
    ipc_mutex_lock(client->lock);
    if (!is_bit_set(client->pending_bitmap, index)) {
        ipc_mutex_unlock(client->lock);
        return NULL;
    }

    ipc_pending_request_t *pendq = &client->pending_pool[index];
    ipc_mutex_unlock(client->lock);
    return pendq;
}

/**
 * @brief 释放待处理请求的索引
 * 
 * @param client 客户端实例
 * @param index 待处理请求的索引
 */
static void free_pending_index(ipc_client_t *client, uint16_t index)
{
    if (index >= IPC_CLIENT_MAX_PENDING) {
        LOG_ERROR("free pending index: invalid index %d", index);
        return;
    }

    int w = index / 32;
    int b = index % 32;
    client->pending_bitmap[w] &= ~(1U << b);
}

/**
 * @brief 创建IPC客户端
 * 
 * @param onmsg 消息处理回调函数
 * @param arg 回调函数参数
 * @return 客户端实例指针，失败返回NULL
 * @warning 此函数必须与ipc_client_close()调用互斥
 */
ipc_client_t *ipc_client_create(ipc_client_msg_handler_t onmsg, void *arg)
{
    int i, err = 0;
    ipc_client_t *client;

    client = (ipc_client_t *)malloc(sizeof(ipc_client_t));
    if (!client) {
        LOG_ERROR("ipc client create: malloc errno %d", errno);
        return (NULL);
    }

    memset(client, 0, sizeof(ipc_client_t));
    memset(client->seqno_to_index, 0xFF, sizeof(client->seqno_to_index));

    // 初始化pendings
    for (int i = 0 ; i < IPC_CLIENT_MAX_PENDING ; i++) {
        client->pending_pool[i].index = i;
    }

    client->sock = -1;

    if (ipc_event_pair_create(&client->evtfd) != 0) {
        LOG_ERROR("ipc client create: event pair create failed, errno %d", errno);
        err = 1;
        goto error;
    }

    client->sendbuf = malloc(IPC_MAX_PACKET_SIZE * 2);
    if (!client->sendbuf) {
        err = 2;
        LOG_ERROR("ipc client create: sendbuf malloc, errno %d", errno);
        goto error;
    }

    ipc_spinlock_init(&client->spin);

    if (ipc_mutex_init(&client->lock)) {
        err = 3;
        LOG_ERROR("ipc client create: mutex init failed, errno %d", errno);
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

/**
 * @brief 关闭IPC客户端
 * 
 * @param client 客户端实例指针
 * @warning 此函数必须与ipc_client_create()调用互斥
 */
void ipc_client_close(ipc_client_t *client)
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
            if (pendq->ftype == IPC_CLIENT_FTYPE_RPC && pendq->callback.rpc) {
                pendq->callback.rpc(client, NULL, NULL, pendq->arg);
            } else if (pendq->ftype == IPC_CLIENT_FTYPE_RES && pendq->callback.res) {
                pendq->callback.res(client, false, pendq->arg);
            }
            free_pending_index(client, pendq->index);
        }
    }
    ipc_mutex_unlock(client->lock);

    ipc_mutex_destroy(client->lock);
    ipc_spinlock_destroy(client->spin);
    free(client);
    LOG_DEBUG("ip client close success.");
}

/**
 * @brief 客户端发送数据包
 * 
 * @param client 客户端实例指针
 * @param len 数据包长度
 * @return 发送成功返回true，失败返回false
 */
static bool ipc_client_send(ipc_client_t *client, size_t len)
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

/**
 * @brief 客户端发送消息
 * 
 * @param client 客户端实例指针
 * @param ipc_hdr IPC消息头部
 * @param url URL引用
 * @param data 数据引用
 * @return 发送成功返回true，失败返回false
 */
static bool ipc_client_sendmsg(ipc_client_t *client, ipc_header_t *ipc_hdr, 
    const ipc_url_ref_t *url, const ipc_data_ref_t *data)
{
    bool ret = ipc_send_message(client->sock, ipc_hdr, url, data);
    if (!ret) {
        ipc_socket_shutdown(client->sock);
    }
    return ret;
}

/**
 * @brief 所有RPC回调超时处理
 * 
 * @param client 客户端实例指针
 */
static void ipc_client_timeout_all (ipc_client_t *client)
{
    ipc_pending_request_t *pendq;

    ipc_mutex_lock(client->lock);
    for (int i = 0; i < IPC_CLIENT_MAX_PENDING; i++) {
        if (is_bit_set(client->pending_bitmap, i)) { // used
            pendq = &client->pending_pool[i];
            if (pendq->ftype == IPC_CLIENT_FTYPE_RPC && pendq->callback.rpc) {
                pendq->callback.rpc(client, NULL, NULL, pendq->arg);
            } else if (pendq->ftype == IPC_CLIENT_FTYPE_RES && pendq->callback.res) {
                pendq->callback.res(client, false, pendq->arg);
            }
            free_pending_index(client, pendq->index);
        }
    }
    ipc_mutex_unlock(client->lock);
}

/**
 * @brief 连接输入回调函数
 * 
 * @param ipc_hdr IPC消息头部
 * @param varg 回调参数
 * @return 处理成功返回true，失败返回false
 */
static bool ipc_client_conn_input(ipc_header_t *ipc_hdr, void *varg)
{
    struct conn_input_arg *arg = (struct conn_input_arg *)varg;
    ipc_data_ref_t data;
    size_t length;
    uint8_t *nid;

    if (ipc_hdr->msg_type != IPC_MSG_TYPE_SERVICE_INFO) {
        return (true);
    }
    if (ipc_get_status(ipc_hdr)) {
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

/**
 * @brief 连接到服务器（同步）
 * 
 * @param client 客户端实例指针
 * @param ipc_path IPC路径
 * @param timeout 超时时间
 * @return 连接成功返回true，失败返回false
 */
bool ipc_client_connect(ipc_client_t *client, const char* ipc_path,
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
        ipc_handle_error(IPC_ERR_INVALID_ARGS, __FILE__, __LINE__, __func__, "invalid client handle");
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
        ipc_handle_error(IPC_ERR_NET_CONNECT, __FILE__, __LINE__, __func__, "create socket failed, errno %d", errno);
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
            ipc_handle_error(IPC_ERR_NET_CONNECT, __FILE__, __LINE__, __func__, "connect failed, errno %d", errno);
            return (false);
        }
    }

    FD_ZERO(&fds);
    FD_SET(client->sock, &fds);

    ret = pselect(client->sock + 1, NULL, &fds, NULL, timeout, NULL);
    if (ret <= 0 || !FD_ISSET(client->sock, &fds)) {
        ipc_handle_error(IPC_ERR_NET_CONNECT, __FILE__, __LINE__, __func__, "pselect failed, errno %d", errno);
        return (false);
    }

    if (!ipc_client_send(client, sizeof(ipc_header_t))) {
        ipc_handle_error(IPC_ERR_NET_WRITE, __FILE__, __LINE__, __func__, "send failed, errno %d", errno);
        return (false);
    }

    ret = pselect(client->sock + 1, &fds, NULL, NULL, timeout, NULL);
    if (ret <= 0 || !FD_ISSET(client->sock, &fds)) {
        ipc_handle_error(IPC_ERR_NET_CONNECT, __FILE__, __LINE__, __func__, "pselect failed, errno %d", errno);
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
        ipc_handle_error(IPC_ERR_NET_READ, __FILE__, __LINE__, __func__, "recv failed, errno %d", errno);
        return (false);
    }

    client->connected = true;
    ipc_memory_barrier();

    /* Set send timeout */
    ipc_socket_set_send_timeout(client->sock, client->send_timeout);
    LOG_DEBUG("ipc client connect success.");

    return (true);
}

/**
 * @brief 从服务器断开连接
 * 
 * 断开连接后，可以再次调用`ipc_client_connect`函数
 * 
 * @param client 客户端实例指针
 * @return 断开连接成功返回true，失败返回false
 */
bool ipc_client_disconnect(ipc_client_t *client)
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

/**
 * @brief 检查IPC客户端是否已连接到服务器
 * 
 * @param client 客户端实例指针
 * @return 已连接返回true，未连接返回false
 */
bool ipc_client_is_connect(ipc_client_t *client)
{
    return (client ? (client->valid && client->connected) : false);
}

/**
 * @brief 设置IPC客户端发送超时
 * 
 * @param client 客户端实例指针
 * @param timeout_ms 超时时间（毫秒）
 * @return 设置成功返回true，失败返回false
 */
bool ipc_client_send_timeout(ipc_client_t *client, const int timeout_ms)
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

/**
 * @brief 获取IPC客户端文件描述符
 * 
 * @param client 客户端实例指针
 * @param rfds 文件描述符集
 * @return 最大文件描述符，失败返回-1
 */
int ipc_client_fds(ipc_client_t *client, fd_set *rfds)
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

/**
 * @brief 客户端输入回调函数
 * 
 * @param ipc_hdr IPC消息头部
 * @param varg 回调参数
 * @return 处理成功返回true，失败返回false
 */
static bool ipc_client_handle_publish(ipc_client_t *client, ipc_header_t *ipc_hdr)
{
    ipc_url_ref_t url;
    ipc_data_ref_t data;
    
    ipc_get_url(ipc_hdr, &url);
    ipc_get_data(ipc_hdr, &data);
    
    LOG_DEBUG("ipc client input: get publish msg.");
    if (client->onsub) {
        client->onsub(client, &url, &data, client->sub_arg);
    }
    
    return true;
}

static bool ipc_client_handle_message(ipc_client_t *client, ipc_header_t *ipc_hdr)
{
    ipc_url_ref_t url;
    ipc_data_ref_t data;
    
    ipc_get_url(ipc_hdr, &url);
    ipc_get_data(ipc_hdr, &data);
    
    LOG_DEBUG("ipc client input: get message msg.");
    if (client->onmsg) {
        client->onmsg(client, &url, &data, client->msg_arg);
    }
    
    return true;
}

static bool ipc_client_handle_response(ipc_client_t *client, ipc_header_t *ipc_hdr, ipc_pending_request_t *pendq)
{
    if (pendq->ftype == IPC_CLIENT_FTYPE_RES) {
        if (pendq->callback.res) {
            pendq->callback.res(client, ipc_get_status(ipc_hdr) == 0, pendq->arg);
        }
    }
    
    return true;
}

static bool ipc_client_handle_rpc_response(ipc_client_t *client, ipc_header_t *ipc_hdr, ipc_pending_request_t *pendq)
{
    ipc_data_ref_t data;
    
    if (pendq->ftype == IPC_CLIENT_FTYPE_RPC) {
        if (pendq->callback.rpc) {
            ipc_get_data(ipc_hdr, &data);
            pendq->callback.rpc(client, ipc_hdr, &data, pendq->arg);
        }
    }
    
    return true;
}

static bool ipc_client_input(ipc_header_t *ipc_hdr, void *varg)
{
    ipc_client_t *client = (ipc_client_t *)varg;
    ipc_pending_request_t *pendq;
    uint16_t seqno;

    if (ipc_hdr->msg_type == IPC_MSG_TYPE_PUBLISH) {
        ipc_client_handle_publish(client, ipc_hdr);
        goto out;
    } else if (ipc_hdr->msg_type == IPC_MSG_TYPE_MESSAGE) {
        ipc_client_handle_message(client, ipc_hdr);
        goto out;
    }

    seqno = ipc_get_seqno(ipc_hdr);
    pendq = get_pending_by_seqno(client, seqno);

    if (pendq) {
        switch (ipc_hdr->msg_type) {

        case IPC_MSG_TYPE_SUBSCRIBE:
        case IPC_MSG_TYPE_UNSUBSCRIBE:
        case IPC_MSG_TYPE_PING_ECHO:
            ipc_client_handle_response(client, ipc_hdr, pendq);
            break;

        case IPC_MSG_TYPE_RPC_REQUEST:
            ipc_client_handle_rpc_response(client, ipc_hdr, pendq);
            break;

        default:
            break;
        }

        free_pending_index(client, pendq->index);
        LOG_DEBUG("ipc client input free seqno pend %d, index %d.", pendq->seqno, pendq->index);
    }

    LOG_DEBUG("ipc client input finished.");

out:
    return (client->valid);
}

/**
 * @brief IPC客户端输入事件处理
 * 
 * @param client 客户端实例指针
 * @param rfds 文件描述符集
 * @return 处理成功返回true，失败返回false
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
                    if (pendq->ftype == IPC_CLIENT_FTYPE_RPC && pendq->callback.rpc) {
                        pendq->callback.rpc(client, NULL, NULL, pendq->arg);
                    } else if (pendq->ftype == IPC_CLIENT_FTYPE_RES && pendq->callback.res) {
                        pendq->callback.res(client, false, pendq->arg);
                    }
                    free_pending_index(client, pendq->index);
                }
            }
        }
        ipc_mutex_unlock(client->lock);
    }

    return (true);
}

/**
 * @brief 准备一个非队列序列号
 * 
 * @param client 客户端实例指针
 * @return 非队列序列号
 */
static uint16_t ipc_client_prepare_seqno (ipc_client_t *client)
{
    uint16_t seqno;

    ipc_spinlock_lock(client->spin);

    if (client->next_non_pending_seqno == 0) {
        seqno = 1;
        client->next_non_pending_seqno = 2;
    } else {
        seqno = client->next_non_pending_seqno;
        client->next_non_pending_seqno++;
    }

    ipc_spinlock_unlock(client->spin);

    return (seqno);
}

/**
 * @brief 分配待处理请求索引
 * 
 * @param client 客户端实例指针
 * @return 成功返回索引，失败返回-1
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

/**
 * @brief 发送请求
 * 
 * @param client 客户端实例指针
 * @param type 请求类型
 * @param url URL引用
 * @param data 数据引用
 * @param callback 回调函数
 * @param arg 回调参数
 * @param timeout_ms 超时时间
 * @return 发送成功返回true，失败返回false
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
        seqno = pendq->seqno = client->seqno++;
        pendq->timeout_ms = timeout_ms;
        pendq->ftype = type;
        pendq->arg = arg;
        client->seqno_to_index[seqno] = index;
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
        free_pending_index(client, pendq->index);
    }

    return (false);
}

/**
 * @brief 订阅URL
 * 
 * @param client 客户端实例指针
 * @param url URL引用
 * @param callback 回调函数
 * @param arg 回调参数
 * @param timeout_ms 超时时间
 * @return 订阅成功返回true，失败返回false
 */
bool ipc_client_subscribe (ipc_client_t *client, const ipc_url_ref_t *url,
                            ipc_client_result_handler_t callback, void *arg, uint64_t timeout_ms)
{
    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ipc client subscribe failed: invalid client handle.");
        return (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ipc client subscribe failed: invalid url.");
        return (false);
    }

    return (ipc_client_request(client, IPC_MSG_TYPE_SUBSCRIBE, url, NULL, callback, arg, timeout_ms));
}

/**
 * @brief 取消订阅URL
 * 
 * @param client 客户端实例指针
 * @param url URL引用
 * @param callback 回调函数
 * @param arg 回调参数
 * @param timeout_ms 超时时间
 * @return 取消订阅成功返回true，失败返回false
 */
bool ipc_client_unsubscribe (ipc_client_t *client, const ipc_url_ref_t *url,
                            ipc_client_result_handler_t callback, void *arg, uint64_t timeout_ms)
{
    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ipc client unsubscribe failed: invalid client handle.");
        return (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ipc client unsubscribe failed: invalid url.");
        return (false);
    }

    return (ipc_client_request(client, IPC_MSG_TYPE_UNSUBSCRIBE, url, NULL, callback, arg, timeout_ms));
}

/**
 * @brief 带外部参数的RPC调用
 * 
 * @param client 客户端实例指针
 * @param url URL引用
 * @param data 数据引用
 * @param callback 回调函数
 * @param arg 回调参数
 * @param timeout_ms 超时时间
 * @param arg_ex 外部参数
 * @return 调用成功返回0，失败返回-1
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

    ipc_mutex_lock(client->lock);
    
    if (callback) {
        int index = alloc_pending_index(client);
        if (index < 0) {
            ipc_mutex_unlock(client->lock);
            LOG_ERROR("ipc client call failed: prepare pendq failed");
            return -1;
        }
        pendq = &client->pending_pool[index];
        seqno = pendq->seqno = client->seqno++;
        pendq->callback.rpc = callback;
        pendq->timeout_ms = timeout_ms;
        pendq->ftype = IPC_CLIENT_FTYPE_RPC;
        pendq->arg = arg;
        client->seqno_to_index[seqno] = index;
    } else {
        pendq = NULL;
        seqno = ipc_client_prepare_seqno(client);
    }

    ipc_hdr = ipc_create_header(client->sendbuf, IPC_MSG_TYPE_RPC_REQUEST, 0, seqno);

    ipc_mutex_unlock(client->lock);

    if (!ipc_client_sendmsg(client, ipc_hdr, url, data)) {
        LOG_ERROR("ipc client call failed: send msg failed.");
        if (pendq) {
            ipc_mutex_lock(client->lock);
            free_pending_index(client, pendq->index);
            ipc_mutex_unlock(client->lock);
        }
        return -1;
    }

    LOG_DEBUG("ipc client call success.");

    return 0;
}

/**
 * @brief RPC调用
 * 
 * @param client 客户端实例指针
 * @param url URL引用
 * @param data 数据引用
 * @param callback 回调函数
 * @param arg 回调参数
 * @param timeout_ms 超时时间
 * @return 调用成功返回0，失败返回-1
 */
int ipc_client_call (ipc_client_t *client, const ipc_url_ref_t *url, const ipc_data_ref_t *data,
                    ipc_client_rpcreply_handler_t callback, void *arg, uint64_t timeout_ms)
{
    return (ipc_client_call_ex(client, url, data, callback, arg, timeout_ms, NULL));
}

/**
 * @brief 发送消息到服务器
 * 
 * @param client 客户端实例指针
 * @param url URL引用
 * @param data 数据引用
 * @return 发送成功返回0，失败返回-1
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

    if (ret) {
        LOG_DEBUG("ipc client message success.");
        return 0;
    } else {
        LOG_ERROR("ipc client message failed.");
        return -1;
    }
}

/**
 * @brief 设置消息处理回调函数
 * 
 * @param client 客户端实例指针
 * @param callback 消息处理回调函数
 * @param arg 回调参数
 */
void ipc_client_set_on_message (ipc_client_t *client, ipc_client_msg_handler_t callback, void *arg)
{
    if (client) {
        client->onmsg = callback;
        client->msg_arg  = arg;
    }
}

/**
 * @brief 轮询客户端事件
 * 
 * @param client 客户端实例指针
 * @param timeout_ms 超时时间
 * @return 成功返回0，失败返回-1
 */
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

/**
 * @brief 运行客户端事件循环
 * 
 * @param client 客户端实例指针
 */
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
