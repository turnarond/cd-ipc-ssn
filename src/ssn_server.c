/*
 * IPC server
 */
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "ssn_list.h"
#include "ssn_server.h"
#include "ssn_frame.h"
#include "ssn_error.h"
#include "ssn_global.h"
#include "util/ssn_log.h"
#include "transports/ssn_transport.h"
#include "protocol/ssn_protocol.h"
#include "protocol/rpc/ssn_rpc.h"
#include "protocol/pubsub/ssn_pubsub.h"
#include "protocol/msg/ssn_msg.h"

/* Client hash */
#define IPC_CLI_HASH_SIZE  64
#define IPC_CLI_HASH_MASK  (IPC_CLI_HASH_SIZE - 1)

/* Command hash */
#define IPC_CMD_HASH_SIZE  32
#define IPC_CMD_HASH_MASK  (IPC_CMD_HASH_SIZE - 1)

/* Subscription node */
typedef struct ssn_server_sub {
    struct ssn_server_sub *next;
    struct ssn_server_sub *prev;
    size_t len;
    char url[1];
} ssn_server_sub_t;

/* Client handshake timer */
typedef struct ssn_server_hst {
    struct ssn_server_hst *next;
    struct ssn_server_hst *prev;
    int alive;
} ssn_server_hst_t;

/* Client node */
typedef struct ssn_server_cli {
    bool active;
    bool onconn;
    struct ssn_server_cli *next;
    struct ssn_server_cli *prev;
    ssn_server_sub_t *subscribed;
    ssn_server_hst_t hst;
    ssn_stream_ctx_t recv;
    ssn_transport_t *transport;
    ssn_peer_id_t id;
} ssn_server_cli_t;

/* Server command */
typedef struct ssn_server_cmd {
    struct ssn_server_cmd *next;
    struct ssn_server_cmd *prev;
    ssn_server_rpc_handler_t onrpc;
    void *arg;
    size_t len;
    char url[1];
} ssn_server_cmd_t;

/* Server */
struct ssn_server {
    bool valid;
    int ref_count;   /* 引用计数：poll/API 调用期间保活，destroy 延迟到引用归零 */
    char ifname[IF_NAMESIZE];
    char srv_name[SRV_NAME_LEN];
    ssn_peer_id_t ncid;
    ssn_server_t *next;
    ssn_server_t *prev;
    ssn_server_hst_t *hst_h;
    ssn_server_cli_t *clis[IPC_CLI_HASH_SIZE];
    ssn_server_cmd_t *cmds[IPC_CMD_HASH_SIZE];
    ssn_server_cmd_t *def_cmd;
    ssn_server_cmd_t *prefix_h;
    ssn_server_cmd_t *prefix_t;
    ssn_server_msg_handler_t onmsg;
    void *msg_arg;
    ssn_on_connect_t oncli;
    void *carg;
    ipc_mutex_t *lock;
    int send_timeout;
    int handshake_timeout;
    int keepalive_timeout;
    ssn_transport_t *transport;
    ipc_event_pair_t *evtfd;
    void *sendbuf;
    void *recvbuf;

    /* 协议层实例 */
    ssn_rpc_rep_t *rpc_rep;
    ssn_pubsub_pub_t *pubsub_pub;
    ssn_msg_recv_t *msg_recv;
};

/* 前向声明（引用计数机制：destroy 延迟释放） */
static void ssn_server_free_resources(ssn_server_t *server);
static void ssn_server_unref(ssn_server_t *server);

/* Input argument */
struct input_arg {
    ssn_server_t *server;
    ssn_server_cli_t *cli;
};

/*
 * Remote client hash
 */
#define ssn_server_cli_hash(id)  (int)(id & IPC_CLI_HASH_MASK)

/*
 * Server timer thread handle
 */
void *ssn_server_timer_handle(void *arg)
{
    bool emit;
    ssn_server_t *server;
    ssn_server_hst_t *hst;

    (void)arg;

    do {
        ipc_thread_msleep(IPC_TIMER_PERIOD);

        /* 进程退出时由 ssn_global_cleanup 置位退出标志，确保线程可退出（join 不阻塞） */
        if (__atomic_load_n(&g_ssn_server_timer_exit, __ATOMIC_ACQUIRE)) {
            break;
        }

        ipc_mutex_lock(g_ssn_server_lock);

        if (!g_ssn_server_list) {
            ipc_mutex_unlock(g_ssn_server_lock);
            continue;   // 空列表时继续轮询而不是退出：定时器线程启动后只执行一次，
                        // 若首个 50ms 内无存活 server 即 break，之后 idle/握手超时不再被处理
        }

        LIST_FOREACH(server, g_ssn_server_list) {
            if (!server->hst_h) {
                continue;
            }

            emit = false;

            ipc_mutex_lock(server->lock);

            LIST_FOREACH(hst, server->hst_h) {
                if (hst->alive <= IPC_TIMER_PERIOD) {
                    hst->alive = 0;
                    emit = true;
                } else {
                    hst->alive -= IPC_TIMER_PERIOD;
                }
            }

            ipc_mutex_unlock(server->lock);

            if (emit) {
                ipc_event_pair_signal(server->evtfd);
            }
        }

        ipc_mutex_unlock(g_ssn_server_lock);

    } while (true);

    ipc_thread_exit();

    return NULL;
}

/*
 * Command hash
 */
static int ssn_server_url_hash(const ssn_url_ref_t *url)
{
    int i, sum = 0;

    for (i = 0; i < url->url_len; i += 2) {
        sum += url->url[i];
    }

    return  (sum & IPC_CMD_HASH_MASK);
}

/*
 * Find client
 */
static ssn_server_cli_t *ssn_server_cli_find(ssn_server_t *server, ssn_peer_id_t id)
{
    int hash = ssn_server_cli_hash(id);
    ssn_server_cli_t *cli;

    LIST_FOREACH(cli, server->clis[hash]) {
        if (cli->id == id) {
            break;
        }
    }

    return  (cli);
}

/*
 * Assign new Client ID
 */
static ssn_peer_id_t ssn_server_cli_newid(ssn_server_t *server)
{
    ssn_peer_id_t id;

    do {
        id = server->ncid;
        server->ncid++;
    } while (ssn_server_cli_find(server, id));

    return  (id);
}

/*
 * Initialize a client
 */
static void ssn_server_cli_init(ssn_server_t *server, ssn_server_cli_t *cli)
{
    int hash;

    cli->id = ssn_server_cli_newid(server);
    hash = ssn_server_cli_hash(cli->id);
    INSERT_TO_HEADER(cli, server->clis[hash]);

    cli->hst.alive = server->handshake_timeout;
    INSERT_TO_HEADER(&cli->hst, server->hst_h);
    LOG_DEBUG("ssn server cli init success");
}

/*
 * Destroy a client
 */
static void ssn_server_cli_destroy(ssn_server_t *server, ssn_server_cli_t *cli)
{
    int hash = ssn_server_cli_hash(cli->id);
    ssn_server_sub_t *sub, *sub_temp;

    LIST_FOREACH_SAFE(sub, sub_temp, cli->subscribed) {
        DELETE_FROM_LIST(sub, cli->subscribed);
        free(sub);
    }

    DELETE_FROM_LIST(cli, server->clis[hash]);

    if (cli->hst.alive) {
        cli->hst.alive = 0;
        DELETE_FROM_LIST(&cli->hst, server->hst_h);
    }

    if (cli->transport) {
        ssn_transport_destroy(cli->transport);
        cli->transport = NULL;
    }
    free(cli);
    LOG_DEBUG("ssn server cli destroy success.");
}

