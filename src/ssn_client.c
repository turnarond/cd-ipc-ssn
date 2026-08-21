/*
 * SSN client
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
#include <unistd.h>
#include "ssn_list.h"
#include "ssn_client.h"
#include "ssn_global.h"
#include "ssn_frame.h"
#include "ssn_error.h"
#include "util/ssn_log.h"
#include "transports/ssn_transport.h"
#include "protocol/ssn_protocol.h"
#include "protocol/rpc/ssn_rpc.h"
#include "protocol/pubsub/ssn_pubsub.h"
#include "protocol/msg/ssn_msg.h"

static void free_pending_index(ssn_client_t *client, uint16_t index);


/* Callback function type */
#define SSN_CLIENT_FTYPE_RPC  1  // RPC
#define SSN_CLIENT_FTYPE_RES  2  // 订阅、PING 的应答

/* Client fast pending buffer */
#define SSN_CLIENT_MAX_PENDING  1024
#define SSN_CLIENT_MAX_PENDING_BITMAP (SSN_CLIENT_MAX_PENDING / 32)

/* Client callback union. */
typedef union {
    ssn_client_rpcreply_handler_t rpc;
    ssn_client_result_handler_t res;
    ssn_client_msg_handler_t msg;
} ssn_client_callback_u;

/* Client pending queue */
typedef struct ssn_pending_request {
    uint16_t index;          // pending 序号
    uint16_t seqno;          // 请求序列号（唯一标识）
    uint32_t timeout_ms;     // 超时值
    uint32_t ftype;          // 回调类型：RPC / RES（subscribe等）
    ssn_client_callback_u callback;
    void *arg;
} ssn_pending_request_t;

/* Per-URL publish handler (subscribed via ssn_client_subscribe) */
typedef struct ssn_sub_handler {
    struct ssn_sub_handler *next;
    ssn_client_msg_handler_t callback;
    void *arg;
    size_t url_len;
    char url[1];
} ssn_sub_handler_t;

/* Client */
struct ssn_client {
    bool valid;
    bool connected;
    ssn_client_t *next;
    ssn_client_t *prev;
    ssn_pending_request_t pending_pool[SSN_CLIENT_MAX_PENDING];
    uint32_t pending_bitmap[SSN_CLIENT_MAX_PENDING_BITMAP];
    uint16_t seqno_to_index[65536];
    void *sendbuf;
    void *recvbuf;
    ssn_stream_ctx_t recv;
    bool cid_valid;
    uint32_t cid;
    uint16_t seqno;
    uint16_t next_non_pending_seqno;
    ssn_transport_t *transport;
    ipc_event_pair_t *evtfd;
    int send_timeout;
    ipc_spinlock_t *spin;
    ipc_mutex_t *lock;
    int ref_count;  // 引用计数
    ssn_client_msg_handler_t onsub;
    void *sub_arg;
    ssn_client_msg_handler_t onmsg;
    void *msg_arg;

    ssn_sub_handler_t *sub_handlers;

    /* 协议层实例 */
    ssn_rpc_req_t *rpc_req;
    ssn_pubsub_sub_t *pubsub_sub;
    ssn_msg_send_t *msg_send;

    /* RPC 回调适配 */
    ssn_client_rpcreply_handler_t onrpc;
    void *rpc_arg;
};
/*
 * 协议层回调适配器 —— 将新协议层的回调转换为旧 client API 的回调格式
 */

/* RPC 应答适配: ssn_rpc_reply_handler_t → ssn_client_rpcreply_handler_t */
static void rpc_reply_adaptor(uint16_t seqno, uint32_t status,
                              const void *data, size_t data_len, void *arg)
{
    ssn_client_t *client = (ssn_client_t *)arg;
    if (!client || !client->onrpc) return;

    ssn_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    ssn_set_seqno(&hdr, seqno);
    ssn_set_status(&hdr, status);

    ssn_data_ref_t data_ref = {
        .data = (void *)data,
        .length = data_len
    };
    client->onrpc(client, &hdr, &data_ref, client->rpc_arg);
}

/* PubSub 消息适配: ssn_pubsub_msg_handler_t → ssn_client_msg_handler_t */
static void pubsub_msg_adaptor(const char *topic,
                               const void *data, size_t data_len, void *arg)
{
    ssn_client_t *client = (ssn_client_t *)arg;
    if (!client || !client->onsub) return;

    ssn_url_ref_t url_ref = {
        .url = (char *)topic,
        .url_len = strlen(topic)
    };
    ssn_data_ref_t data_ref = {
        .data = (void *)data,
        .length = data_len
    };
    client->onsub(client, &url_ref, &data_ref, client->sub_arg);
}

