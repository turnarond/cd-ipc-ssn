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
#include "ipc_list.h"
#include "ipc_server.h"
#include "ipc_protocol.h"
#include "ipc_error.h"
#include "ipc_global.h"
#include "util/ipc_log.h"

/* Client hash */
#define IPC_CLI_HASH_SIZE  64
#define IPC_CLI_HASH_MASK  (IPC_CLI_HASH_SIZE - 1)

/* Command hash */
#define IPC_CMD_HASH_SIZE  32
#define IPC_CMD_HASH_MASK  (IPC_CMD_HASH_SIZE - 1)

/* Subscription node */
typedef struct ipc_server_sub {
    struct ipc_server_sub *next;
    struct ipc_server_sub *prev;
    size_t len;
    char url[1];
} ipc_server_sub_t;

/* Client handshake timer */
typedef struct ipc_server_hst {
    struct ipc_server_hst *next;
    struct ipc_server_hst *prev;
    int alive;
} ipc_server_hst_t;

/* Client node */
typedef struct ipc_server_cli {
    bool active;
    bool onconn;
    struct ipc_server_cli *next;
    struct ipc_server_cli *prev;
    ipc_server_sub_t *subscribed;
    ipc_server_hst_t hst;
    ipc_stream_ctx_t recv;
    int sock;
    cli_id_t id;
} ipc_server_cli_t;

/* Server command */
typedef struct ipc_server_cmd {
    struct ipc_server_cmd *next;
    struct ipc_server_cmd *prev;
    ipc_server_rpc_handler_t onrpc;
    void *arg;
    size_t len;
    char url[1];
} ipc_server_cmd_t;

/* Server */
struct ipc_server {
    bool valid;
    char ifname[IF_NAMESIZE];
    char srv_name[SRV_NAME_LEN];
    cli_id_t ncid;
    ipc_server_t *next;
    ipc_server_t *prev;
    ipc_server_hst_t *hst_h;
    ipc_server_cli_t *clis[IPC_CLI_HASH_SIZE];
    ipc_server_cmd_t *cmds[IPC_CMD_HASH_SIZE];
    ipc_server_cmd_t *def_cmd;
    ipc_server_cmd_t *prefix_h;
    ipc_server_cmd_t *prefix_t;
    ipc_server_msg_handler_t onmsg;
    void *msg_arg;
    ipc_on_connect_t oncli;
    void *carg;
    ipc_mutex_t *lock;
    int send_timeout;
    int handshake_timeout;
    int keepalive_timeout;
    int sock;
    ipc_event_pair_t *evtfd;
    void *sendbuf;
    void *recvbuf;
};

/* Input argument */
struct input_arg {
    ipc_server_t *server;
    ipc_server_cli_t *cli;
};

/*
 * Remote client hash
 */
#define ipc_server_cli_hash(id)  (int)(id & IPC_CLI_HASH_MASK)

/*
 * Server timer thread handle
 */