/*
 * Close a client
 */
bool ssn_server_peer_close(ssn_server_t *server, ssn_peer_id_t id)
{
    bool ret;
    ssn_server_cli_t *cli;

    if (!server || !server->valid) {
        LOG_ERROR("ssn server peer close failed: invalid server handle.");
        return  (false);
    }

    ipc_mutex_lock(server->lock);

    cli = ssn_server_cli_find(server, id);
    if (cli) {
        if (cli->transport) {
            ssn_transport_disconnect(cli->transport);
        }
        ret = true;
    } else {
        ret = false;
    }

    ipc_mutex_unlock(server->lock);

    LOG_DEBUG("ssn server peer close success: cid is %d.", id);
    return  (ret);
}


/*
 * Client send
 */
static bool ssn_server_cli_sendmsg(ssn_server_cli_t *cli, ssn_header_t *ipc_hdr, 
    const ssn_url_ref_t *url, const ssn_data_ref_t *data)
{
    bool ret = ssn_send_message(cli->transport, ipc_hdr, url, data);
    if (!ret) {
        if (cli->transport) {
            ssn_transport_disconnect(cli->transport);
        }
        LOG_ERROR("ssn server sendmsg faield, cli %d", cli->id);
    }
    return ret;
}

/*
 * Client subscribe match
 */
static bool ssn_server_cli_sub_match(ssn_server_cli_t *cli, const ssn_url_ref_t *url)
{
    size_t path_len;
    ssn_server_sub_t *sub;

    LIST_FOREACH(sub, cli->subscribed) {
        if (sub->len == 1) {
            break;
        } else if (sub->len == url->url_len && !memcmp(sub->url, url->url, sub->len)) {
            break;
        } else if (sub->url[sub->len - 1] == '/') {
            path_len = sub->len - 1;
            if (url->url_len >= path_len && !memcmp(url->url, sub->url, path_len)) {
                if (url->url_len == path_len || url->url[path_len] == '/') {
                    break;
                }
            }
        }
    }

    return (sub ? true : false);
}

ssn_server_t *ssn_server_create_with_options(const char *name, const server_options_t *opts)
{
    ssn_server_t *server;

    if (name == NULL) {
        LOG_ERROR("ssn server create with options: invalid name.");
        return NULL;
    }

    server = (ssn_server_t *)calloc(1, sizeof(ssn_server_t));
    if (!server) {
        LOG_ERROR("ssn server create with options: calloc failed, errno is %d.", errno);
        return NULL;
    }

    server->transport = NULL;

    if (ipc_mutex_init(&server->lock)) {
        LOG_ERROR("ssn server create with options: init mutex failed, errno %d", errno);
        goto error;
    }

    if (ipc_event_pair_create(&server->evtfd) != 0) {
        LOG_ERROR("ssn server create with options: event pair create failed, errno %d", errno);
        goto error;
    }

    server->sendbuf = malloc(SSN_MAX_PACKET_SIZE * 2);
    if (!server->sendbuf) {
        LOG_ERROR("ssn server create with options: sendbuf malloc failed, errno is %d", errno);
        goto error;
    }

    if (opts) {
        server->send_timeout = opts->send_timeout_ms;
        /* conn_timeout_ms<=0（未设置/显式 0）时回退默认握手超时：原实现把 0 直接
         * 赋给 handshake_timeout，cli_init 的 hst.alive=0 使定时器首个 tick 即销毁
         * 握手中的连接（竞态窗口，客户端偶发连接失败，Issue #15） */
        server->handshake_timeout = (opts->conn_timeout_ms > 0)
                                    ? opts->conn_timeout_ms
                                    : IPC_SERVER_DEF_HANDSHAKE_TIMEOUT;
        server->keepalive_timeout = opts->idle_timeout_sec;
        if(opts->ifname[0]) {
            /* 用 snprintf 保证 NUL 终止并防越界（ifname 仅 IF_NAMESIZE 字节） */
            snprintf(server->ifname, sizeof(server->ifname), "%s", opts->ifname);
        }
    } else {
        server->send_timeout = IPC_DEF_SEND_TIMEOUT;
        server->handshake_timeout = IPC_SERVER_DEF_HANDSHAKE_TIMEOUT;
        server->keepalive_timeout = IPC_SERVER_KEEPALIVE_TIMEOUT;
    }

    /* 用 snprintf 保证 NUL 终止（srv_name 为 SRV_NAME_LEN 字节，strncpy 以
     * strlen 为 n 不追加 NUL，后续 strstr 越界读取） */
    snprintf(server->srv_name, sizeof(server->srv_name), "%s", name);
    server->recvbuf      = (uint8_t *)server->sendbuf + SSN_MAX_PACKET_SIZE;

    /* 创建协议层实例 */
    server->rpc_rep = ssn_rpc_rep_create(NULL, server);
    if (!server->rpc_rep) {
        LOG_ERROR("ssn server create: rpc_rep create failed");
        goto error;
    }

    server->pubsub_pub = ssn_pubsub_pub_create();
    if (!server->pubsub_pub) {
        LOG_ERROR("ssn server create: pubsub_pub create failed");
        goto error;
    }

    server->msg_recv = ssn_msg_recv_create(NULL, server);
    if (!server->msg_recv) {
        LOG_ERROR("ssn server create: msg_recv create failed");
        goto error;
    }

    server->valid        = true;
    server->ref_count    = 1;   /* 创建者持有的引用 */

    ipc_mutex_lock(g_ssn_server_lock);

    INSERT_TO_HEADER(server, g_ssn_server_list);

    ipc_mutex_unlock(g_ssn_server_lock);

    LOG_DEBUG("ssn server create with option success, name is %s", name);

    return  (server);

error:
    if (server->msg_recv) ssn_msg_destroy((ssn_protocol_ctx_t *)server->msg_recv);
    if (server->pubsub_pub) ssn_pubsub_destroy((ssn_protocol_ctx_t *)server->pubsub_pub);
    if (server->rpc_rep) ssn_rpc_destroy((ssn_protocol_ctx_t *)server->rpc_rep);
    if (server->sendbuf) free(server->sendbuf);
    if (server->evtfd) ipc_event_pair_destroy(server->evtfd);
    ipc_mutex_destroy(server->lock);
    free(server);
    LOG_ERROR("ssn server create with options: failed, errno %d", errno);
    return NULL;
}

/**
 * @brief 创建IPC服务器
 * 
 * @param server_info 服务器信息
 * @return 服务器实例指针，失败返回NULL
 * @warning 此函数必须与ssn_server_destroy()调用互斥
 */
ssn_server_t *ssn_server_create(const char *server_info)
{
    return ssn_server_create_with_options(server_info, NULL);
}

/**
 * @brief 启动IPC服务器
 * 
 * @param server 服务器实例指针
 * @return 启动成功返回true，失败返回false
 */