/* Connect input argument */
struct conn_input_arg {
    ssn_client_t *client;
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
void *ssn_client_timer_handle(void *arg)
{
    bool emit;
    ssn_client_t *client;
    ssn_pending_request_t *pendq, *tmp;

    (void)arg;

    do {
        ipc_thread_msleep(IPC_TIMER_PERIOD);

        /* 进程退出时由 ssn_global_cleanup 置位退出标志，确保线程可退出（join 不阻塞） */
        if (__atomic_load_n(&g_ssn_client_timer_exit, __ATOMIC_ACQUIRE)) {
            break;
        }

        ipc_mutex_lock(g_ssn_client_lock);

        if (!g_ssn_client_list) {
            ipc_mutex_unlock(g_ssn_client_lock);
            continue;   // 空列表时继续轮询而不是退出：定时器线程启动后只执行一次，
                        // 若首个 50ms 内无存活 client 即 break，之后所有 pending 超时不再被处理
        }

        LIST_FOREACH(client, g_ssn_client_list) {
            emit = false;

            ipc_mutex_lock(client->lock);
            for (int i = 0 ; i < SSN_CLIENT_MAX_PENDING ; i++) {
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

        ipc_mutex_unlock(g_ssn_client_lock);

    } while (true);

    ipc_thread_exit();

    return (NULL);
}

/**
 * @brief 增加客户端引用计数
 * 
 * @param client 客户端实例
 */
void ssn_client_ref(ssn_client_t *client)
{
    if (!client) {
        return;
    }
    ipc_mutex_lock(client->lock);
    client->ref_count++;
    ipc_mutex_unlock(client->lock);
}

/**
 * @brief 减少客户端引用计数
 * 
 * @param client 客户端实例
 */
void ssn_client_unref(ssn_client_t *client)
{
    if (!client) {
        return;
    }
    
    bool should_free = false;
    
    ipc_mutex_lock(client->lock);
    if (client->ref_count > 0) {
        client->ref_count--;
        if (client->ref_count == 0 && !client->valid) {
            should_free = true;
        }
    }
    ipc_mutex_unlock(client->lock);
    
    if (should_free) {
        // 真正释放资源
        ssn_pending_request_t *pendq;

        client->connected = false;
        ipc_memory_barrier();

        /* 清理 per-URL 订阅处理器 */
        {
            ssn_sub_handler_t *h, *next;
            for (h = client->sub_handlers; h; h = next) {
                next = h->next;
                free(h);
            }
            client->sub_handlers = NULL;
        }

        /* 销毁协议层实例 */
        if (client->msg_send) {
            ssn_msg_destroy((ssn_protocol_ctx_t *)client->msg_send);
            client->msg_send = NULL;
        }
        if (client->pubsub_sub) {
            ssn_pubsub_destroy((ssn_protocol_ctx_t *)client->pubsub_sub);
            client->pubsub_sub = NULL;
        }
        if (client->rpc_req) {
            ssn_rpc_destroy((ssn_protocol_ctx_t *)client->rpc_req);
            client->rpc_req = NULL;
        }

        if (client->transport) {
        ssn_transport_destroy(client->transport);
        client->transport = NULL;
    }

        ipc_event_pair_destroy(client->evtfd);
        free(client->sendbuf);
        
        /* 缺陷背景：原实现持 client->lock 调用剩余 pending 的用户回调，回调内
         * 调用 ssn_client_* API 会自锁死锁。修复：锁内收集并清位图，解锁后回调。 */
        struct timeout_item {
            uint32_t ftype;
            ssn_client_callback_u callback;
            void *arg;
        } items[SSN_CLIENT_MAX_PENDING];
        int n_items = 0;

        ipc_mutex_lock(client->lock);
        for (int i = 0 ; i < SSN_CLIENT_MAX_PENDING ; i++) {
            if (is_bit_set(client->pending_bitmap, i)) {
                pendq = &client->pending_pool[i];
                if (n_items < SSN_CLIENT_MAX_PENDING) {
                    items[n_items].ftype = pendq->ftype;
                    items[n_items].callback = pendq->callback;
                    items[n_items].arg = pendq->arg;
                    n_items++;
                }
                free_pending_index(client, pendq->index);
            }
        }
        ipc_mutex_unlock(client->lock);
        
        /* 锁外回调：客户端已 invalid，回调内对 ssn_client_* 的调用会被拒绝 */
        for (int i = 0; i < n_items; i++) {
            if (items[i].ftype == SSN_CLIENT_FTYPE_RPC && items[i].callback.rpc) {
                items[i].callback.rpc(client, NULL, NULL, items[i].arg);
            } else if (items[i].ftype == SSN_CLIENT_FTYPE_RES && items[i].callback.res) {
                items[i].callback.res(client, false, items[i].arg);
            }
        }

        ipc_mutex_destroy(client->lock);
        ipc_spinlock_destroy(client->spin);
        free(client);
        LOG_DEBUG("ip client close success.");
    }
}

/**
 * @brief 根据序列号获取待处理请求（锁内拷贝快照）
 * 
 * 缺陷背景：get_pending_by_seqno 解锁后返回池内指针，定时器线程超时路径可并发
 * free 该槽位，调用方随后读 pendq->index 即 use-after-free（TOCTOU）。
 * 修复：锁内完成查找并拷贝关键字段到调用方快照，解锁后使用快照。
 * 
 * @param client 客户端实例指针
 * @param seqno 请求序列号
 * @param[out] out 快照输出
 * @return 找到返回 true，失败返回 false
 */
static bool get_pending_snapshot(ssn_client_t *client, uint16_t seqno,
                                 ssn_pending_request_t *out)
{
    uint16_t index = client->seqno_to_index[seqno];
    if (index == 0xFFFF || index >= SSN_CLIENT_MAX_PENDING) {
        return false;
    }

    ipc_mutex_lock(client->lock);
    if (!is_bit_set(client->pending_bitmap, index)) {
        ipc_mutex_unlock(client->lock);
        return false;
    }

    *out = client->pending_pool[index];   /* 拷贝 callback/arg/index/seqno/ftype */
    ipc_mutex_unlock(client->lock);
    return true;
}

/**
 * @brief 释放待处理请求的索引
 * 
 * @param client 客户端实例
 * @param index 待处理请求的索引
 */
static void free_pending_index(ssn_client_t *client, uint16_t index)
{
    if (index >= SSN_CLIENT_MAX_PENDING) {
        LOG_ERROR("free pending index: invalid index %d", index);
        return;
    }

    // 注意：此函数假设调用者已经持有 client->lock 锁
    /* 缺陷背景：原实现只清位图，seqno_to_index 映射残留——seqno 回绕或迟到
     * 应答时旧 seqno 错配到复用同 index 的新请求（应答串台）。修复：释放时
     * 同步清映射，并校验槽位 seqno 与映射一致才清（防误清新请求）。 */
    uint16_t slot_seqno = client->pending_pool[index].seqno;
    if (client->seqno_to_index[slot_seqno] == index) {
        client->seqno_to_index[slot_seqno] = 0xFFFF;
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
 * @warning 此函数必须与ssn_client_close()调用互斥
 */
ssn_client_t *ssn_client_create(void)
{
    int i, err = 0;
    ssn_client_t *client;

    client = (ssn_client_t *)malloc(sizeof(ssn_client_t));
    if (!client) {
        LOG_ERROR("ssn client create: malloc errno %d", errno);
        return (NULL);
    }

    memset(client, 0, sizeof(ssn_client_t));
    memset(client->seqno_to_index, 0xFF, sizeof(client->seqno_to_index));

    // 初始化pendings
    for (int i = 0 ; i < SSN_CLIENT_MAX_PENDING ; i++) {
        client->pending_pool[i].index = i;
    }

    client->transport = NULL;

    if (ipc_event_pair_create(&client->evtfd) != 0) {
        LOG_ERROR("ssn client create: event pair create failed, errno %d", errno);
        err = 1;
        goto error;
    }

    client->sendbuf = malloc(SSN_MAX_PACKET_SIZE * 2);
    if (!client->sendbuf) {
        err = 2;
        LOG_ERROR("ssn client create: sendbuf malloc, errno %d", errno);
        goto error;
    }

    ipc_spinlock_init(&client->spin);

    if (ipc_mutex_init(&client->lock)) {
        err = 3;
        LOG_ERROR("ssn client create: mutex init failed, errno %d", errno);
        goto error;
    }

    ssn_stream_init(&client->recv);
    client->recvbuf      = (uint8_t *)client->sendbuf + SSN_MAX_PACKET_SIZE;
    client->onmsg        = NULL;
    client->msg_arg      = NULL;
    client->sub_handlers = NULL;
    client->send_timeout = IPC_DEF_SEND_TIMEOUT;
    client->valid        = true;
    client->ref_count    = 1;  // 初始化引用计数为1

    /* 创建协议层实例 */
    client->rpc_req = ssn_rpc_req_create(rpc_reply_adaptor, client);
    if (!client->rpc_req) {
        err = 4;
        LOG_ERROR("ssn client create: rpc_req create failed");
        goto error;
    }

    client->pubsub_sub = ssn_pubsub_sub_create(pubsub_msg_adaptor, client);
    if (!client->pubsub_sub) {
        err = 5;
        LOG_ERROR("ssn client create: pubsub_sub create failed");
        goto error;
    }

    client->msg_send = ssn_msg_send_create();
    if (!client->msg_send) {
        err = 6;
        LOG_ERROR("ssn client create: msg_send create failed");
        goto error;
    }

    ipc_mutex_lock(g_ssn_client_lock);

    INSERT_TO_HEADER(client, g_ssn_client_list);

    ipc_mutex_unlock(g_ssn_client_lock);

    LOG_DEBUG("ssn client create success.");
    return (client);

error:
    if (err > 5) {
        ssn_msg_destroy((ssn_protocol_ctx_t *)client->msg_send);
    }
    if (err > 4) {
        ssn_pubsub_destroy((ssn_protocol_ctx_t *)client->pubsub_sub);
    }
    if (err > 3) {
        ssn_rpc_destroy((ssn_protocol_ctx_t *)client->rpc_req);
    }
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
 * @warning 此函数必须与ssn_client_create()调用互斥
 */
void ssn_client_close(ssn_client_t *client)
{
    if (!client) {
        return;
    }

    /* 缺陷背景：原实现 valid 检查/置位与 DELETE_FROM_LIST 分属不同锁，两线程
     * 同时 close 同一 client 时双双通过 valid 检查 → 第二个 DELETE 命中已摘除
     * 节点（next==prev==NULL）→ 全局链表头被置 NULL，其余 client 泄漏且定时器
     * 停止处理超时；ref_count 双 decrement 亦可能提前触发释放。
     * 修复：valid 的检查与置位在 client->lock 内原子完成（单次进入），
     * 全局链表删除仍持 g_ssn_client_lock。 */
    ipc_mutex_lock(client->lock);
    if (!client->valid) {
        ipc_mutex_unlock(client->lock);
        return;
    }
    /* Set client to invalid state, so that no new operations can be performed.
     * This is necessary to ensure that the client is not used after closing.
     */
    client->valid = false;
    ipc_mutex_unlock(client->lock);

    ipc_mutex_lock(g_ssn_client_lock);
    DELETE_FROM_LIST(client, g_ssn_client_list);
    ipc_mutex_unlock(g_ssn_client_lock);

    // 减少引用计数，当引用计数为0时会真正释放资源
    ssn_client_unref(client);
    LOG_DEBUG("ip client close initiated.");
}

/**
 * @brief 客户端发送数据包
 * 
 * @param client 客户端实例指针
 * @param transport 传输层实例（connect 握手期间传入局部 transport，
 *                  其余路径为 client->transport）
 * @param len 数据包长度
 * @return 发送成功返回true，失败返回false
 */
static bool ssn_client_send(ssn_client_t *client, ssn_transport_t *transport, size_t len)
{
    uint8_t *buffer = (uint8_t *)client->sendbuf;
    ssize_t num, total = 0;

    do {
        num = ssn_transport_send(transport, &buffer[total], len - total);
        if (num > 0) {
            total += num;
        } else {
            ssn_transport_disconnect(transport);
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
static bool ssn_client_sendmsg(ssn_client_t *client, ssn_header_t *ipc_hdr, 
    const ssn_url_ref_t *url, const ssn_data_ref_t *data)
{
    bool ret = ssn_send_message(client->transport, ipc_hdr, url, data);
    if (!ret) {
        ssn_transport_disconnect(client->transport);
    }
    return ret;
}

/**
 * @brief 所有RPC回调超时处理
 * 
 * @param client 客户端实例指针
 */
static void ssn_client_timeout_all (ssn_client_t *client)
{
    /* 缺陷背景：原实现持 client->lock 直接调用用户回调，而回调内再调用任何
     * ssn_client_* API（call/disconnect/close）都需取同一把非递归锁 → 自锁死锁。
     * 修复：先在锁内收集超时项（拷贝回调/arg）并清位图，解锁后再逐个回调。 */
    struct timeout_item {
        uint32_t ftype;
        ssn_client_callback_u callback;
        void *arg;
    } items[SSN_CLIENT_MAX_PENDING];
    int n_items = 0;

    ipc_mutex_lock(client->lock);
    for (int i = 0; i < SSN_CLIENT_MAX_PENDING; i++) {
        if (is_bit_set(client->pending_bitmap, i)) { // used
            ssn_pending_request_t *pendq = &client->pending_pool[i];
            if (n_items < SSN_CLIENT_MAX_PENDING) {
                items[n_items].ftype = pendq->ftype;
                items[n_items].callback = pendq->callback;
                items[n_items].arg = pendq->arg;
                n_items++;
            }
            free_pending_index(client, pendq->index);
        }
    }
    ipc_mutex_unlock(client->lock);

    /* 锁外调用回调：回调内可安全调用 ssn_client_call/disconnect 等 API */
    for (int i = 0; i < n_items; i++) {
        if (items[i].ftype == SSN_CLIENT_FTYPE_RPC && items[i].callback.rpc) {
            items[i].callback.rpc(client, NULL, NULL, items[i].arg);
        } else if (items[i].ftype == SSN_CLIENT_FTYPE_RES && items[i].callback.res) {
            items[i].callback.res(client, false, items[i].arg);
        }
    }
}

/**
 * @brief 连接输入回调函数
 * 
 * @param ipc_hdr IPC消息头部
 * @param varg 回调参数
 * @return 处理成功返回true，失败返回false
 */
static bool ssn_client_conn_input(ssn_header_t *ipc_hdr, void *varg)
{
    struct conn_input_arg *arg = (struct conn_input_arg *)varg;
    ssn_data_ref_t data;
    size_t length;
    uint8_t *nid;

    if (ipc_hdr->msg_type != SSN_MSG_TYPE_SERVICE_INFO) {
        return (true);
    }
    if (ssn_get_status(ipc_hdr)) {
        return (true);
    }

    if (!ssn_get_data(ipc_hdr, &data) || !data.length) {
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
bool ssn_client_connect(ssn_client_t *client, const char* ipc_path,
                        const struct timespec *timeout)
{
    fd_set fds;
    size_t len = 0;
    ssize_t num;
    ssn_header_t *ipc_hdr;
    ssn_data_ref_t data;
    struct sockaddr_un server;
    struct conn_input_arg arg;

    if (!client || !client->valid) {
        ssn_handle_error(SSN_ECODE_INVALID_ARGS, __FILE__, __LINE__, __func__, "invalid client handle");
        return (false);
    }

    // 增加引用计数
    ssn_client_ref(client);

    client->connected = false;
    ipc_memory_barrier();

    /* 加锁销毁旧 transport：与 poll 线程 process_events/fds 读 transport 互斥 */
    ipc_mutex_lock(client->lock);
    if (client->transport) {
        ssn_transport_destroy(client->transport);
        client->transport = NULL;
    }
    ipc_mutex_unlock(client->lock);

    ssn_client_timeout_all(client);

    if (timeout == NULL) {
        struct timespec default_timeout = {3, 0}; // 默认超时 3 秒（注释修正：原误写 5 秒，与 recv 超时混淆）
        timeout = &default_timeout;
    }
    // 创建transport配置
    ssn_transport_config_t config = {
        .non_blocking = true,
        .send_timeout_ms = client->send_timeout,
        .recv_timeout_ms = 5000, // 增加接收超时时间到5秒
        .connect_timeout_ms = timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000,
        .enable_keepalive = true,
        .keepalive_idle_sec = 60,
        .keepalive_interval_sec = 10,
        .keepalive_count = 3,
        .enable_nagle = false,
        .send_buffer_size = SSN_MAX_PACKET_SIZE,
        .recv_buffer_size = SSN_MAX_PACKET_SIZE,
        .reuse_address = true
    };

    // 解析地址，得到地址类型
    ssn_address_t addr;
    if (!ssn_address_parse(ipc_path, &addr)) {
        ssn_handle_error(SSN_ECODE_NET_CONNECT, __FILE__, __LINE__, __func__, "parse address failed: '%s'", ipc_path);
        ssn_client_unref(client);
        return (false);
    }

    // 根据地址类型设置配置和创建transport
    config.type = addr.type;
    /* 缺陷背景：原实现创建后立即锁内赋值 client->transport，随后锁外使用——
     * poll 线程检测到旧连接丢失时销毁 client->transport，可能销毁 connect 刚
     * 发布/正在使用的新 transport（UAF，稳定性套件 T6 负载下偶发；ASAN 定位
     * unix_transport_connect 读已释放 transport）。修复：connect 全程使用局部
     * transport（不发布到 client->transport），握手成功后一次性锁内发布。 */
    ssn_transport_t *new_transport = ssn_transport_create(addr.type, &config);
    if (!new_transport) {
        ssn_handle_error(SSN_ECODE_NET_CONNECT, __FILE__, __LINE__, __func__, "create transport failed");
        ssn_client_unref(client);
        return (false);
    }

    // 连接到服务器
    if (!ssn_transport_connect(new_transport, &addr, config.connect_timeout_ms)) {
        ssn_handle_error(SSN_ECODE_NET_CONNECT, __FILE__, __LINE__, __func__, "connect to '%s' failed", ipc_path);
        ssn_transport_destroy(new_transport);
        ssn_client_unref(client);
        return (false);
    }

    ipc_hdr = ssn_create_header(client->sendbuf, SSN_MSG_TYPE_SERVICE_INFO, 0, 0);

    if (!ssn_client_send(client, new_transport, sizeof(ssn_header_t))) {
        ssn_handle_error(SSN_ECODE_NET_WRITE, __FILE__, __LINE__, __func__, "send failed");
        ssn_transport_destroy(new_transport);
        ssn_client_unref(client);
        return (false);
    }

    // 重试接收，处理非阻塞套接字的 EAGAIN 错误
    int retries = 0;
    /* 重试预算与用户传入的 connect 超时对齐（默认 3s）。缺陷背景：文档化服务端
     * 事件循环（poll + sleep，见 examples/ 各 server）下握手需 ≥2 个 poll 周期
     * （accept 一轮 + SERVICE_INFO 应答一轮），固定 5×100ms=500ms 的旧预算会在
     * 服务端应答前超时，导致用户按示例/教程运行必然连接失败（用户旅程回归）。
     * 每次重试等待 retry_delay_ms，总预算 = connect_timeout_ms（下限 500ms
     * 保持旧行为兼容，上限 6s 防止极端值长时间挂起）。 */
    const int retry_delay_ms = 100;
    int max_retries = config.connect_timeout_ms / retry_delay_ms;
    if (max_retries < 5) {
        max_retries = 5;
    } else if (max_retries > 60) {
        max_retries = 60;
    }

    while (retries < max_retries) {
        num = ssn_transport_recv(new_transport, client->recvbuf, SSN_MAX_PACKET_SIZE, config.recv_timeout_ms);
        
        if (num > 0) {
            arg.client     = client;
            arg.packet_cnt = 0;
            if (!ssn_stream_feed(&client->recv, client->recvbuf,
                                num, ssn_client_conn_input, &arg)) {
                num = -1;
            } else if (arg.packet_cnt > 0) {
                // 成功接收到并处理了服务端响应
                break;
            }
        }
        
        if (num == 0) {
            // 连接被关闭
            LOG_ERROR("recv failed, connection closed by peer");
            ssn_handle_error(SSN_ECODE_NET_READ, __FILE__, __LINE__, __func__, "recv failed, connection closed by peer");
            ssn_transport_destroy(new_transport);
            ssn_client_unref(client);
            return (false);
        } else if (num < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
            // 发生了真正的错误
            LOG_ERROR("recv failed, errno %d: %s", errno, strerror(errno));
            ssn_handle_error(SSN_ECODE_NET_READ, __FILE__, __LINE__, __func__, "recv failed");
            ssn_transport_destroy(new_transport);
            ssn_client_unref(client);
            return (false);
        }
        
        // 非阻塞套接字没有可用数据，重试
        retries++;
        usleep(retry_delay_ms * 1000);
    }
    
    if (retries >= max_retries || !arg.packet_cnt) {
        LOG_ERROR("recv failed after %d retries", max_retries);
        ssn_handle_error(SSN_ECODE_NET_READ, __FILE__, __LINE__, __func__, "recv failed after multiple retries");
        ssn_transport_destroy(new_transport);
        ssn_client_unref(client);
        return (false);
    }

    /* 握手成功：锁内发布新 transport 并置 connected（与 poll 线程 fds/
     * process_events 锁内读 client->transport 互斥）。缺陷背景：发布前
     * connected 保持 false，poll 线程不会使用/销毁它；发布后 poll 线程
     * 才能看到完整就绪的 transport，杜绝「销毁未就绪 transport」的 UAF。 */
    ipc_mutex_lock(client->lock);
    if (client->transport) {
        ssn_transport_destroy(client->transport);
        client->transport = NULL;
    }
    client->transport = new_transport;
    client->connected = true;
    ipc_memory_barrier();
    ipc_mutex_unlock(client->lock);

    /* 绑定协议层实例到传输层（transport 已发布，可直接引用） */
    ssn_rpc_connect((ssn_protocol_ctx_t *)client->rpc_req, client->transport);
    ssn_pubsub_sub_connect(client->pubsub_sub, client->transport);
    ssn_msg_send_connect(client->msg_send, client->transport);

    /* Set send timeout */
    // 发送超时已在transport创建时设置
    LOG_DEBUG("ssn client connect success.");

    ssn_client_unref(client);
    return (true);
}

/**
 * @brief 从服务器断开连接
 * 
 * 断开连接后，可以再次调用`ssn_client_connect`函数
 * 
 * @param client 客户端实例指针
 * @return 断开连接成功返回true，失败返回false
 */
bool ssn_client_disconnect(ssn_client_t *client)
{
    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ssn client disconnect failed: invalid client handle.");
        return (false);
    }

    // 增加引用计数
    ssn_client_ref(client);

    ipc_mutex_lock(client->lock);

    client->connected = false;
    ipc_memory_barrier();

    if (client->transport) {
        ssn_transport_disconnect(client->transport);
    }

    ipc_mutex_unlock(client->lock);

    ssn_client_timeout_all(client);

    LOG_DEBUG("ssn client disconnect success.");

    // 减少引用计数
    ssn_client_unref(client);
    return (true);
}

/**
 * @brief 检查IPC客户端是否已连接到服务器
 * 
 * @param client 客户端实例指针
 * @return 已连接返回true，未连接返回false
 */
bool ssn_client_is_connect(ssn_client_t *client)
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
bool ssn_client_send_timeout(ssn_client_t *client, const int timeout_ms)
{
    if (!client || !client->valid) {
        LOG_ERROR("ssn client send timeout failed: invalid client handle.");
        return (false);
    }

    // 增加引用计数
    ssn_client_ref(client);

    if (timeout_ms > 0) {
        client->send_timeout  = timeout_ms;
    } else {
        client->send_timeout = IPC_DEF_SEND_TIMEOUT;
    }

    /* 加锁保护 transport 重建：与 poll 线程的 process_events/fds 读 transport
     * 互斥，避免「用户线程销毁旧 transport、事件线程仍在 recv/get_fd」的 UAF */
    ipc_mutex_lock(client->lock);
    if (client->connected && client->transport) {
        ssn_transport_config_t config = client->transport->config;
        config.send_timeout_ms = client->send_timeout;
        // 使用transport的实际类型重新创建transport
        ssn_transport_t *new_transport = ssn_transport_create(client->transport->type, &config);
        if (new_transport) {
            ssn_address_t addr;
            if (ssn_transport_get_address(client->transport, &addr)) {
                /* 缺陷背景：原实现忽略 connect 返回值，失败仍销毁旧 transport 并
                 * 发布新 transport（fd=-1）——connected 保持 true 形成「假连接」：
                 * 后续 send 全失败、poll 等不到 EOF 状态卡死，且新连接未握手，
                 * 服务端 5s 超时后将其销毁（双向状态不一致）。修复：connect 失败
                 * 保留旧 transport（发送超时设置仍生效于下次真正重建），仅成功才替换。 */
                if (ssn_transport_connect(new_transport, &addr, config.connect_timeout_ms)) {
                    ssn_transport_destroy(client->transport);
                    client->transport = new_transport;
                } else {
                    LOG_WARN("ssn client send timeout: reconnect failed, keep old transport");
                    ssn_transport_destroy(new_transport);
                }
            } else {
                ssn_transport_destroy(new_transport);
            }
        }
    }
    ipc_mutex_unlock(client->lock);

    LOG_DEBUG("set ipc client send timeout success.");
    
    // 减少引用计数
    ssn_client_unref(client);
    return (true);
}

/**
 * @brief 获取IPC客户端文件描述符
 * 
 * @param client 客户端实例指针
 * @param rfds 文件描述符集
 * @return 最大文件描述符，失败返回-1
 */
int ssn_client_fds(ssn_client_t *client, fd_set *rfds)
{
    int max_fd;

    /* 缺陷背景：原实现先取 evt_fd 再判空——ssn_client_fds(NULL) 必崩。
     * 修复：NULL/valid 检查前置。 */
    if (!client || !client->valid) {
        LOG_ERROR("ssn client fds failed: invalid client handle.");
        return (-1);
    }

    int evt_fd = ipc_event_pair_get_read_fd(client->evtfd);

    if (!client->connected) {
        FD_SET(evt_fd, rfds);
        return (evt_fd);
    }

    /* 锁内读 transport fd：与 connect/send_timeout 的 transport 替换互斥，
     * 避免读到已销毁 transport 的 fd */
    ipc_mutex_lock(client->lock);
    int sock_fd = client->transport ?
                  ssn_transport_get_fd(client->transport) : -1;
    ipc_mutex_unlock(client->lock);

    if (sock_fd >= 0) {
        FD_SET(sock_fd, rfds);
        max_fd = sock_fd;
    } else {
        max_fd = -1;
    }

    FD_SET(evt_fd, rfds);
    if (max_fd < evt_fd) {
        max_fd = evt_fd;
    }

    return (max_fd);
}

/**
 * @brief 客户端输入回调函数
 * 
 * @param ipc_hdr IPC消息头部
 * @param varg 回调参数
 * @return 处理成功返回true，失败返回false
 */
static bool ssn_client_handle_publish(ssn_client_t *client, ssn_header_t *ipc_hdr)
{
    ssn_url_ref_t url;
    ssn_data_ref_t data;
    ssn_sub_handler_t *h;

    ssn_get_url(ipc_hdr, &url);
    ssn_get_data(ipc_hdr, &data);

    LOG_DEBUG("ssn client input: get publish msg, url=%.*s",
              (int)url.url_len, url.url);

    /* 缺陷背景：sub_handlers 链表由 subscribe/unsubscribe（用户线程）无锁增删、
     * handle_publish（poll 线程）无锁遍历调用回调，并发时遍历到已 free 节点即崩溃。
     * 修复：锁内查找并拷贝匹配到的回调/arg（或确认无匹配），锁外调用回调。 */
    ssn_client_msg_handler_t match_cb = NULL;
    void *match_arg = NULL;
    bool matched = false;

    ipc_mutex_lock(client->lock);
    for (h = client->sub_handlers; h; h = h->next) {
        if (h->url_len == url.url_len &&
            memcmp(h->url, url.url, url.url_len) == 0) {
            /* 订阅时允许传 NULL 回调（仅登记订阅）：此时消息未被处理，
             * 回退到 onmsg（ssn_client_set_on_message 设置的兜底回调） */
            if (h->callback) {
                match_cb = h->callback;
                match_arg = h->arg;
                matched = true;
            }
            break;
        }
    }
    ipc_mutex_unlock(client->lock);

    if (matched && match_cb) {
        match_cb(client, &url, &data, match_arg);
        return true;
    }

    /* Fallback: call the error/unhandled-message callback */
    if (client->onmsg) {
        client->onmsg(client, &url, &data, client->msg_arg);
    }

    return true;
}

static bool ssn_client_handle_message(ssn_client_t *client, ssn_header_t *ipc_hdr)
{
    ssn_url_ref_t url;
    ssn_data_ref_t data;
    
    ssn_get_url(ipc_hdr, &url);
    ssn_get_data(ipc_hdr, &data);
    
    LOG_DEBUG("ssn client input: get message msg.");
    if (client->onmsg) {
        client->onmsg(client, &url, &data, client->msg_arg);
    }
    
    return true;
}

static bool ssn_client_handle_response(ssn_client_t *client, ssn_header_t *ipc_hdr, ssn_pending_request_t *pendq)
{
    if (pendq->ftype == SSN_CLIENT_FTYPE_RES) {
        if (pendq->callback.res) {
            pendq->callback.res(client, ssn_get_status(ipc_hdr) == 0, pendq->arg);
        }
    }
    
    return true;
}

static bool ssn_client_handle_rpc_response(ssn_client_t *client, ssn_header_t *ipc_hdr, ssn_pending_request_t *pendq)
{
    ssn_data_ref_t data;
    
    if (pendq->ftype == SSN_CLIENT_FTYPE_RPC) {
        if (pendq->callback.rpc) {
            ssn_get_data(ipc_hdr, &data);
            pendq->callback.rpc(client, ipc_hdr, &data, pendq->arg);
        }
    }
    
    return true;
}

static bool ssn_client_input(ssn_header_t *ipc_hdr, void *varg)
{
    ssn_client_t *client = (ssn_client_t *)varg;
    ssn_pending_request_t *pendq;
    uint16_t seqno;

    if (ipc_hdr->msg_type == SSN_MSG_TYPE_PUBLISH) {
        ssn_client_handle_publish(client, ipc_hdr);
        goto out;
    } else if (ipc_hdr->msg_type == SSN_MSG_TYPE_MESSAGE) {
        ssn_client_handle_message(client, ipc_hdr);
        goto out;
    }

    seqno = ssn_get_seqno(ipc_hdr);
    /* 用快照函数替代裸指针返回（避免解锁后 pending 槽被并发释放的 TOCTOU） */
    {
        ssn_pending_request_t snapshot;
        if (!get_pending_snapshot(client, seqno, &snapshot)) {
            goto out;
        }
        pendq = &snapshot;

        switch (ipc_hdr->msg_type) {

        case SSN_MSG_TYPE_SUBSCRIBE:
        case SSN_MSG_TYPE_UNSUBSCRIBE:
        case SSN_MSG_TYPE_PING_ECHO:
            ssn_client_handle_response(client, ipc_hdr, pendq);
            break;

        case SSN_MSG_TYPE_RPC_REQUEST:
            ssn_client_handle_rpc_response(client, ipc_hdr, pendq);
            break;

        default:
            break;
        }

        // 加锁保护 free_pending_index 操作
        ipc_mutex_lock(client->lock);
        free_pending_index(client, pendq->index);
        ipc_mutex_unlock(client->lock);
        LOG_DEBUG("ssn client input free seqno pend %d, index %d.", pendq->seqno, pendq->index);
    }

    LOG_DEBUG("ssn client input finished.");

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
static bool ssn_client_process_events (ssn_client_t *client, const fd_set *rfds)
{
    /* pkt_e 必须初始化（缺陷背景：未初始化 UB——socket 无数据（did_recv=false）
     * 时 pkt_e 读栈垃圾值，可能为 true 导致误判「连接丢失」断开，触发 cliauto
     * 连接建立后 ~50ms 循环重连（Issue #22）） */
    bool pkt_e = false;
    ssize_t num;
    ssn_header_t *ipc_hdr;
    ssn_pending_request_t *pendq;

    if (!client || !client->valid) {
        LOG_DEBUG("ssn client process event failed: invalid client handle.");
        return (false);
    }

    if (client->connected) 
    {
        /* 锁内读取 transport 并 recv：与 connect/send_timeout 的 transport 销毁/
         * 替换互斥，避免事件线程 recv 已释放 transport 的 UAF（锁外回调）。 */
        int sock_fd = -1;
        bool did_recv = false;   /* 是否真正执行了 recv（FD_ISSET 命中） */
        num = -1;
        ipc_mutex_lock(client->lock);
        if (client->transport) {
            sock_fd = ssn_transport_get_fd(client->transport);
            if (sock_fd >= 0 && FD_ISSET(sock_fd, rfds)) {
                did_recv = true;
                num = ssn_transport_recv(client->transport, client->recvbuf,
                                         SSN_MAX_PACKET_SIZE, 0);
            }
        }
        ipc_mutex_unlock(client->lock);

        if (num > 0) {
            pkt_e = false;
            if (!ssn_stream_feed(&client->recv, client->recvbuf,
                                num, ssn_client_input, client)) {
                LOG_ERROR("ssn client process event failed: stream feed failed.");
                pkt_e = true;
            }
        } else if (did_recv && num == 0) {
            pkt_e = true;   /* 对端关闭 */
        }

        if (pkt_e) {
            // Connection closed or stream error
            client->connected = false;
            ipc_memory_barrier();

            ssn_client_timeout_all(client);
            LOG_ERROR("ssn client process event failed: connection lost.");
            return (false);
        } else if (did_recv && num < 0 &&
                   errno != EAGAIN && errno != EWOULDBLOCK) {
            // Real error (not just "no data available yet")
            client->connected = false;
            ipc_memory_barrier();

            ssn_client_timeout_all(client);
            LOG_ERROR("ssn client process event failed: recv error %s.", strerror(errno));
            return (false);
        }
    }

    int evt_fd = ipc_event_pair_get_read_fd(client->evtfd);
    if (FD_ISSET(evt_fd, rfds)) 
    {
        ipc_event_pair_drain(client->evtfd);

        /* 缺陷背景：原实现持 client->lock 遍历超时项并直接调用用户回调，回调内
         * 调用任何 ssn_client_* API 会自锁死锁。修复：锁内收集超时项并清位图，
         * 解锁后由 ssn_client_timeout_all 在锁外回调（与超时路径共用）。 */
        struct timeout_item {
            uint32_t ftype;
            ssn_client_callback_u callback;
            void *arg;
        } items[SSN_CLIENT_MAX_PENDING];
        int n_items = 0;

        ipc_mutex_lock(client->lock);
        for (int i = 0 ; i < SSN_CLIENT_MAX_PENDING ; i++) {
            if (is_bit_set(client->pending_bitmap, i)) {
                pendq = &client->pending_pool[i];
                if (pendq->timeout_ms == 0) {
                    if (n_items < SSN_CLIENT_MAX_PENDING) {
                        items[n_items].ftype = pendq->ftype;
                        items[n_items].callback = pendq->callback;
                        items[n_items].arg = pendq->arg;
                        n_items++;
                    }
                    free_pending_index(client, pendq->index);
                }
            }
        }
        ipc_mutex_unlock(client->lock);

        /* 锁外回调：回调内可安全调用 ssn_client_call/disconnect 等 API */
        for (int i = 0; i < n_items; i++) {
            if (items[i].ftype == SSN_CLIENT_FTYPE_RPC && items[i].callback.rpc) {
                items[i].callback.rpc(client, NULL, NULL, items[i].arg);
            } else if (items[i].ftype == SSN_CLIENT_FTYPE_RES && items[i].callback.res) {
                items[i].callback.res(client, false, items[i].arg);
            }
        }
    }

    return (true);
}

/**
 * @brief 准备一个非队列序列号
 * 
 * @param client 客户端实例指针
 * @return 非队列序列号
 */
static uint16_t ssn_client_prepare_seqno (ssn_client_t *client)
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
static int alloc_pending_index (ssn_client_t *client)
{
    // 注意：此函数假设调用者已经持有 client->lock 锁
    for (int w = 0; w < SSN_CLIENT_MAX_PENDING_BITMAP; w++) {
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
 * @param[out] out_seqno 可选：登记成功的请求 seqno（用于调用方主动撤销）
 * @return 发送成功返回true，失败返回false
 */
static bool ssn_client_request_ex (ssn_client_t *client, uint8_t type, 
                                   const ssn_url_ref_t *url, const ssn_data_ref_t *data,
                                   ssn_client_result_handler_t callback, void *arg,
                                   uint64_t timeout_ms, uint16_t *out_seqno)
{
    uint16_t seqno;
    ssn_header_t *ipc_hdr;
    ssn_pending_request_t *pendq;

    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ssn client request: invalid client handle.");
        return (false);
    }

    // 增加引用计数
    ssn_client_ref(client);

    /* 缺陷背景：原实现在加锁前执行 alloc_pending_index + seqno++ +
     * seqno_to_index 登记——与定时器线程（锁内改 pendq->timeout_ms）、并发
     * subscribe/call 竞态：槽位重复分配、seqno 重复 → 应答串线/误回调；
     * pending 池满时 return false 未 unref → 引用计数泄漏。修复：登记整体
     * 移入锁内（与 ssn_client_call_ex 对齐），失败路径补 unref。 */
    ipc_mutex_lock(client->lock);

    if (callback) {
        int index = alloc_pending_index(client);
        if (index < 0) {
            ipc_mutex_unlock(client->lock);
            LOG_ERROR("ssn client request: prepare pendq failed.");
            ssn_client_unref(client);
            return (false);
        }
        pendq = &client->pending_pool[index];
        pendq->callback.res = callback;
        seqno = pendq->seqno = client->seqno++;
        pendq->timeout_ms = timeout_ms;
        /* RES 类请求（SUBSCRIBE/UNSUBSCRIBE/PING_ECHO）统一登记为 FTYPE_RES，
         * 使 ssn_client_handle_response 能按结果回调分发（缺陷背景：原实现
         * ftype=type，PING_ECHO(0xFF)≠FTYPE_RES(2) 导致 ping 应答永不触发回调） */
        pendq->ftype = SSN_CLIENT_FTYPE_RES;
        pendq->arg = arg;
        client->seqno_to_index[seqno] = index;
    } else {
        pendq = NULL;
        seqno = ssn_client_prepare_seqno(client);
    }

    ipc_hdr = ssn_create_header(client->sendbuf, type, 0, seqno);

    if (!ssn_client_sendmsg(client, ipc_hdr, url, data)) {
        LOG_ERROR("ssn client request: sendmsg failed.");
        if (pendq) {
            free_pending_index(client, pendq->index);   /* 已在锁内 */
        }
        ipc_mutex_unlock(client->lock);
        ssn_client_unref(client);
        return (false);
    }

    ipc_mutex_unlock(client->lock);

    if (out_seqno) {
        *out_seqno = seqno;
    }

    LOG_DEBUG("ssn client request success.");
    ssn_client_unref(client);
    return (true);
}

/* 无 seqno 输出的薄封装（subscribe/unsubscribe 等无需主动撤销的路径） */
static bool ssn_client_request (ssn_client_t *client, uint8_t type, 
                                const ssn_url_ref_t *url, const ssn_data_ref_t *data,
                                ssn_client_result_handler_t callback, void *arg, uint64_t timeout_ms)
{
    return ssn_client_request_ex(client, type, url, data, callback, arg, timeout_ms, NULL);
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
bool ssn_client_subscribe (ssn_client_t *client, const ssn_url_ref_t *url,
                            ssn_client_msg_handler_t callback, void *arg, uint64_t timeout_ms)
{
    ssn_sub_handler_t *h;

    /* 缺陷背景：原实现先判 client 再在日志参数中解引用 url（未判空）——
     * 未连接时传 url=NULL 会在 LOG_ERROR 的 %.*s 参数求值处空指针崩溃。
     * 修复：url 判空前置。 */
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ssn client subscribe failed: invalid url.");
        return (false);
    }
    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ssn client subscribe to '%.*s' failed: client not connected.", (int)url->url_len, url->url);
        return (false);
    }

    /* Register per-URL handler（加锁保护：与 poll 线程的 handle_publish 遍历互斥） */
    h = (ssn_sub_handler_t *)malloc(sizeof(ssn_sub_handler_t) + url->url_len);
    if (!h) return false;
    h->callback  = callback;
    h->arg       = arg;
    h->url_len   = url->url_len;
    memcpy(h->url, url->url, url->url_len);
    h->url[url->url_len] = '\0';
    ipc_mutex_lock(client->lock);
    h->next = client->sub_handlers;
    client->sub_handlers = h;
    ipc_mutex_unlock(client->lock);

    /* Send SUBSCRIBE request to server */
    return (ssn_client_request(client, SSN_MSG_TYPE_SUBSCRIBE, url, NULL, NULL, NULL, timeout_ms));
}

bool ssn_client_unsubscribe (ssn_client_t *client, const ssn_url_ref_t *url,
                             uint64_t timeout_ms)
{
    ssn_sub_handler_t **prev, *h;

    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ssn client unsubscribe failed: invalid client handle.");
        return (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ssn client unsubscribe failed: invalid url.");
        return (false);
    }

    /* Remove per-URL handler（加锁保护：与 poll 线程的 handle_publish 遍历互斥） */
    ipc_mutex_lock(client->lock);
    prev = &client->sub_handlers;
    while ((h = *prev)) {
        if (h->url_len == url->url_len &&
            memcmp(h->url, url->url, url->url_len) == 0) {
            *prev = h->next;
            free(h);
            break;
        }
        prev = &h->next;
    }
    ipc_mutex_unlock(client->lock);

    return (ssn_client_request(client, SSN_MSG_TYPE_UNSUBSCRIBE, url, NULL, NULL, NULL, timeout_ms));
}

/* 保活 ping 应答回调：arg 指向 volatile bool（由 ssn_client_ping 轮询置位） */
static void ping_reply_cb(ssn_client_t *client, bool ok, void *arg)
{
    (void)client;
    volatile bool *replied = (volatile bool *)arg;
    *replied = ok;
}

/**
 * @brief 保活 ping（半开连接检测）
 * 
 * 发送 PING_ECHO（服务端原样回显同 seqno），并同步轮询等待应答。
 * 返回 true 表示应答已收到（连接存活）；false 表示未连接/发送失败/超时无应答。
 * 注意：本函数内部会 poll，调用方应避免在持有 client 锁的上下文调用。
 * 
 * @param client 客户端实例指针
 * @param timeout_ms 等待应答窗口（毫秒）
 * @return 连接存活返回 true
 */
bool ssn_client_ping(ssn_client_t *client, uint64_t timeout_ms)
{
    volatile bool replied = false;

    if (!client || !client->valid || !client->connected) {
        return false;
    }

    /* 缺陷背景：原实现把栈上 replied 的地址作为回调 arg 登记 pending，等待窗口
     * 结束后 pending 仍留在位图（仅定时器置超时、下次 poll 才触发回调并释放），
     * 迟到应答/超时回调会向已返回函数的栈帧写入 → 栈污染（UB）。修复：等待结束
     * 时主动撤销 pending 登记，杜绝回调再访问栈地址。 */
    uint16_t ping_seqno = 0;
    bool registered = ssn_client_request_ex(client, SSN_MSG_TYPE_PING_ECHO, NULL, NULL,
                                            ping_reply_cb, (void *)&replied, timeout_ms,
                                            &ping_seqno);
    if (!registered) {
        return false;
    }

    /* 同步等待应答（cliauto 单线程模型：本函数在其自有线程调用） */
    uint64_t waited = 0;
    while (!replied && waited < timeout_ms) {
        ssn_client_poll(client, 10);
        waited += 10;
    }

    if (!replied) {
        /* 超时无应答：主动撤销 pending 登记（锁内清位图与 seqno 映射），
         * 之后迟到的应答/超时回调因查不到 seqno 被丢弃，不再触碰栈上 replied */
        ipc_mutex_lock(client->lock);
        if (client->seqno_to_index[ping_seqno] != 0xFFFF) {
            uint16_t idx = client->seqno_to_index[ping_seqno];
            if (is_bit_set(client->pending_bitmap, idx)) {
                free_pending_index(client, idx);
            }
        }
        ipc_mutex_unlock(client->lock);
    }

    return replied;
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
static int ssn_client_call_ex (ssn_client_t *client, const ssn_url_ref_t *url, const ssn_data_ref_t *data,
                        ssn_client_rpcreply_handler_t callback, void *arg, uint64_t timeout_ms, void *arg_ex)
{
    size_t len;
    uint8_t flag;
    uint16_t seqno;
    ssn_header_t *ipc_hdr;
    ssn_pending_request_t *pendq;

    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ssn client call failed: invalid client handle.");
        return -1;
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ssn client call failed: invalid url.");
        return -1;
    }

    // 增加引用计数
    ssn_client_ref(client);

    ipc_mutex_lock(client->lock);
    
    if (callback) {
        int index = alloc_pending_index(client);
        if (index < 0) {
            ipc_mutex_unlock(client->lock);
            LOG_ERROR("ssn client call failed: prepare pendq failed");
            ssn_client_unref(client);
            return -1;
        }
        pendq = &client->pending_pool[index];
        seqno = pendq->seqno = client->seqno++;
        pendq->callback.rpc = callback;
        pendq->timeout_ms = timeout_ms;
        pendq->ftype = SSN_CLIENT_FTYPE_RPC;
        pendq->arg = arg;
        client->seqno_to_index[seqno] = index;
    } else {
        pendq = NULL;
        seqno = ssn_client_prepare_seqno(client);
    }

    ipc_hdr = ssn_create_header(client->sendbuf, SSN_MSG_TYPE_RPC_REQUEST, 0, seqno);

    /* 缺陷背景：原实现在此先解锁再 sendmsg——sendmsg 内部无锁读
     * client->transport，与 poll 线程销毁 transport（连接丢失）竞争 UAF。
     * 修复：sendmsg 在锁内执行（与 ssn_client_request/message 一致）。 */
    if (!ssn_client_sendmsg(client, ipc_hdr, url, data)) {
        LOG_ERROR("ssn client call failed: send msg failed.");
        if (pendq) {
            free_pending_index(client, pendq->index);
        }
        ipc_mutex_unlock(client->lock);
        ssn_client_unref(client);
        return -1;
    }

    ipc_mutex_unlock(client->lock);

    LOG_DEBUG("ssn client call success.");

    ssn_client_unref(client);
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
int ssn_client_call (ssn_client_t *client, const ssn_url_ref_t *url, const ssn_data_ref_t *data,
                    ssn_client_rpcreply_handler_t callback, void *arg, uint64_t timeout_ms)
{
    return (ssn_client_call_ex(client, url, data, callback, arg, timeout_ms, NULL));
}

/**
 * @brief 发送消息到服务器
 * 
 * @param client 客户端实例指针
 * @param url URL引用
 * @param data 数据引用
 * @return 发送成功返回0，失败返回-1
 */
int ssn_client_message (ssn_client_t *client, const ssn_url_ref_t *url, const ssn_data_ref_t *data)
{
    bool ret;
    size_t len;
    ssn_header_t *ipc_hdr;

    if (!client || !client->valid || !client->connected) {
        LOG_ERROR("ssn client message: invalid client handle.");
        return -1;
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ssn client message: invalid url.");
        return -1;
    }
    if (!data) {
        LOG_ERROR("ssn client message: invalid data.");
        return -1;
    }

    // 增加引用计数
    ssn_client_ref(client);

    ipc_mutex_lock(client->lock);

    ipc_hdr = ssn_create_header(client->sendbuf, SSN_MSG_TYPE_MESSAGE, 0, 0);

    ret = ssn_client_sendmsg(client, ipc_hdr, url, data);

    ipc_mutex_unlock(client->lock);

    if (ret) {
        LOG_DEBUG("ssn client message success.");
        ssn_client_unref(client);
        return 0;
    } else {
        LOG_ERROR("ssn client message failed.");
        ssn_client_unref(client);
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
void ssn_client_set_on_message (ssn_client_t *client, ssn_client_msg_handler_t callback, void *arg)
{
    if (!client || !client->valid) {
        LOG_ERROR("ssn client set on message: invalid client handle.");
        return;
    }

    // 增加引用计数
    ssn_client_ref(client);

    client->onmsg = callback;
    client->onsub = callback;
    client->msg_arg  = arg;
    client->sub_arg  = arg;

    // 减少引用计数
    ssn_client_unref(client);
}

void ssn_client_set_on_publish(ssn_client_t *client, ssn_client_msg_handler_t callback, void *arg)
{
    if (!client || !client->valid) {
        LOG_ERROR("ssn client set on publish: invalid client handle.");
        return;
    }
    ssn_client_ref(client);
    client->onsub   = callback;
    client->sub_arg = arg;
    ssn_client_unref(client);
}

/**
 * @brief 轮询客户端事件
 * 
 * @param client 客户端实例指针
 * @param timeout_ms 超时时间
 * @return 成功返回0，失败返回-1
 */
int ssn_client_poll(ssn_client_t *client, uint64_t timeout_ms)
{
    int max_fd, cnt;
    fd_set fds;
    sigset_t empty_mask;
    struct timespec timeout = { timeout_ms / 1000, (timeout_ms % 1000) * 1000000LL };

    if (!client || !client->valid) {
        LOG_ERROR("ssn client poll: invalid client handle.");
        return -1;
    }

    // 增加引用计数
    ssn_client_ref(client);

    FD_ZERO(&fds);
    max_fd = ssn_client_fds(client, &fds);

    sigemptyset(&empty_mask);

    // 阻塞空信号集，可以传递并中断所有信号
    cnt = pselect(max_fd + 1, &fds, NULL, NULL, &timeout, &empty_mask);
    if (cnt > 0) {
        if (!ssn_client_process_events(client, &fds)) {
            /* Connection lost but keep client valid so auto-client can reconnect */
            client->connected = false;
            ipc_memory_barrier();
            /* 缺陷背景：原实现无锁销毁 transport，与 ssn_client_connect 重建时
             * 无锁赋值 transport 竞争——poll 线程销毁的可能是 connect 线程刚
             * 创建/正在使用的 transport（UAF），后续 get_fd 读到垃圾 fd 触发
             * glibc fd_set 越界 abort（稳定性套件 T6 在负载下偶发复现）。
             * 修复：销毁/替换统一持 client->lock（与 connect/fds/process_events
             * 的锁内读 transport 互斥）。 */
            ipc_mutex_lock(client->lock);
            if (client->transport) {
                ssn_transport_destroy(client->transport);
                client->transport = NULL;
            }
            ipc_mutex_unlock(client->lock);
            LOG_ERROR("ssn client poll: connection of client %d lost", client->cid);
        }
        cnt = 0;
    }

    // 减少引用计数
    ssn_client_unref(client);
    return cnt;
}

/**
 * @brief 运行客户端事件循环
 * 
 * @param client 客户端实例指针
 */
void ssn_client_run(ssn_client_t *client)
{
    int max_fd, cnt;
    fd_set fds;
    sigset_t empty_mask;

    if (!client || !client->valid) {
        LOG_ERROR("ssn client run: invalid client handle.");
        return;
    }

    // 增加引用计数
    ssn_client_ref(client);

    sigemptyset(&empty_mask);

    while(true) {
        
        FD_ZERO(&fds);
        max_fd = ssn_client_fds(client, &fds);
        if (max_fd < 0) break;

        cnt = pselect(max_fd + 1, &fds, NULL, NULL, NULL, &empty_mask);
        if (cnt > 0) {
            if (!ssn_client_process_events(client, &fds)) {
                ssn_client_close(client);
                LOG_ERROR("ssn client run: connection of client %d lost", client->cid);
                // 减少引用计数
                ssn_client_unref(client);
                return;
            }
        }
    }
    LOG_ERROR("ssn client run: exit invalid.");
    
    // 减少引用计数
    ssn_client_unref(client);
}

/*
* end
*/