void *ipc_server_timer_handle(void *arg)
{
    bool emit;
    ipc_server_t *server;
    ipc_server_hst_t *hst;

    (void)arg;

    do {
        ipc_thread_msleep(IPC_TIMER_PERIOD);

        ipc_mutex_lock(g_ipc_server_lock);

        if (!g_ipc_server_list) {
            ipc_mutex_unlock(g_ipc_server_lock);
            break;
        }

        LIST_FOREACH(server, g_ipc_server_list) {
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

        ipc_mutex_unlock(g_ipc_server_lock);

    } while (true);

    ipc_thread_exit();

    return NULL;
}

/*
 * Command hash
 */
static int ipc_server_url_hash(const ipc_url_ref_t *url)
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
static ipc_server_cli_t *ipc_server_cli_find(ipc_server_t *server, cli_id_t id)
{
    int hash = ipc_server_cli_hash(id);
    ipc_server_cli_t *cli;

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
static cli_id_t ipc_server_cli_newid(ipc_server_t *server)
{
    cli_id_t id;

    do {
        id = server->ncid;
        server->ncid++;
    } while (ipc_server_cli_find(server, id));

    return  (id);
}

/*
 * Initialize a client
 */
static void ipc_server_cli_init(ipc_server_t *server, ipc_server_cli_t *cli)
{
    int hash;

    cli->id = ipc_server_cli_newid(server);
    hash = ipc_server_cli_hash(cli->id);
    INSERT_TO_HEADER(cli, server->clis[hash]);

    cli->hst.alive = server->handshake_timeout;
    INSERT_TO_HEADER(&cli->hst, server->hst_h);
    LOG_DEBUG("ipc server cli init success");
}

/*
 * Destroy a client
 */
static void ipc_server_cli_destroy(ipc_server_t *server, ipc_server_cli_t *cli)
{
    int hash = ipc_server_cli_hash(cli->id);
    ipc_server_sub_t *sub, *sub_temp;

    LIST_FOREACH_SAFE(sub, sub_temp, cli->subscribed) {
        DELETE_FROM_LIST(sub, cli->subscribed);
        free(sub);
    }

    DELETE_FROM_LIST(cli, server->clis[hash]);

    if (cli->hst.alive) {
        cli->hst.alive = 0;
        DELETE_FROM_LIST(&cli->hst, server->hst_h);
    }

    ipc_socket_close(cli->sock);
    free(cli);
    LOG_DEBUG("ipc server cli destroy success.");
}

/*
 * Close a client
 */
bool ipc_server_peer_close(ipc_server_t *server, cli_id_t id)
{
    bool ret;
    ipc_server_cli_t *cli;

    if (!server || !server->valid) {
        LOG_ERROR("ipc server peer close failed: invalid server handle.");
        return  (false);
    }

    ipc_mutex_lock(server->lock);

    cli = ipc_server_cli_find(server, id);
    if (cli) {
        ipc_socket_shutdown(cli->sock);
        ret = true;
    } else {
        ret = false;
    }

    ipc_mutex_unlock(server->lock);

    LOG_DEBUG("ipc server peer close success: cid is %d.", id);
    return  (ret);
}


/*
 * Client send
 */
static bool ipc_server_cli_sendmsg(ipc_server_cli_t *cli, ipc_header_t *ipc_hdr, 
    const ipc_url_ref_t *url, const ipc_data_ref_t *data)
{
    bool ret = ipc_send_message(cli->sock, ipc_hdr, url, data);
    if (!ret) {
        ipc_socket_shutdown(cli->sock);
        LOG_ERROR("ipc server sendmsg faield, cli %d errno %d", cli->id, errno);
    }
    return ret;
}

/*
 * Client subscribe match
 */
static bool ipc_server_cli_sub_match(ipc_server_cli_t *cli, const ipc_url_ref_t *url)
{
    size_t path_len;
    ipc_server_sub_t *sub;

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

ipc_server_t *ipc_server_create_with_options(const char *name, const server_options_t *opts)
{
    ipc_server_t *server;

    if (name == NULL) {
        LOG_ERROR("ipc server create with options: invalid name.");
        return NULL;
    }

    server = (ipc_server_t *)calloc(1, sizeof(ipc_server_t));
    if (!server) {
        LOG_ERROR("ipc server create with options: calloc failed, errno is %d.", errno);
        return NULL;
    }

    server->sock   = -1;

    if (ipc_mutex_init(&server->lock)) {
        LOG_ERROR("ipc server create with options: init mutex failed, errno %d", errno);
        goto error;
    }

    if (ipc_event_pair_create(&server->evtfd) != 0) {
        LOG_ERROR("ipc server create with options: event pair create failed, errno %d", errno);
        goto error;
    }

    server->sendbuf = malloc(IPC_MAX_PACKET_SIZE * 2);
    if (!server->sendbuf) {
        LOG_ERROR("ipc server create with options: sendbuf malloc failed, errno is %d", errno);
        goto error;
    }

    if (opts) {
        server->send_timeout = opts->send_timeout_ms;
        server->handshake_timeout = opts->conn_timeout_ms;
        server->keepalive_timeout = opts->idle_timeout_sec;
        if(opts->ifname[0]) {
            strncpy(server->ifname, opts->ifname, strlen(opts->ifname));
        }
    } else {
        server->send_timeout = IPC_DEF_SEND_TIMEOUT;
        server->handshake_timeout = IPC_SERVER_DEF_HANDSHAKE_TIMEOUT;
        server->keepalive_timeout = IPC_SERVER_KEEPALIVE_TIMEOUT;
    }

    strncpy(server->srv_name, name, strlen(name));
    server->recvbuf      = (uint8_t *)server->sendbuf + IPC_MAX_PACKET_SIZE;
    server->valid        = true;

    ipc_mutex_lock(g_ipc_server_lock);

    INSERT_TO_HEADER(server, g_ipc_server_list);

    ipc_mutex_unlock(g_ipc_server_lock);

    LOG_DEBUG("ipc server create with option success, name is %s", name);

    return  (server);

error:
    if (server->sendbuf) free(server->sendbuf);
    if (server->evtfd) ipc_event_pair_destroy(server->evtfd);
    ipc_mutex_destroy(server->lock);
    free(server);
    LOG_ERROR("ipc server create with options: failed, errno %d", errno);
    return NULL;
}

/**
 * @brief 创建IPC服务器
 * 
 * @param server_info 服务器信息
 * @return 服务器实例指针，失败返回NULL
 * @warning 此函数必须与ipc_server_destroy()调用互斥
 */
ipc_server_t *ipc_server_create(const char *server_info)
{
    return ipc_server_create_with_options(server_info, NULL);
}

/**
 * @brief 启动IPC服务器
 * 
 * @param server 服务器实例指针
 * @return 启动成功返回true，失败返回false
 */
bool ipc_server_start(ipc_server_t *server)
{
    int en = 1;

    struct sockaddr_un addr;

    if (!server || !server->valid) {
        ipc_handle_error(IPC_ERR_INVALID_ARGS, __FILE__, __LINE__, __func__, "invalid server handle");
        return  (false);
    }

    /* Check uds path exist. */
    struct stat st_uds;
    if (stat(server->srv_name, &st_uds) == 0) {
        /* Check if is a sock file. */
        if (S_ISSOCK(st_uds.st_mode)) {
            unlink(server->srv_name);
            LOG_INFO("ipc server start: delete sock file %s.", server->srv_name);
        } else {
            LOG_ERROR("ipc server start: file %s is not a sock file.", server->srv_name);
            return false;
        }
    } else if (errno != ENOENT) {
        LOG_ERROR("ipc server start: stat file %s exist but failed, errno %d.", server->srv_name, errno);
        return false;
    }

    server->sock = ipc_socket_create(AF_UNIX, SOCK_STREAM, 0, false);
    if (server->sock < 0) {
        ipc_handle_error(IPC_ERR_NET_CONNECT, __FILE__, __LINE__, __func__, "create socket failed, errno is %d", errno);
        return  (false);
    }

    setsockopt(server->sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&en, sizeof(int));

    if (strlen(server->srv_name) > sizeof(addr.sun_path)) {
        LOG_ERROR("ipc server start: Invalid ipc path %s.", server->srv_name);
        goto error;
    }
    memset(&addr, 0x0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, server->srv_name);

    if (bind(server->sock, (struct sockaddr *)&addr, SUN_LEN(&addr))) {
        ipc_handle_error(IPC_ERR_NET_CONNECT, __FILE__, __LINE__, __func__, "bind failed, errno is %d", errno);
        goto error;
    }

    if (server->ifname[0]) {
        ipc_socket_bind_to_interface(server->sock, server->ifname);
    }

    listen(server->sock, IPC_SERVER_BACKLOG);

    LOG_DEBUG("ipc server start success.");

    return  (true);

error:
    if (server->sock >= 0) {
        ipc_socket_close(server->sock);
        server->sock = -1;
    }

    LOG_ERROR("ipc server start failed.");

    return  (false);
}

/**
 * @brief 获取IPC服务器地址（必须在`ipc_server_start`之后调用）
 * 
 * @param server 服务器实例指针
 * @param addr 地址结构体
 * @param namelen 地址长度
 * @return 获取成功返回true，失败返回false
 */
int ipc_server_address(ipc_server_t *server, struct sockaddr *addr, socklen_t *namelen)
{
    if (!server || !server->valid || server->sock < 0) {
        LOG_ERROR("ipc server address: invalid server handle.");
        return  (false);
    }

    if (getsockname(server->sock, addr, namelen)) {
        LOG_ERROR("ipc server address: getsockname failed, errno is %d.", errno);
        return  (false);
    }

    LOG_DEBUG("ipc server address success.");
    return  (true);
}

/**
 * @brief 关闭IPC服务器
 * 
 * @param server 服务器实例指针
 * @warning 此函数必须与ipc_server_create()调用互斥
 */
void ipc_server_destroy(ipc_server_t *server)
{
    int i;
    ipc_server_cli_t *cli, *cli_temp;
    ipc_server_cmd_t *cmd, *cmd_temp;

    if (!server || !server->valid) {
        LOG_ERROR("ipc server destroy: invalid server handle.");
        return;
    }

    ipc_mutex_lock(g_ipc_server_lock);

    DELETE_FROM_LIST(server, g_ipc_server_list);

    ipc_mutex_unlock(g_ipc_server_lock);

    ipc_mutex_lock(server->lock);

    server->valid = false;
    ipc_memory_barrier();

    if (server->sock >= 0) {
        ipc_socket_close(server->sock);
        server->sock = -1;
    }

    ipc_event_pair_destroy(server->evtfd);
    free(server->sendbuf);

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH_SAFE(cli, cli_temp, server->clis[i]) {
            ipc_server_cli_destroy(server, cli);
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

    ipc_mutex_unlock(server->lock);
    ipc_mutex_destroy(server->lock);

    unlink(server->srv_name);

    free(server);
    LOG_DEBUG("ipc server destory success.");
}

/**
 * @brief 设置客户端连接回调函数
 * 
 * @param server 服务器实例指针
 * @param oncli 连接回调函数
 * @param arg 回调参数
 */
void ipc_server_set_connect_handler (ipc_server_t *server, ipc_on_connect_t oncli, void *arg)
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
int ipc_server_peer_count (ipc_server_t *server)
{
    int i, cnt = 0;
    ipc_server_cli_t *cli;

    if (!server || !server->valid) {
        LOG_ERROR("ipc server peer count: invalid server handle.");
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

    LOG_DEBUG("ipc server peer count: count is %d.", cnt);

    return  (cnt);
}

/**
 * @brief 检查服务器是否被订阅
 * 
 * @param server 服务器实例指针
 * @param url URL引用
 * @return 已订阅返回true，未订阅返回false
 */
bool ipc_server_is_subscribed (ipc_server_t *server, const ipc_url_ref_t *url)
{
    ipc_server_cli_t *cli;
    int i;

    if (!server || !server->valid) {
        LOG_ERROR("ipc server is subscribe: invalid server handle.");
        return  (false);
    }
    if (!url || !url->url || !url->url_len) {
        LOG_ERROR("ipc server is subscribe: invalid url handle.");
        return  (false);
    }

    ipc_mutex_lock(server->lock);

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH(cli, server->clis[i]) {
            if (!cli->active) {
                continue;
            }
            if (ipc_server_cli_sub_match(cli, url)) {
                ipc_mutex_unlock(server->lock);
                LOG_DEBUG("ipc server is subscribed, true.");
                return  (true);
            }
        }
    }

    ipc_mutex_unlock(server->lock);

    LOG_DEBUG("ipc server is subscribed, false.");

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
static bool ipc_server_do_publish (ipc_server_t *server, const ipc_url_ref_t *url, const ipc_data_ref_t *data)
{
    int i;
    size_t len;
    ipc_header_t *ipc_hdr;
    ipc_server_cli_t *cli;

    if (!server || !server->valid) {
        LOG_ERROR("ipc server publish: invalid server handle.");
        return  (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ipc server publish: invalid url.");
        return  (false);
    }

    ipc_mutex_lock(server->lock);

    ipc_hdr = ipc_create_header(server->sendbuf, IPC_MSG_TYPE_PUBLISH, 0, 0);

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH(cli, server->clis[i]) {
            if (!cli->active) {
                continue;
            }
            if (ipc_server_cli_sub_match(cli, url)) {
                ipc_server_cli_sendmsg(cli, ipc_hdr, url, data);
            }
        }
    }

    ipc_mutex_unlock(server->lock);

    LOG_DEBUG("ipc server publish success.");

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
int ipc_server_publish (ipc_server_t *server, const ipc_url_ref_t *url, const ipc_data_ref_t *data)
{
    return  (ipc_server_do_publish(server, url, data));
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
bool ipc_server_add_method (ipc_server_t *server,
                               const ipc_url_ref_t *url, ipc_server_rpc_handler_t callback, void *arg)
{
    int hash;
    size_t path_len;
    bool def, prefix;
    ipc_server_cmd_t *cmd, *need_free = NULL;

    if (!server || !server->valid) {
        LOG_ERROR("ipc server add method: invalid server handle.");
        return  (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/' || !callback) {
        LOG_ERROR("ipc server add method: invalid url.");
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

    cmd = (ipc_server_cmd_t *)calloc(1, sizeof(ipc_server_cmd_t) + url->url_len);
    if (!cmd) {
        LOG_ERROR("ipc server add method: calloc failed, errno is %d.", errno);
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
            hash = ipc_server_url_hash(url);
            INSERT_TO_HEADER(cmd, server->cmds[hash]);
        }
    }

    ipc_mutex_unlock(server->lock);

    if (need_free) {
        free(need_free);
    }

    LOG_DEBUG("ipc server add method success.");

    return  (true);
}

/**
 * @brief 移除RPC监听器
 * 
 * @param server 服务器实例指针
 * @param url URL引用
 */
void ipc_server_remove_method (ipc_server_t *server, const ipc_url_ref_t *url)
{
    int hash;
    size_t path_len;
    bool def, prefix;
    ipc_server_cmd_t *cmd, *cmd_temp, **header;

    if (!server || !server->valid) {
        LOG_ERROR("ipc server remove method: invalid server handle.");
        return;
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ipc server remove method: invalid url.");
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
            hash   = ipc_server_url_hash(url);
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

    LOG_DEBUG("ipc server remove method %.*s success.", (int)url->url_len, url->url);
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
int ipc_server_peer_address (ipc_server_t *server, cli_id_t id, struct sockaddr *addr, socklen_t *namelen)
{
    ipc_server_cli_t *cli;

    if (!server || !server->valid) {
        LOG_ERROR("ipc server peer address: invalid server handle.");
        return  (false);
    }

    ipc_mutex_lock(server->lock);

    cli = ipc_server_cli_find(server, id);
    if (!cli || getpeername(cli->sock, addr, namelen)) {
        ipc_mutex_unlock(server->lock);
        if (!cli) {
            LOG_ERROR("ipc server peer address: client not found for id %d.", id);
        } else {
            LOG_ERROR("ipc server peer address: invalid client handle %d.", cli->id);
        }
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
int ipc_server_response (ipc_server_t *server, cli_id_t id,
                            uint32_t status, uint16_t seqno, const ipc_data_ref_t *data)
{
    bool ret;
    ipc_server_cli_t *cli;
    ipc_header_t *ipc_hdr;

    if (!server || !server->valid) {
        LOG_ERROR("ipc server response: invalid server handle.");
        return  (false);
    }

    ipc_mutex_lock(server->lock);

    cli = ipc_server_cli_find(server, id);
    if (!cli) {
        ipc_mutex_unlock(server->lock);
        LOG_ERROR("ipc server response: invalid cli %d handle.", id);
        return  (false);
    }

    ipc_hdr = ipc_create_header(server->sendbuf, IPC_MSG_TYPE_RPC_REQUEST, status, seqno);

    ret = ipc_server_cli_sendmsg(cli, ipc_hdr, NULL, data);

    ipc_mutex_unlock(server->lock);

    if (ret) {
        LOG_DEBUG("ipc server response success: cid %d.", id);
    } else {
        LOG_ERROR("ipc server response failed: cid %d.", id);
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
bool ipc_server_cli_keepalive (ipc_server_t *server, cli_id_t id, int keepalive)
{
    int en = 1;
    ipc_server_cli_t *cli;

    int count = 3, idle = server->keepalive_timeout;

    if (!server || !server->valid) {
        LOG_ERROR("ipc server cli keepalive: invalid server handle.");
        return  (false);
    }

    ipc_mutex_lock(server->lock);

    cli = ipc_server_cli_find(server, id);
    if (!cli) {
        ipc_mutex_unlock(server->lock);
        LOG_ERROR("ipc server cli keepalive: invalid cli of id %d.", id);
        return  (false);
    }

    setsockopt(cli->sock, SOL_SOCKET, SO_KEEPALIVE, (const void *)&en, sizeof(int));

    ipc_mutex_unlock(server->lock);

    LOG_DEBUG("ipc server cli keepalive %d success.", id);

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
int ipc_server_peer_list (ipc_server_t *server, cli_id_t ids[], int max_cnt)
{
    int i, cnt;
    ipc_server_cli_t *cli;

    if (!server || !server->valid || !ids || max_cnt <= 0) {
        LOG_ERROR("ipc server peer list: invalid server handle.");
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

    LOG_DEBUG("ipc server peer list success, cnt is %d.", cnt);

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
bool ipc_server_cli_send_timeout (ipc_server_t *server, cli_id_t id, int timeout_ms)
{
    int timeval;
    ipc_server_cli_t *cli;

    if (!server || !server->valid) {
        LOG_ERROR("ipc server send timeout failed: invalid server handle.");
        return  (false);
    }

    if (timeout_ms > 0) {
        timeval = timeout_ms;
    } else {
        timeval = server->send_timeout;
    }

    ipc_mutex_lock(server->lock);

    cli = ipc_server_cli_find(server, id);
    
    if (cli) {
        ipc_socket_set_send_timeout(cli->sock, timeval);
    }
    ipc_mutex_unlock(server->lock);


    LOG_DEBUG("ipc server cli send timeout of cid %d success.", id);

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
int ipc_server_cli_do_message (ipc_server_t *server, cli_id_t id, const ipc_url_ref_t *url, const ipc_data_ref_t *data)
{
    bool ret;
    size_t len;
    ipc_server_cli_t *cli;
    ipc_header_t *ipc_hdr;

    if (!server || !server->valid) {
        LOG_ERROR("ipc server do message: invalid server handle.");
        return  (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        LOG_ERROR("ipc server do message: invalid url.");
        return  (false);
    }
    if (!data) {
        LOG_ERROR("ipc server do message: invalid data.");
        return  (false);
    }

    ipc_mutex_lock(server->lock);

    cli = ipc_server_cli_find(server, id);
    if (!cli) {
        ipc_mutex_unlock(server->lock);
        LOG_ERROR("ipc server do message: not found cli %d.", id);
        return  (false);
    }

    ipc_hdr = ipc_create_header(server->sendbuf, IPC_MSG_TYPE_MESSAGE, 0, 0);

    ret = ipc_server_cli_sendmsg(cli, ipc_hdr, url, data);

    ipc_mutex_unlock(server->lock);

    if (ret) {
        LOG_DEBUG("ipc server do message to cid %d success.", id);
    } else {
        LOG_ERROR("ipc server do message to cid %d failed.", id);
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
int ipc_server_message (ipc_server_t *server, cli_id_t id, const ipc_url_ref_t *url, const ipc_data_ref_t *data)
{
    return  (ipc_server_cli_do_message(server, id, url, data));
}

/**
 * @brief 设置消息处理回调函数
 * 
 * @param server 服务器实例指针
 * @param callback 消息处理回调函数
 * @param arg 回调参数
 */
void ipc_server_set_message_handler (ipc_server_t *server, ipc_server_msg_handler_t callback, void *arg)
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
static int ipc_server_fds (ipc_server_t *server, fd_set *rfds)
{
    int i, max_fd;
    ipc_server_cli_t *cli;

    if (!server || !server->valid || server->sock < 0) {
        LOG_ERROR("ipc server fds: invalid server handle.");
        return  (-1);
    }

    FD_SET(server->sock, rfds);
    max_fd = server->sock;

    int ev_fd = ipc_event_pair_get_read_fd(server->evtfd);
    FD_SET(ev_fd, rfds);
    if (max_fd < ev_fd) {
        max_fd = ev_fd;
    }

    ipc_mutex_lock(server->lock);

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH(cli, server->clis[i]) {
            FD_SET(cli->sock, rfds);
            if (max_fd < cli->sock) {
                max_fd = cli->sock;
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
static ipc_server_cmd_t *ipc_server_cmd_match (ipc_server_t *server, const ipc_url_ref_t *url)
{
    int hash = ipc_server_url_hash(url);
    ipc_server_cmd_t *cmd;

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
static bool ipc_server_handle_service_info(ipc_server_t *server, ipc_server_cli_t *cli, ipc_header_t *ipc_hdr, uint16_t seqno)
{
    ipc_header_t *send_hdr;
    ipc_data_ref_t reply;
    uint32_t cid = htonl(cli->id);
    
    send_hdr = ipc_create_header(server->sendbuf, ipc_hdr->msg_type, 0, seqno);
    reply.data = &cid;
    reply.length = sizeof(uint32_t);
    ipc_server_cli_sendmsg(cli, send_hdr, NULL, &reply);
    
    if (cli->hst.alive) {
        cli->hst.alive = 0;
        DELETE_FROM_LIST(&cli->hst, server->hst_h);
    }
    
    ipc_mutex_unlock(server->lock);
    
    if (!cli->onconn) {
        cli->onconn = true;
        if (server->oncli) {
            server->oncli(server, cli->id, true, server->carg);
        }
    }
    
    return true;
}

static bool ipc_server_handle_rpc_request(ipc_server_t *server, ipc_server_cli_t *cli, ipc_header_t *ipc_hdr, uint16_t seqno, const ipc_url_ref_t *url, const ipc_data_ref_t *data)
{
    ipc_server_cmd_t *cmd;
    ipc_server_rpc_handler_t callback;
    ipc_header_t *send_hdr;
    
    if (url->url_len && url->url[0] == '/') {
        cmd = ipc_server_cmd_match(server, url);
        if (cmd) {
            callback = cmd->onrpc;
            ipc_mutex_unlock(server->lock);
            callback(server, cli->id, ipc_hdr, (ipc_url_ref_t *)url, (ipc_data_ref_t *)data, cmd->arg);
        } else {
            send_hdr = ipc_create_header(server->sendbuf, ipc_hdr->msg_type, IPC_ERR_NOT_FOUND, seqno);
            ipc_server_cli_sendmsg(cli, send_hdr, NULL, NULL);
            ipc_mutex_unlock(server->lock);
        }
    } else {
        send_hdr = ipc_create_header(server->sendbuf, ipc_hdr->msg_type, IPC_ERR_INVALID_ARGS, seqno);
        ipc_server_cli_sendmsg(cli, send_hdr, NULL, NULL);
        ipc_mutex_unlock(server->lock);
    }
    
    return true;
}

static bool ipc_server_handle_subscribe(ipc_server_t *server, ipc_server_cli_t *cli, ipc_header_t *ipc_hdr, uint16_t seqno, const ipc_url_ref_t *url)
{
    uint32_t status;
    ipc_server_sub_t *sub;
    ipc_header_t *send_hdr;
    
    if (url->url_len && url->url[0] == '/') {
        LIST_FOREACH(sub, cli->subscribed) {
            if (sub->len == url->url_len && !memcmp(sub->url, url->url, sub->len)) {
                break;
            }
        }
        if (!sub) {
            sub = (ipc_server_sub_t *)calloc(1, sizeof(ipc_server_sub_t) + url->url_len);
            if (!sub) {
                status = IPC_ERR_OUT_OF_MEMORY;
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
        status = IPC_ERR_INVALID_ARGS;
    }
    
    send_hdr = ipc_create_header(server->sendbuf, ipc_hdr->msg_type, status, seqno);
    ipc_server_cli_sendmsg(cli, send_hdr, NULL, NULL);
    ipc_mutex_unlock(server->lock);
    
    return true;
}

static bool ipc_server_handle_unsubscribe(ipc_server_t *server, ipc_server_cli_t *cli, ipc_header_t *ipc_hdr, uint16_t seqno, const ipc_url_ref_t *url)
{
    uint32_t status = 0;
    ipc_server_sub_t *sub, *sub_temp;
    ipc_header_t *send_hdr;
    
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
    
    send_hdr = ipc_create_header(server->sendbuf, ipc_hdr->msg_type, status, seqno);
    ipc_server_cli_sendmsg(cli, send_hdr, NULL, NULL);
    ipc_mutex_unlock(server->lock);
    
    return true;
}

static bool ipc_server_handle_ping_echo(ipc_server_t *server, ipc_server_cli_t *cli, ipc_header_t *ipc_hdr, uint16_t seqno)
{
    ipc_header_t *send_hdr;
    
    send_hdr = ipc_create_header(server->sendbuf, ipc_hdr->msg_type, 0, seqno);
    ipc_server_cli_sendmsg(cli, send_hdr, NULL, NULL);
    ipc_mutex_unlock(server->lock);
    
    return true;
}

static bool ipc_server_input (ipc_header_t *ipc_hdr, void *arg)
{
    struct input_arg *input_arg = arg;
    ipc_server_t *server  = input_arg->server;
    ipc_server_cli_t *cli = input_arg->cli;
    uint16_t seqno;
    ipc_url_ref_t url;
    ipc_data_ref_t data;

    seqno = ipc_get_seqno(ipc_hdr);
    ipc_get_url(ipc_hdr, &url);
    ipc_get_data(ipc_hdr, &data);

    if (!cli->active) {
        cli->active = true;
    }

    if (ipc_hdr->msg_type == IPC_MSG_TYPE_MESSAGE) {
        if (server->onmsg) {
            server->onmsg(server, cli->id, &url, &data, server->msg_arg);
        }
        return  (server->valid);
    }

    ipc_mutex_lock(server->lock);

    switch (ipc_hdr->msg_type) {

    case IPC_MSG_TYPE_SERVICE_INFO:
        ipc_server_handle_service_info(server, cli, ipc_hdr, seqno);
        break;

    case IPC_MSG_TYPE_RPC_REQUEST:
        ipc_server_handle_rpc_request(server, cli, ipc_hdr, seqno, &url, &data);
        break;

    case IPC_MSG_TYPE_SUBSCRIBE:
        ipc_server_handle_subscribe(server, cli, ipc_hdr, seqno, &url);
        break;

    case IPC_MSG_TYPE_UNSUBSCRIBE:
        ipc_server_handle_unsubscribe(server, cli, ipc_hdr, seqno, &url);
        break;

    case IPC_MSG_TYPE_PING_ECHO:
        ipc_server_handle_ping_echo(server, cli, ipc_hdr, seqno);
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
static void ipc_server_handle_client_input(ipc_server_t *server, ipc_server_cli_t *cli, const fd_set *rfds)
{
    ssize_t num;
    struct input_arg input_arg;
    
    if (FD_ISSET(cli->sock, rfds)) {
        num = recv(cli->sock, server->recvbuf, IPC_MAX_PACKET_SIZE, MSG_DONTWAIT);
        if (num > 0) {
            input_arg.server = server;
            input_arg.cli = cli;
            ipc_stream_feed(&cli->recv, server->recvbuf, num, ipc_server_input, &input_arg);
        }

        if (num == 0 || (num < 0 && errno != EWOULDBLOCK)) {
            if (cli->onconn) {
                cli->onconn = false;
                if (server->oncli) {
                    server->oncli(server, cli->id, false, server->carg);
                }
            }

            ipc_mutex_lock(server->lock);
            ipc_server_cli_destroy(server, cli);
            ipc_mutex_unlock(server->lock);
        }
    }
}

static void ipc_server_handle_new_connection(ipc_server_t *server, const fd_set *rfds)
{
    int sock;
    socklen_t addr_len = sizeof(struct sockaddr_storage);
    struct sockaddr_storage addr;
    ipc_server_cli_t *cli;
    
    if (server->sock >= 0 && FD_ISSET(server->sock, rfds)) {
#ifdef __USE_GNU
        sock = accept4(server->sock, (struct sockaddr *)&addr, &addr_len, SOCK_NONBLOCK);
#else
        sock = accept(server->sock, (struct sockaddr *)&addr, &addr_len);
        /* Set nonblock */
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
        if (sock >= 0) {
            cli = (ipc_server_cli_t *)calloc(1, sizeof(ipc_server_cli_t));
            if (cli) {
                cli->sock = sock;
                cli->active = false;
                /* TODO: deal with init recv buffer. */
                ipc_stream_init(&cli->recv);
                ipc_socket_set_send_timeout(sock, server->send_timeout);

                ipc_mutex_lock(server->lock);
                ipc_server_cli_init(server, cli);
                ipc_mutex_unlock(server->lock);
            } else {
                ipc_socket_close(sock);
            }
        }
    }
}

static void ipc_server_handle_event_input(ipc_server_t *server, const fd_set *rfds)
{
    int evt_fd = ipc_event_pair_get_read_fd(server->evtfd);
    ipc_server_hst_t *hst, *hst_temp;
    ipc_server_cli_t *cli;
    
    if (FD_ISSET(evt_fd, rfds)) {
        ipc_event_pair_drain(server->evtfd);
        ipc_mutex_lock(server->lock);

        LIST_FOREACH_SAFE(hst, hst_temp, server->hst_h) {
            if (hst->alive == 0) {
                DELETE_FROM_LIST(hst, server->hst_h);

                cli = (ipc_server_cli_t *)((char *)hst - offsetof(ipc_server_cli_t, hst));
                ipc_socket_shutdown(cli->sock);
            }
        }

        ipc_mutex_unlock(server->lock);
    }
}

static void ipc_server_input_fds (ipc_server_t *server, const fd_set *rfds)
{
    int i;
    ipc_server_cli_t *cli, *cli_temp;

    if (!server || !server->valid) {
        return;
    }

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH_SAFE(cli, cli_temp, server->clis[i]) {
            ipc_server_handle_client_input(server, cli, rfds);
        }
    }

    ipc_server_handle_new_connection(server, rfds);
    ipc_server_handle_event_input(server, rfds);
}

/*
 * IPC server poll 
 */
int ipc_server_poll(ipc_server_t *server, int timeout_ms)
{
    fd_set fds;
    sigset_t empty_mask;
    struct timespec timeout = {
        .tv_sec  = timeout_ms / 1000,
        .tv_nsec = (timeout_ms % 1000) * 1000000LL
    };

    sigemptyset(&empty_mask);
    FD_ZERO(&fds);
    int max_fd = ipc_server_fds(server, &fds);
    // 阻塞空信号集，可以传递并中断所有信号
    int cnt;
    do {
        cnt = pselect(max_fd + 1, &fds, NULL, NULL, &timeout, &empty_mask);
    } while (cnt < 0 && errno == EINTR);
    if (cnt > 0) {
        ipc_server_input_fds(server, &fds);
        return 0;
    }
    return cnt;
}

void ipc_server_run(ipc_server_t *server)
{
    fd_set fds;
    sigset_t empty_mask;

    while (true) {
        sigemptyset(&empty_mask);
        FD_ZERO(&fds);
        int max_fd = ipc_server_fds(server, &fds);
        // 阻塞空信号集，可以传递并中断所有信号
        int cnt;
        do {
            cnt = pselect(max_fd + 1, &fds, NULL, NULL, NULL, &empty_mask);
        } while (cnt < 0 && errno == EINTR);
        if (cnt > 0) {
            ipc_server_input_fds(server, &fds);
            continue;
        }
    }
}

/*
 * end
 */