bool ssn_server_start(ssn_server_t *server)
{
    int en = 1;

    if (!server || !server->valid) {
        ssn_handle_error(SSN_ECODE_INVALID_ARGS, __FILE__, __LINE__, __func__, "invalid server handle");
        return  (false);
    }

    // 解析地址，自动判断地址类型
    char address_str[256];
    if (strstr(server->srv_name, "://") != NULL) {
        // srv_name已经包含协议前缀（unix://、tcp://、udp://等），直接使用
        snprintf(address_str, sizeof(address_str), "%s", server->srv_name);
    } else {
        // 默认作为Unix socket路径处理
        snprintf(address_str, sizeof(address_str), "unix://%s", server->srv_name);
    }
    ssn_address_t ssn_addr;
    if (!ssn_address_parse(address_str, &ssn_addr)) {
        ssn_handle_error(SSN_ECODE_NET_CONNECT, __FILE__, __LINE__, __func__, "parse address failed");
        return (false);
    }

    // 只有Unix Socket类型才需要检查和删除旧的文件
    if (ssn_addr.type == SSN_TRANSPORT_UNIX) {
        struct stat st_uds;
        if (stat(server->srv_name, &st_uds) == 0) {
            if (S_ISSOCK(st_uds.st_mode)) {
                unlink(server->srv_name);
                LOG_INFO("ssn server start: delete sock file %s.", server->srv_name);
            } else {
                LOG_ERROR("ssn server start: file %s is not a sock file.", server->srv_name);
                return false;
            }
        } else if (errno != ENOENT) {
            LOG_ERROR("ssn server start: stat file %s exist but failed, errno %d.", server->srv_name, errno);
            return false;
        }
    }

    // 创建transport配置
    /* non_blocking=true：发送为非阻塞，ssn_send_message 内部按 send_timeout_ms
     * 有界重试 EAGAIN。缺陷背景：原为阻塞发送且持 server->lock 调用，单个慢
     * 客户端（socket 缓冲满）可阻塞整个事件循环并令定时器线程（idle/握手超时）
     * 失效——非阻塞 + 有界重试消除该 DoS。 */
    ssn_transport_config_t config = {
        .non_blocking = true,
        .send_timeout_ms = server->send_timeout,
        .recv_timeout_ms = 1000,
        .connect_timeout_ms = server->handshake_timeout,
        .enable_keepalive = true,
        .keepalive_idle_sec = server->keepalive_timeout,
        .keepalive_interval_sec = 10,
        .keepalive_count = 3,
        .enable_nagle = false,
        .send_buffer_size = SSN_MAX_PACKET_SIZE,
        .recv_buffer_size = SSN_MAX_PACKET_SIZE,
        .reuse_address = true
    };

    // 根据地址类型设置配置和创建transport
    config.type = ssn_addr.type;
    server->transport = ssn_transport_create(ssn_addr.type, &config);
    if (!server->transport) {
        ssn_handle_error(SSN_ECODE_NET_CONNECT, __FILE__, __LINE__, __func__, "create transport failed");
        return (false);
    }

    // 绑定地址
    if (!ssn_transport_bind(server->transport, &ssn_addr)) {
        ssn_handle_error(SSN_ECODE_NET_CONNECT, __FILE__, __LINE__, __func__, "bind failed");
        goto error;
    }

    // 开始监听
    if (!ssn_transport_listen(server->transport, IPC_SERVER_BACKLOG)) {
        ssn_handle_error(SSN_ECODE_NET_CONNECT, __FILE__, __LINE__, __func__, "listen failed");
        goto error;
    }

    /* 绑定协议层实例到传输层 */
    ssn_rpc_bind((ssn_protocol_ctx_t *)server->rpc_rep, server->transport);
    ssn_pubsub_pub_bind(server->pubsub_pub, server->transport);
    ssn_msg_recv_bind(server->msg_recv, server->transport);

    LOG_DEBUG("ssn server start success.");

    return  (true);

error:
    if (server->transport) {
        ssn_transport_destroy(server->transport);
        server->transport = NULL;
    }

    LOG_ERROR("ssn server start failed.");

    return  (false);
}

/**
 * @brief 获取IPC服务器地址（必须在`ssn_server_start`之后调用）
 * 
 * @param server 服务器实例指针
 * @param addr 地址结构体
 * @param namelen 地址长度
 * @return 获取成功返回true，失败返回false
 */
int ssn_server_address(ssn_server_t *server, struct sockaddr *addr, socklen_t *namelen)
{
    if (!server || !server->valid || !server->transport) {
        LOG_ERROR("ssn server address: invalid server handle.");
        return  (false);
    }

    // 使用transport的get_address方法获取地址
    ssn_address_t ssn_addr;
    if (!ssn_transport_get_address(server->transport, &ssn_addr)) {
        LOG_ERROR("ssn server address: get_address failed");
        return  (false);
    }

    // 复制地址到输出参数
    if (ssn_addr.type == SSN_TRANSPORT_UNIX) {
        memcpy(addr, &ssn_addr.addr.unix_addr, sizeof(struct sockaddr_un));
        *namelen = sizeof(struct sockaddr_un);
    } else if (ssn_addr.type == SSN_TRANSPORT_TCP || ssn_addr.type == SSN_TRANSPORT_UDP) {
        memcpy(addr, &ssn_addr.addr.inet_addr, sizeof(struct sockaddr_in));
        *namelen = sizeof(struct sockaddr_in);
    } else if (ssn_addr.type == SSN_TRANSPORT_TCP6 || ssn_addr.type == SSN_TRANSPORT_UDP6) {
        memcpy(addr, &ssn_addr.addr.inet6_addr, sizeof(struct sockaddr_in6));
        *namelen = sizeof(struct sockaddr_in6);
    } else {
        LOG_ERROR("ssn server address: unsupported address type");
        return  (false);
    }

    LOG_DEBUG("ssn server address success.");
    return  (true);
}

/**
 * @brief 增加服务器引用计数（与 client 对称：poll/API 调用期间保活）
 */
static void ssn_server_ref(ssn_server_t *server)
{
    if (!server) return;
    ipc_mutex_lock(server->lock);
    server->ref_count++;
    ipc_mutex_unlock(server->lock);
}

/**
 * @brief 减少服务器引用计数，归零且 invalid 时真正释放
 */
static void ssn_server_unref(ssn_server_t *server)
{
    if (!server) return;

    bool should_free = false;
    ipc_mutex_lock(server->lock);
    if (server->ref_count > 0) {
        server->ref_count--;
        if (server->ref_count == 0 && !server->valid) {
            should_free = true;
        }
    }
    ipc_mutex_unlock(server->lock);

    if (should_free) {
        ssn_server_free_resources(server);
    }
}

/**
 * @brief 真正释放服务器资源（引用归零且 invalid 后调用）
 */
static void ssn_server_free_resources(ssn_server_t *server)
{
    int i;
    ssn_server_cli_t *cli, *cli_temp;
    ssn_server_cmd_t *cmd, *cmd_temp;

    if (!server) return;

    if (server->transport) {
        ssn_transport_destroy(server->transport);
        server->transport = NULL;
    }

    ipc_event_pair_destroy(server->evtfd);
    free(server->sendbuf);

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH_SAFE(cli, cli_temp, server->clis[i]) {
            ssn_server_cli_destroy(server, cli);
        }
    }

    for (i = 0; i < IPC_CMD_HASH_SIZE; i++) {
        LIST_FOREACH_SAFE(cmd, cmd_temp, server->cmds[i]) {
            DELETE_FROM_LIST(cmd, server->cmds[i]);
            free(cmd);
        }
    }

    LIST_FOREACH_SAFE(cmd, cmd_temp, server->prefix_h) {
        DELETE_FROM_LIST(cmd, server->prefix_h);
        free(cmd);
    }

    if (server->def_cmd) {
        free(server->def_cmd);
    }

    ipc_mutex_destroy(server->lock);

    unlink(server->srv_name);

    free(server);
    LOG_DEBUG("ssn server free success.");
}

/**
 * @brief 关闭IPC服务器
 * 
 * @param server 服务器实例指针
 * @warning 此函数必须与ssn_server_create()调用互斥
 */
void ssn_server_destroy(ssn_server_t *server)
{
    int i;
    ssn_server_cli_t *cli, *cli_temp;
    ssn_server_cmd_t *cmd, *cmd_temp;

    if (!server || !server->valid) {
        LOG_ERROR("ssn server destroy: invalid server handle.");
        return;
    }

    ipc_mutex_lock(g_ssn_server_lock);

    DELETE_FROM_LIST(server, g_ssn_server_list);

    ipc_mutex_unlock(g_ssn_server_lock);

    ipc_mutex_lock(server->lock);

    server->valid = false;
    ipc_memory_barrier();

    /* 销毁协议层实例 */
    if (server->msg_recv) {
        ssn_msg_destroy((ssn_protocol_ctx_t *)server->msg_recv);
        server->msg_recv = NULL;
    }
    if (server->pubsub_pub) {
        ssn_pubsub_destroy((ssn_protocol_ctx_t *)server->pubsub_pub);
        server->pubsub_pub = NULL;
    }
    if (server->rpc_rep) {
        ssn_rpc_destroy((ssn_protocol_ctx_t *)server->rpc_rep);
        server->rpc_rep = NULL;
    }

    ipc_mutex_unlock(server->lock);

    /* 引用计数归零且 invalid 时才真正释放（延迟释放：回调中调用 destroy 时
     * 正在 poll 的路径仍持有引用，可安全继续使用到 poll 结束） */
    ssn_server_unref(server);
}

/**
 * @brief 设置客户端连接回调函数
 * 
 * @param server 服务器实例指针
 * @param oncli 连接回调函数
 * @param arg 回调参数
 */
void ssn_server_set_connect_handler (ssn_server_t *server, ssn_on_connect_t oncli, void *arg)
{
    if (server) {
        server->oncli = oncli;
        server->carg  = arg;
    }
}

/**
 * @brief 获取远程客户端数量
 * 
 * @param server 服务器实例指针
 * @return 客户端数量
 */
int ssn_server_peer_count (ssn_server_t *server)
{
    int i, cnt = 0;
    ssn_server_cli_t *cli;

    if (!server || !server->valid) {
        LOG_ERROR("ssn server peer count: invalid server handle.");
        return  (0);
    }

    ipc_mutex_lock(server->lock);

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH(cli, server->clis[i]) {
            if (cli->active) {
                cnt++;
            }
        }
    }

    ipc_mutex_unlock(server->lock);

    LOG_DEBUG("ssn server peer count: count is %d.", cnt);

    return  (cnt);
}

/**
 * @brief 检查服务器是否被订阅
 * 
 * @param server 服务器实例指针
 * @param url URL引用
 * @return 已订阅返回true，未订阅返回false
 */
bool ssn_server_is_subscribed (ssn_server_t *server, const ssn_url_ref_t *url)
{
    ssn_server_cli_t *cli;
    int i;

    if (!server || !server->valid) {
        LOG_ERROR("ssn server is subscribe: invalid server handle.");
        return  (false);
    }
    if (!url || !url->url || !url->url_len) {
        LOG_ERROR("ssn server is subscribe: invalid url handle.");
        return  (false);
    }

    ipc_mutex_lock(server->lock);

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH(cli, server->clis[i]) {
            if (!cli->active) {
                continue;
            }
            if (ssn_server_cli_sub_match(cli, url)) {
                ipc_mutex_unlock(server->lock);
                LOG_DEBUG("ssn server is subscribed, true.");
                return  (true);
            }
        }
    }

    ipc_mutex_unlock(server->lock);

    LOG_DEBUG("ssn server is subscribed, false.");

    return  (false);
}

/**
 * @brief 服务器发布消息
 * 
 * @param server 服务器实例指针
 * @param url URL引用
 * @param data 数据引用
 * @return 发布成功返回true，失败返回false
 */
static bool ssn_server_do_publish (ssn_server_t *server, const ssn_url_ref_t *url, const ssn_data_ref_t *data)
{
    int i;
    size_t len;
    ssn_header_t *ipc_hdr;
    ssn_server_cli_t *cli;

    if (!server || !server->valid) {
        LOG_ERROR("ssn server publish: invalid server handle.");
        return  (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ssn server publish: invalid url.");
        return  (false);
    }

    ipc_mutex_lock(server->lock);

    ipc_hdr = ssn_create_header(server->sendbuf, SSN_MSG_TYPE_PUBLISH, 0, 0);

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH(cli, server->clis[i]) {
            if (!cli->active) {
                continue;
            }
            if (ssn_server_cli_sub_match(cli, url)) {
                ssn_server_cli_sendmsg(cli, ipc_hdr, url, data);
            }
        }
    }

    ipc_mutex_unlock(server->lock);

    LOG_DEBUG("ssn server publish success.");

    return  (true);
}

/**
 * @brief 服务器发布消息
 * 
 * @param server 服务器实例指针
 * @param url URL引用
 * @param data 数据引用
 * @return 发布成功返回true，失败返回false
 */
int ssn_server_publish (ssn_server_t *server, const ssn_url_ref_t *url, const ssn_data_ref_t *data)
{
    return  (ssn_server_do_publish(server, url, data));
}

/**
 * @brief 添加RPC监听器
 * 
 * @param server 服务器实例指针
 * @param url URL引用
 * @param callback 回调函数
 * @param arg 回调参数
 * @return 添加成功返回true，失败返回false
 */
bool ssn_server_add_method (ssn_server_t *server,
                               const ssn_url_ref_t *url, ssn_server_rpc_handler_t callback, void *arg)
{
    int hash;
    size_t path_len;
    bool def, prefix;
    ssn_server_cmd_t *cmd, *need_free = NULL;

    if (!server || !server->valid) {
        LOG_ERROR("ssn server add method: invalid server handle.");
        return  (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/' || !callback) {
        LOG_ERROR("ssn server add method: invalid url.");
        return  (false);
    }

    def = url->url_len == 1 ? true : false;
    if (!def && url->url[url->url_len - 1] == '/') {
        prefix   = true;
        path_len = url->url_len - 1;
    } else {
        prefix   = false;
        path_len = url->url_len;
    }

    cmd = (ssn_server_cmd_t *)calloc(1, sizeof(ssn_server_cmd_t) + url->url_len);
    if (!cmd) {
        LOG_ERROR("ssn server add method: calloc failed, errno is %d.", errno);
        return  (false);
    }

    cmd->onrpc = callback;
    cmd->arg = arg;
    cmd->len = path_len;
    memcpy(cmd->url, url->url, path_len);
    cmd->url[path_len] = '\0';

    ipc_mutex_lock(server->lock);

    if (def) {
        need_free = server->def_cmd;
        server->def_cmd = cmd;

    } else {
        if (prefix) {
            INSERT_TO_FIFO(cmd, server->prefix_h, server->prefix_t);
        } else {
            hash = ssn_server_url_hash(url);
            INSERT_TO_HEADER(cmd, server->cmds[hash]);
        }
    }

    ipc_mutex_unlock(server->lock);

    if (need_free) {
        free(need_free);
    }

    LOG_DEBUG("ssn server add method success.");

    return  (true);
}

/**
 * @brief 移除RPC监听器
 * 
 * @param server 服务器实例指针
 * @param url URL引用
 */
void ssn_server_remove_method (ssn_server_t *server, const ssn_url_ref_t *url)
{
    int hash;
    size_t path_len;
    bool def, prefix;
    ssn_server_cmd_t *cmd, *cmd_temp, **header;

    if (!server || !server->valid) {
        LOG_ERROR("ssn server remove method: invalid server handle.");
        return;
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ssn server remove method: invalid url.");
        return;
    }

    def = url->url_len == 1 ? true : false;
    if (!def && url->url[url->url_len - 1] == '/') {
        prefix   = true;
        path_len = url->url_len - 1;
    } else {
        prefix   = false;
        path_len = url->url_len;
    }

    ipc_mutex_lock(server->lock);

    if (def) {
        cmd = server->def_cmd;
        server->def_cmd = NULL;

    } else {
        if (prefix) {
            header = &server->prefix_h;
        } else {
            hash   = ssn_server_url_hash(url);
            header = &server->cmds[hash];
        }

        LIST_FOREACH_SAFE(cmd, cmd_temp, *header) {
            if (cmd->len == path_len && !memcmp(cmd->url, url->url, path_len)) {
                DELETE_FROM_LIST(cmd, *header);
                break;
            }
        }
    }

    ipc_mutex_unlock(server->lock);

    if (cmd) {
        free(cmd);
    }

    LOG_DEBUG("ssn server remove method %.*s success.", (int)url->url_len, url->url);
}

/**
 * @brief 获取远程客户端地址
 * 
 * @param server 服务器实例指针
 * @param id 客户端ID
 * @param addr 地址结构体
 * @param namelen 地址长度
 * @return 获取成功返回true，失败返回false
 */
int ssn_server_peer_address (ssn_server_t *server, ssn_peer_id_t id, struct sockaddr *addr, socklen_t *namelen)
{
    ssn_server_cli_t *cli;

    if (!server || !server->valid) {
        LOG_ERROR("ssn server peer address: invalid server handle.");
        return  (false);
    }

    ipc_mutex_lock(server->lock);

    cli = ssn_server_cli_find(server, id);
    if (!cli || !cli->transport) {
        ipc_mutex_unlock(server->lock);
        if (!cli) {
            LOG_ERROR("ssn server peer address: client not found for id %d.", id);
        } else {
            LOG_ERROR("ssn server peer address: invalid client handle %d.", cli->id);
        }
        return  (false);
    }

    // 使用transport的get_address方法获取地址
    ssn_address_t ssn_addr;
    if (!ssn_transport_get_address(cli->transport, &ssn_addr)) {
        ipc_mutex_unlock(server->lock);
        LOG_ERROR("ssn server peer address: get_address failed");
        return  (false);
    }

    // 复制地址到输出参数
    if (ssn_addr.type == SSN_TRANSPORT_UNIX) {
        memcpy(addr, &ssn_addr.addr.unix_addr, sizeof(struct sockaddr_un));
        *namelen = sizeof(struct sockaddr_un);
    } else if (ssn_addr.type == SSN_TRANSPORT_TCP || ssn_addr.type == SSN_TRANSPORT_UDP) {
        memcpy(addr, &ssn_addr.addr.inet_addr, sizeof(struct sockaddr_in));
        *namelen = sizeof(struct sockaddr_in);
    } else if (ssn_addr.type == SSN_TRANSPORT_TCP6 || ssn_addr.type == SSN_TRANSPORT_UDP6) {
        memcpy(addr, &ssn_addr.addr.inet6_addr, sizeof(struct sockaddr_in6));
        *namelen = sizeof(struct sockaddr_in6);
    } else {
        ipc_mutex_unlock(server->lock);
        LOG_ERROR("ssn server peer address: unsupported address type");
        return  (false);
    }

    ipc_mutex_unlock(server->lock);

    return  (true);
}

/**
 * @brief 服务器RPC响应
 * 
 * @param server 服务器实例指针
 * @param id 客户端ID
 * @param status 状态码
 * @param seqno 序列号
 * @param data 数据引用
 * @return 响应成功返回true，失败返回false
 */
int ssn_server_response (ssn_server_t *server, ssn_peer_id_t id,
                            uint32_t status, uint16_t seqno, const ssn_data_ref_t *data)
{
    bool ret;
    ssn_server_cli_t *cli;
    ssn_header_t *ipc_hdr;

    if (!server || !server->valid) {
        LOG_ERROR("ssn server response: invalid server handle.");
        return  (false);
    }

    ipc_mutex_lock(server->lock);

    cli = ssn_server_cli_find(server, id);
    if (!cli) {
        ipc_mutex_unlock(server->lock);
        LOG_ERROR("ssn server response: invalid cli %d handle.", id);
        return  (false);
    }

    ipc_hdr = ssn_create_header(server->sendbuf, SSN_MSG_TYPE_RPC_REQUEST, status, seqno);

    ret = ssn_server_cli_sendmsg(cli, ipc_hdr, NULL, data);

    ipc_mutex_unlock(server->lock);

    if (ret) {
        LOG_DEBUG("ssn server response success: cid %d.", id);
    } else {
        LOG_ERROR("ssn server response failed: cid %d.", id);
    }

    return  (ret);
}

/**
 * @brief 远程客户端心跳
 * 
 * @param server 服务器实例指针
 * @param id 客户端ID
 * @param keepalive 心跳时间
 * @return 设置成功返回true，失败返回false
 */
bool ssn_server_cli_keepalive (ssn_server_t *server, ssn_peer_id_t id, int keepalive)
{
    ssn_server_cli_t *cli;

    int count = 3, idle = server->keepalive_timeout;

    if (!server || !server->valid) {
        LOG_ERROR("ssn server cli keepalive: invalid server handle.");
        return  (false);
    }

    ipc_mutex_lock(server->lock);

    cli = ssn_server_cli_find(server, id);
    if (!cli || !cli->transport) {
        ipc_mutex_unlock(server->lock);
        LOG_ERROR("ssn server cli keepalive: invalid cli of id %d.", id);
        return  (false);
    }

    // 保活设置已在transport创建时配置

    ipc_mutex_unlock(server->lock);

    LOG_DEBUG("ssn server cli keepalive %d success.", id);

    return  (true);
}

/**
 * @brief 获取远程客户端ID列表
 * 
 * @param server 服务器实例指针
 * @param ids ID数组
 * @param max_cnt 最大数量
 * @return 实际获取的ID数量
 */
int ssn_server_peer_list (ssn_server_t *server, ssn_peer_id_t ids[], int max_cnt)
{
    int i, cnt;
    ssn_server_cli_t *cli;

    if (!server || !server->valid || !ids || max_cnt <= 0) {
        LOG_ERROR("ssn server peer list: invalid server handle.");
        return  (0);
    }

    cnt = 0;

    ipc_mutex_lock(server->lock);

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH(cli, server->clis[i]) {
            ids[cnt++] = cli->id;
            if (cnt >= max_cnt) {
                goto    out;
            }
        }
    }

    LOG_DEBUG("ssn server peer list success, cnt is %d.", cnt);

out:
    ipc_mutex_unlock(server->lock);

    return  (cnt);
}

/**
 * @brief 设置服务器发送数据包到客户端的超时时间
 * 
 * NULL表示拥塞时无限等待
 * 
 * @param server 服务器实例指针
 * @param id 客户端ID
 * @param timeout_ms 超时时间（毫秒）
 * @return 设置成功返回true，失败返回false
 */
bool ssn_server_cli_send_timeout (ssn_server_t *server, ssn_peer_id_t id, int timeout_ms)
{
    int timeval;
    ssn_server_cli_t *cli;

    if (!server || !server->valid) {
        LOG_ERROR("ssn server send timeout failed: invalid server handle.");
        return  (false);
    }

    if (timeout_ms > 0) {
        timeval = timeout_ms;
    } else {
        timeval = server->send_timeout;
    }

    ipc_mutex_lock(server->lock);

    cli = ssn_server_cli_find(server, id);
    
    if (cli && cli->transport) {
        // 发送超时已在transport创建时设置
    }
    ipc_mutex_unlock(server->lock);


    LOG_DEBUG("ssn server cli send timeout of cid %d success.", id);

    return  (cli ? true : false);
}

/**
 * @brief 服务器发送消息
 * 
 * @param server 服务器实例指针
 * @param id 客户端ID
 * @param url URL引用
 * @param data 数据引用
 * @return 发送成功返回true，失败返回false
 */
int ssn_server_cli_do_message (ssn_server_t *server, ssn_peer_id_t id, const ssn_url_ref_t *url, const ssn_data_ref_t *data)
{
    bool ret;
    size_t len;
    ssn_server_cli_t *cli;
    ssn_header_t *ipc_hdr;

    if (!server || !server->valid) {
        LOG_ERROR("ssn server do message: invalid server handle.");
        return  (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ssn server do message: invalid url.");
        return  (false);
    }
    if (!data) {
        LOG_ERROR("ssn server do message: invalid data.");
        return  (false);
    }

    ipc_mutex_lock(server->lock);

    cli = ssn_server_cli_find(server, id);
    if (!cli) {
        ipc_mutex_unlock(server->lock);
        LOG_ERROR("ssn server do message: not found cli %d.", id);
        return  (false);
    }

    ipc_hdr = ssn_create_header(server->sendbuf, SSN_MSG_TYPE_MESSAGE, 0, 0);

    ret = ssn_server_cli_sendmsg(cli, ipc_hdr, url, data);

    ipc_mutex_unlock(server->lock);

    if (ret) {
        LOG_DEBUG("ssn server do message to cid %d success.", id);
    } else {
        LOG_ERROR("ssn server do message to cid %d failed.", id);
    }

    return  (ret);
}

/**
 * @brief 服务器发送消息
 * 
 * @param server 服务器实例指针
 * @param id 客户端ID
 * @param url URL引用
 * @param data 数据引用
 * @return 发送成功返回true，失败返回false
 */
int ssn_server_message (ssn_server_t *server, ssn_peer_id_t id, const ssn_url_ref_t *url, const ssn_data_ref_t *data)
{
    return  (ssn_server_cli_do_message(server, id, url, data));
}

/**
 * @brief 设置消息处理回调函数
 * 
 * @param server 服务器实例指针
 * @param callback 消息处理回调函数
 * @param arg 回调参数
 */
void ssn_server_set_message_handler (ssn_server_t *server, ssn_server_msg_handler_t callback, void *arg)
{
    if (server) {
        server->onmsg = callback;
        server->msg_arg  = arg;
    }
}

/**
 * @brief 获取服务器文件描述符
 * 
 * @param server 服务器实例指针
 * @param rfds 文件描述符集
 * @return 最大文件描述符，失败返回-1
 */
static int ssn_server_fds (ssn_server_t *server, fd_set *rfds)
{
    int i, max_fd;
    ssn_server_cli_t *cli;

    if (!server || !server->valid || !server->transport) {
        LOG_ERROR("ssn server fds: invalid server handle.");
        return  (-1);
    }

    int server_fd = ssn_transport_get_fd(server->transport);
    if (server_fd >= 0) {
        FD_SET(server_fd, rfds);
        max_fd = server_fd;
    } else {
        max_fd = -1;
    }

    int ev_fd = ipc_event_pair_get_read_fd(server->evtfd);
    FD_SET(ev_fd, rfds);
    if (max_fd < ev_fd) {
        max_fd = ev_fd;
    }

    ipc_mutex_lock(server->lock);

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH(cli, server->clis[i]) {
            int cli_fd = ssn_transport_get_fd(cli->transport);
            if (cli_fd >= 0) {
                FD_SET(cli_fd, rfds);
                if (max_fd < cli_fd) {
                    max_fd = cli_fd;
                }
            }
        }
    }

    ipc_mutex_unlock(server->lock);

    return  (max_fd);
}

/**
 * @brief 命令匹配
 * 
 * @param server 服务器实例指针
 * @param url URL引用
 * @return 匹配的命令指针，未匹配返回NULL
 */
static ssn_server_cmd_t *ssn_server_cmd_match (ssn_server_t *server, const ssn_url_ref_t *url)
{
    int hash = ssn_server_url_hash(url);
    ssn_server_cmd_t *cmd;

    LIST_FOREACH(cmd, server->cmds[hash]) {
        if (cmd->len == url->url_len && !memcmp(cmd->url, url->url, url->url_len)) {
            return  (cmd);
        }
    }

    LIST_FOREACH(cmd, server->prefix_h) {
        if (cmd->len <= url->url_len && !memcmp(cmd->url, url->url, cmd->len)) {
            if ((cmd->len == url->url_len) || (url->url[cmd->len] == '/')) {
                return  (cmd);
            }
        }
    }

    return  (server->def_cmd);
}

/**
 * @brief 服务器数据包输入处理
 * 
 * @param ipc_hdr IPC消息头部
 * @param arg 回调参数
 * @return 处理成功返回true，失败返回false
 */
static bool ssn_server_handle_service_info(ssn_server_t *server, ssn_server_cli_t *cli, ssn_header_t *ipc_hdr, uint16_t seqno)
{
    ssn_header_t *send_hdr;
    ssn_data_ref_t reply;
    uint32_t cid = htonl(cli->id);
    
    send_hdr = ssn_create_header(server->sendbuf, ipc_hdr->msg_type, 0, seqno);
    reply.data = &cid;
    reply.length = sizeof(uint32_t);

    /* 握手完成：切换为 idle 计时（keepalive_timeout 秒 → ms），保留在 hst 链表由定时器线程跟踪；
     * idle 禁用（keepalive_timeout <= 0）时保持原语义：清零并移出链表 */
    if (server->keepalive_timeout > 0) {
        cli->hst.alive = server->keepalive_timeout * 1000;
    } else if (cli->hst.alive) {
        cli->hst.alive = 0;
        DELETE_FROM_LIST(&cli->hst, server->hst_h);
    }
    
    ipc_mutex_unlock(server->lock);
    
    // 释放锁之后再发送响应，避免死锁
    ssn_server_cli_sendmsg(cli, send_hdr, NULL, &reply);
    
    if (!cli->onconn) {
        cli->onconn = true;
        if (server->oncli) {
            server->oncli(server, cli->id, true, server->carg);
        }
    }
    
    return true;
}

static bool ssn_server_handle_rpc_request(ssn_server_t *server, ssn_server_cli_t *cli, ssn_header_t *ipc_hdr, uint16_t seqno, const ssn_url_ref_t *url, const ssn_data_ref_t *data)
{
    ssn_server_cmd_t *cmd;
    ssn_server_rpc_handler_t callback;
    ssn_header_t *send_hdr;
    
    if (url->url_len && url->url[0] == '/') {
        cmd = ssn_server_cmd_match(server, url);
        if (cmd) {
            callback = cmd->onrpc;
            ipc_mutex_unlock(server->lock);
            callback(server, cli->id, ipc_hdr, (ssn_url_ref_t *)url, (ssn_data_ref_t *)data, cmd->arg);
        } else {
            send_hdr = ssn_create_header(server->sendbuf, ipc_hdr->msg_type, SSN_ECODE_NOT_FOUND, seqno);
            ssn_server_cli_sendmsg(cli, send_hdr, NULL, NULL);
            ipc_mutex_unlock(server->lock);
        }
    } else {
        send_hdr = ssn_create_header(server->sendbuf, ipc_hdr->msg_type, SSN_ECODE_INVALID_ARGS, seqno);
        ssn_server_cli_sendmsg(cli, send_hdr, NULL, NULL);
        ipc_mutex_unlock(server->lock);
    }
    
    return true;
}

static bool ssn_server_handle_subscribe(ssn_server_t *server, ssn_server_cli_t *cli, ssn_header_t *ipc_hdr, uint16_t seqno, const ssn_url_ref_t *url)
{
    uint32_t status;
    ssn_server_sub_t *sub;
    ssn_header_t *send_hdr;
    
    if (url->url_len && url->url[0] == '/') {
        LIST_FOREACH(sub, cli->subscribed) {
            if (sub->len == url->url_len && !memcmp(sub->url, url->url, sub->len)) {
                break;
            }
        }
        if (!sub) {
            sub = (ssn_server_sub_t *)calloc(1, sizeof(ssn_server_sub_t) + url->url_len);
            if (!sub) {
                status = SSN_ECODE_OUT_OF_MEMORY;
            } else {
                sub->len = url->url_len;
                memcpy(sub->url, url->url, sub->len);
                sub->url[sub->len] = '\0';
                INSERT_TO_HEADER(sub, cli->subscribed);
                status = 0;
            }
        } else {
            status = 0;
        }
    } else {
        status = SSN_ECODE_INVALID_ARGS;
    }
    
    send_hdr = ssn_create_header(server->sendbuf, ipc_hdr->msg_type, status, seqno);
    ssn_server_cli_sendmsg(cli, send_hdr, NULL, NULL);
    ipc_mutex_unlock(server->lock);
    
    return true;
}

static bool ssn_server_handle_unsubscribe(ssn_server_t *server, ssn_server_cli_t *cli, ssn_header_t *ipc_hdr, uint16_t seqno, const ssn_url_ref_t *url)
{
    uint32_t status = 0;
    ssn_server_sub_t *sub, *sub_temp;
    ssn_header_t *send_hdr;
    
    if (url->url_len && url->url[0] == '/') {
        LIST_FOREACH_SAFE(sub, sub_temp, cli->subscribed) {
            if (url->url_len != sub->len || memcmp(sub->url, url->url, sub->len)) {
                continue;
            }
            DELETE_FROM_LIST(sub, cli->subscribed);
            free(sub);
            break;
        }
    } else {
        LIST_FOREACH_SAFE(sub, sub_temp, cli->subscribed) {
            DELETE_FROM_LIST(sub, cli->subscribed);
            free(sub);
        }
    }
    
    send_hdr = ssn_create_header(server->sendbuf, ipc_hdr->msg_type, status, seqno);
    ssn_server_cli_sendmsg(cli, send_hdr, NULL, NULL);
    ipc_mutex_unlock(server->lock);
    
    return true;
}

static bool ssn_server_handle_ping_echo(ssn_server_t *server, ssn_server_cli_t *cli, ssn_header_t *ipc_hdr, uint16_t seqno)
{
    ssn_header_t *send_hdr;
    
    send_hdr = ssn_create_header(server->sendbuf, ipc_hdr->msg_type, 0, seqno);
    ssn_server_cli_sendmsg(cli, send_hdr, NULL, NULL);
    ipc_mutex_unlock(server->lock);
    
    return true;
}

static bool ssn_server_input (ssn_header_t *ipc_hdr, void *arg)
{
    struct input_arg *input_arg = arg;
    ssn_server_t *server  = input_arg->server;
    ssn_server_cli_t *cli = input_arg->cli;
    uint16_t seqno;
    ssn_url_ref_t url;
    ssn_data_ref_t data;

    seqno = ssn_get_seqno(ipc_hdr);
    ssn_get_url(ipc_hdr, &url);
    ssn_get_data(ipc_hdr, &data);

    if (!cli->active) {
        cli->active = true;
    }

    /* 握手完成后（active）每次收到数据包都重置 idle 计时：
     * 定时器线程按 keepalive_timeout 递减 hst.alive，活跃连接借此续期不被断开。
     * hst.alive 为双线程共享字段（定时器线程持 server->lock 读改写），读改写必须持锁；
     * MESSAGE 分支在加锁区之前提前返回，故重置需自带锁并在入口统一执行。 */
    if (cli->active && server->keepalive_timeout > 0) {
        ipc_mutex_lock(server->lock);
        if (cli->hst.alive) {
            cli->hst.alive = server->keepalive_timeout * 1000;
        }
        ipc_mutex_unlock(server->lock);
    }

    if (ipc_hdr->msg_type == SSN_MSG_TYPE_MESSAGE) {
        if (server->onmsg) {
            server->onmsg(server, cli->id, &url, &data, server->msg_arg);
        }
        return  (server->valid);
    }

    ipc_mutex_lock(server->lock);

    switch (ipc_hdr->msg_type) {

    case SSN_MSG_TYPE_SERVICE_INFO:
        ssn_server_handle_service_info(server, cli, ipc_hdr, seqno);
        break;

    case SSN_MSG_TYPE_RPC_REQUEST:
        ssn_server_handle_rpc_request(server, cli, ipc_hdr, seqno, &url, &data);
        break;

    case SSN_MSG_TYPE_SUBSCRIBE:
        ssn_server_handle_subscribe(server, cli, ipc_hdr, seqno, &url);
        break;

    case SSN_MSG_TYPE_UNSUBSCRIBE:
        ssn_server_handle_unsubscribe(server, cli, ipc_hdr, seqno, &url);
        break;

    case SSN_MSG_TYPE_PING_ECHO:
        ssn_server_handle_ping_echo(server, cli, ipc_hdr, seqno);
        break;

    default:
        ipc_mutex_unlock(server->lock);
        break;
    }

    return  (server->valid);
}

/**
 * @brief 服务器输入事件处理
 * 
 * @param server 服务器实例指针
 * @param rfds 文件描述符集
 */
/**
 * @brief 处理客户端输入事件
 * 
 * @param server 服务器实例指针
 * @param cli 客户端实例指针
 * @param rfds 文件描述符集
 */
static void ssn_server_handle_client_input(ssn_server_t *server, ssn_server_cli_t *cli, const fd_set *rfds)
{
    ssize_t num;
    struct input_arg input_arg;
    
    int cli_fd = ssn_transport_get_fd(cli->transport);
    if (cli_fd >= 0 && FD_ISSET(cli_fd, rfds)) {
        num = ssn_transport_recv(cli->transport, server->recvbuf, SSN_MAX_PACKET_SIZE, 0);
        if (num > 0) {
            input_arg.server = server;
            input_arg.cli = cli;
            ssn_stream_feed(&cli->recv, server->recvbuf, num, ssn_server_input, &input_arg);
        }

        if (num == 0) {
            // Connection closed by peer
            if (cli->onconn) {
                cli->onconn = false;
                if (server->oncli) {
                    server->oncli(server, cli->id, false, server->carg);
                }
            }

            ipc_mutex_lock(server->lock);
            ssn_server_cli_destroy(server, cli);
            ipc_mutex_unlock(server->lock);
        } else if (num < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            // Real error (not just "no data available yet")
            LOG_ERROR("Failed to receive from client: %s", strerror(errno));
            if (cli->onconn) {
                cli->onconn = false;
                if (server->oncli) {
                    server->oncli(server, cli->id, false, server->carg);
                }
            }

            ipc_mutex_lock(server->lock);
            ssn_server_cli_destroy(server, cli);
            ipc_mutex_unlock(server->lock);
        }
    }
}

static void ipc_server_handle_new_connection(ssn_server_t *server, const fd_set *rfds)
{
    int sock;
    socklen_t addr_len = sizeof(struct sockaddr_storage);
    struct sockaddr_storage addr;
    ssn_server_cli_t *cli;
    
    int server_fd = ssn_transport_get_fd(server->transport);
    if (server_fd >= 0 && FD_ISSET(server_fd, rfds)) {
        ssn_address_t client_addr;
        ssn_transport_t *client_transport = ssn_transport_accept(server->transport, &client_addr, 0);
        if (client_transport) {
            cli = (ssn_server_cli_t *)calloc(1, sizeof(ssn_server_cli_t));
            if (cli) {
                cli->transport = client_transport;
                cli->active = false;
                /* TODO: deal with init recv buffer. */
                ssn_stream_init(&cli->recv);

                ipc_mutex_lock(server->lock);
                ssn_server_cli_init(server, cli);
                ipc_mutex_unlock(server->lock);

                /* 首包处理交给下一轮 poll：client fd 已入 clis 表，FD_ISSET 先验后
                 * 由 ssn_server_handle_client_input 接管（修复 Issue #4——原实现在此
                 * 无条件 recv(timeout=0) 无限阻塞，空连接可挂死整个服务端）。 */
            } else {
                ssn_transport_destroy(client_transport);
            }
        }
    }
}

static void ipc_server_handle_event_input(ssn_server_t *server, const fd_set *rfds)
{
    int evt_fd = ipc_event_pair_get_read_fd(server->evtfd);
    ssn_server_hst_t *hst, *hst_temp;
    ssn_server_cli_t *cli;
    
    if (FD_ISSET(evt_fd, rfds)) {
        ipc_event_pair_drain(server->evtfd);
        ipc_mutex_lock(server->lock);

        LIST_FOREACH_SAFE(hst, hst_temp, server->hst_h) {
            if (hst->alive == 0) {
                DELETE_FROM_LIST(hst, server->hst_h);

                cli = (ssn_server_cli_t *)((char *)hst - offsetof(ssn_server_cli_t, hst));
                if (cli->transport) {
                    ssn_transport_disconnect(cli->transport);
                }
                /* 断开后立即销毁：fd 已关闭，poll 循环无法再观察到该连接的 EOF，
                 * 若不销毁则 cli 悬挂在哈希表中，peer_count 无法归零并造成内存泄漏 */
                ssn_server_cli_destroy(server, cli);
            }
        }

        ipc_mutex_unlock(server->lock);
    }
}

static void ssn_server_input_fds (ssn_server_t *server, const fd_set *rfds)
{
    int i;
    ssn_server_cli_t *cli, *cli_temp;

    if (!server || !server->valid) {
        return;
    }

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH_SAFE(cli, cli_temp, server->clis[i]) {
            ssn_server_handle_client_input(server, cli, rfds);
        }
    }

    ipc_server_handle_new_connection(server, rfds);
    ipc_server_handle_event_input(server, rfds);
}

/*
 * IPC server poll 
 */
int ssn_server_poll(ssn_server_t *server, int timeout_ms)
{
    fd_set fds;
    sigset_t empty_mask;
    /* 负数超时按「无限等待」处理（缺陷背景：原实现对 -1 计算非法 timespec——
     * -1/1000=0、(-1%1000)=-1 → tv_nsec=-1e6，pselect 恒 EINVAL，poll(-1) 无法
     * 实现永久阻塞语义） */
    struct timespec timeout_buf;
    struct timespec *timeout_ptr = NULL;
    if (timeout_ms >= 0) {
        timeout_buf.tv_sec  = timeout_ms / 1000;
        timeout_buf.tv_nsec = (timeout_ms % 1000) * 1000000LL;
        timeout_ptr = &timeout_buf;
    }

    if (!server) return -1;

    /* 引用计数保活：回调中调用 ssn_server_destroy 时，本 poll 仍持有引用，
     * server 延迟到 poll 结束才真正释放（避免回调后访问已 free 对象） */
    ssn_server_ref(server);

    sigemptyset(&empty_mask);
    FD_ZERO(&fds);
    int max_fd = ssn_server_fds(server, &fds);
    // 阻塞空信号集，可以传递并中断所有信号
    int cnt;
    do {
        cnt = pselect(max_fd + 1, &fds, NULL, NULL, timeout_ptr, &empty_mask);
    } while (cnt < 0 && errno == EINTR);
    if (cnt > 0) {
        ssn_server_input_fds(server, &fds);
        ssn_server_unref(server);
        return 0;
    }
    ssn_server_unref(server);
    return cnt;
}

void ssn_server_run(ssn_server_t *server)
{
    fd_set fds;
    sigset_t empty_mask;

    while (true) {
        sigemptyset(&empty_mask);
        FD_ZERO(&fds);
        int max_fd = ssn_server_fds(server, &fds);
        // 阻塞空信号集，可以传递并中断所有信号
        int cnt;
        do {
            cnt = pselect(max_fd + 1, &fds, NULL, NULL, NULL, &empty_mask);
        } while (cnt < 0 && errno == EINTR);
        if (cnt > 0) {
            ssn_server_input_fds(server, &fds);
            continue;
        }
    }
}

/*
 * end
 */
