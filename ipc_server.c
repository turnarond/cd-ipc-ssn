/*
 * IPC server
 */

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "ipc_list.h"
#include "ipc_server.h"
#include "ipc_parser.h"
#include "ipc_platform.h"

/* Client hash */
#define IPC_CLI_HASH_SIZE  64
#define IPC_CLI_HASH_MASK  0x2f

/* Command hash */
#define IPC_CMD_HASH_SIZE  32
#define IPC_CMD_HASH_MASK  0x1f

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
    ipc_recv_t recv;
    int sock;
    ipc_cli_id_t id;
} ipc_server_cli_t;

/* Server command */
typedef struct ipc_server_cmd {
    struct ipc_server_cmd *next;
    struct ipc_server_cmd *prev;
    ipc_server_cmd_func_t callback;
    void *arg;
    size_t len;
    char url[1];
} ipc_server_cmd_t;

/* Server */
struct ipc_server {
    bool valid;
    char ifname[IF_NAMESIZE];
    ipc_cli_id_t ncid;
    ipc_server_t *next;
    ipc_server_t *prev;
    ipc_server_hst_t *hst_h;
    ipc_server_cli_t *clis[IPC_CLI_HASH_SIZE];
    ipc_server_cmd_t *cmds[IPC_CMD_HASH_SIZE];
    ipc_server_cmd_t *def_cmd;
    ipc_server_cmd_t *prefix_h;
    ipc_server_cmd_t *prefix_t;
    ipc_server_dat_func_t ondat;
    void *darg;
    ipc_server_cli_func_t oncli;
    void *carg;
    ipc_mutex_t lock;
    struct timeval send_timeout;
    int sock;
    int evtfd[2];
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

/* Default send timeout */
static const struct timeval ipc_server_def_send_timeout = {
    .tv_sec  = (IPC_SERVER_DEF_SEND_TIMEOUT / 1000),
    .tv_usec = (IPC_SERVER_DEF_SEND_TIMEOUT % 1000) * 1000
};

/* Server timer period (ms) */
#define IPC_SERVER_TIMER_PERIOD  100

/* Server list lock, header, timer thread */
static ipc_thread_t  ipc_server_timer;
static ipc_server_t *ipc_server_list = NULL;
static ipc_mutex_t   ipc_server_lock = IPC_MUTEX_INITIALIZER;

/*
 * Server timer thread handle
 */
static void *ipc_server_timer_handle (void *arg)
{
    bool emit;
    ipc_server_t *server;
    ipc_server_hst_t *hst;

    (void)arg;

    do {
        ipc_thread_msleep(IPC_SERVER_TIMER_PERIOD);

        ipc_mutex_lock(&ipc_server_lock);

        if (!ipc_server_list) {
            ipc_mutex_unlock(&ipc_server_lock);
            break;
        }

        LIST_FOREACH(server, ipc_server_list) {
            if (!server->hst_h) {
                continue;
            }

            emit = false;

            ipc_mutex_lock(&server->lock);

            LIST_FOREACH(hst, server->hst_h) {
                if (hst->alive > IPC_SERVER_TIMER_PERIOD) {
                    hst->alive -= IPC_SERVER_TIMER_PERIOD;
                } else {
                    emit = true;
                }
            }

            ipc_mutex_unlock(&server->lock);

            if (emit) {
                ipc_event_pair_signal(server->evtfd[1]);
            }
        }

        ipc_mutex_unlock(&ipc_server_lock);

    } while (true);

    ipc_thread_exit();

    return  (NULL);
}

/*
 * Command hash
 */
static int ipc_server_url_hash (const ipc_url_t *url)
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
static ipc_server_cli_t *ipc_server_cli_find (ipc_server_t *server, ipc_cli_id_t id)
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
static ipc_cli_id_t ipc_server_cli_newid (ipc_server_t *server)
{
    ipc_cli_id_t id;

    do {
        id = server->ncid;
        server->ncid++;
    } while (ipc_server_cli_find(server, id));

    return  (id);
}

/*
 * Initialize a client
 */
static void ipc_server_cli_init (ipc_server_t *server, ipc_server_cli_t *cli)
{
    int hash;

    cli->id = ipc_server_cli_newid(server);
    hash = ipc_server_cli_hash(cli->id);
    INSERT_TO_HEADER(cli, server->clis[hash]);

    cli->hst.alive = IPC_SERVER_DEF_HANDSHAKE_TIMEOUT;
    INSERT_TO_HEADER(&cli->hst, server->hst_h);

#ifndef IPC_INHERIT_NODELAY
    {
        int en = 1;
        setsockopt(cli->sock, IPPROTO_TCP, TCP_NODELAY, (const void *)&en, sizeof(int));
    }
#endif
}

/*
 * Destroy a client
 */
static void ipc_server_cli_destroy (ipc_server_t *server, ipc_server_cli_t *cli)
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

    close_socket(cli->sock);
    free(cli);
}

/*
 * Close a client
 */
bool ipc_server_cli_close (ipc_server_t *server, ipc_cli_id_t id)
{
    bool ret;
    ipc_server_cli_t *cli;

    if (!server || !server->valid) {
        return  (false);
    }

    ipc_mutex_lock(&server->lock);

    cli = ipc_server_cli_find(server, id);
    if (cli) {
        shutdown_socket(cli->sock);
        ret = true;
    } else {
        ret = false;
    }

    ipc_mutex_unlock(&server->lock);

    return  (ret);
}


/*
 * Client send
 */
static bool ipc_server_cli_sendmsg(ipc_server_cli_t *cli, ipc_header_t *ipc_hdr, 
    const ipc_url_t *url, const ipc_payload_t *payload)
{
    ssize_t len;
    struct iovec iov[4] = {
        {
            .iov_base = (void*)ipc_hdr,
            .iov_len = sizeof(ipc_header_t)
        }
    };
    struct msghdr msg = {
        .msg_iov = iov,
        .msg_iovlen = 1
    };
    if (url) {
        iov[msg.msg_iovlen].iov_base = url->url;
        iov[msg.msg_iovlen].iov_len = url->url_len;
        msg.msg_iovlen++;
    }
    if (payload) {
        if (payload->data) {
            iov[msg.msg_iovlen].iov_base = payload->data;
            iov[msg.msg_iovlen].iov_len = payload->data_len;
            msg.msg_iovlen++;
        }
    }

    len = sendmsg(cli->sock, &msg, 0); 

    if (len < 0) {
        shutdown_socket(cli->sock);
        return false;
    }
    return true;
}

/*
 * Client send
 */
static bool ipc_server_cli_send (ipc_server_t *server, ipc_server_cli_t *cli, size_t len)
{
    uint8_t *buffer = (uint8_t *)server->sendbuf;
    ssize_t num, total = 0;

    if (len == 0) {
        if (!ipc_parser_validate_header((ipc_header_t *)server->sendbuf, &len)) {
            return  (false);
        }
        if (len == 0) {
            return  (true);
        }
    }

    do {
        num = send(cli->sock, &buffer[total], len - total, MSG_NOSIGNAL);
        if (num > 0) {
            total += num;
        } else {
            shutdown_socket(cli->sock);
            break;
        }
    } while (total < len);

    return  (total == len);
}

/*
 * Client subscribe match
 */
static bool ipc_server_cli_sub_match (ipc_server_cli_t *cli, const ipc_url_t *url)
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

    return  (sub ? true : false);
}

/*
 * Create IPC server
 * Warning: This function must be mutually exclusive with the ipc_server_close() call
 */
ipc_server_t *ipc_server_create (const char *server_info)
{
    int err = 0;
    ipc_server_t *server;

    server = (ipc_server_t *)malloc(sizeof(ipc_server_t));
    if (!server) {
        return  (NULL);
    }

    bzero(server, sizeof(ipc_server_t));

    server->sock   = -1;

    if (ipc_mutex_init(&server->lock)) {
        goto    error;
    }

    if (!ipc_event_pair_create(server->evtfd)) {
        err = 1;
        goto    error;
    }

    server->sendbuf = malloc(IPC_MAX_PACKET_LENGTH * 2);
    if (!server->sendbuf) {
        err = 2;
        goto    error;
    }

    server->send_timeout = ipc_server_def_send_timeout;
    server->recvbuf      = (uint8_t *)server->sendbuf + IPC_MAX_PACKET_LENGTH;
    server->valid        = true;

    ipc_mutex_lock(&ipc_server_lock);

    if (ipc_server_list == NULL) {
        if (ipc_thread_create(&ipc_server_timer, ipc_server_timer_handle, NULL)) {
            ipc_mutex_unlock(&ipc_server_lock);
            err = 3;
            goto    error;
        }
    }

    INSERT_TO_HEADER(server, ipc_server_list);

    ipc_mutex_unlock(&ipc_server_lock);

    return  (server);

error:
    if (err > 2) {
        free(server->sendbuf);
    }
    if (err > 1) {
        ipc_event_pair_close(server->evtfd);
    }
    if (err > 0) {
        ipc_mutex_destroy(&server->lock);
    }

    free(server);
    return  (NULL);
}

/*
 * Start IPC server
 */
bool ipc_server_start (ipc_server_t *server, const char* ipc_path)
{
    int en = 1;

    struct sockaddr_un addr;
    socklen_t namelen = sizeof(addr);

    if (!server || !server->valid) {
        return  (false);
    }

    server->sock = create_socket(AF_UNIX, SOCK_STREAM, 0, false);
    if (server->sock < 0) {
        return  (false);
    }

    setsockopt(server->sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&en, sizeof(int));
    setsockopt(server->sock, IPPROTO_TCP, TCP_NODELAY, (const void *)&en, sizeof(int));

    strcpy(addr.sun_path, ipc_path);
    addr.sun_family = AF_UNIX;
    //清理旧的连接
    unlink(addr.sun_path);
    if (bind(server->sock, (struct sockaddr *)&addr, namelen)) {
        goto    error;
    }

    if (server->ifname[0]) {
        ipc_socket_bindif(server->sock, server->ifname);
    }

    listen(server->sock, IPC_SERVER_BACKLOG);

    return  (true);

error:
    if (server->sock >= 0) {
        close_socket(server->sock);
        server->sock = -1;
    }

    return  (false);
}

/*
 * Get IPC server address (must be called after `ipc_server_start`)
 */
bool ipc_server_address (ipc_server_t *server, struct sockaddr *addr, socklen_t *namelen)
{
    if (!server || !server->valid || server->sock < 0) {
        return  (false);
    }

    if (getsockname(server->sock, addr, namelen)) {
        return  (false);
    } else {
        return  (true);
    }
}

/*
 * Bind IPC server to specified network interface
 */
bool ipc_server_bind_if (ipc_server_t *server, const char *ifname)
{
    bool ret;

    if (!server || !server->valid) {
        return  (false);
    }
    if (!ifname || strlen(ifname) >= IF_NAMESIZE) {
        return  (false);
    }

    strcpy(server->ifname, ifname);
    ret = true;

    if (server->sock >= 0 && !ipc_socket_bindif(server->sock, ifname)) {
        ret = false;
    }

    return  (ret);
}

/*
 * Close IPC server
 * Warning: This function must be mutually exclusive with the ipc_server_create() call
 */
void ipc_server_close (ipc_server_t *server)
{
    int i;
    bool wait_thread_quit = false;
    ipc_server_cli_t *cli, *cli_temp;
    ipc_server_cmd_t *cmd, *cmd_temp;

    if (!server || !server->valid) {
        return;
    }

    ipc_mutex_lock(&ipc_server_lock);

    DELETE_FROM_LIST(server, ipc_server_list);
    if (ipc_server_list == NULL) {
        wait_thread_quit = true;
    }

    ipc_mutex_unlock(&ipc_server_lock);

    if (wait_thread_quit) {
        ipc_thread_wait(&ipc_server_timer);
    }

    ipc_mutex_lock(&server->lock);

    server->valid = false;
    ipc_memory_barrier();

    if (server->sock >= 0) {
        close_socket(server->sock);
        server->sock = -1;
    }

    ipc_event_pair_close(server->evtfd);
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

    ipc_mutex_unlock(&server->lock);
    ipc_mutex_destroy(&server->lock);
    free(server);
}

/*
 * IPC server set on client callback
 */
void ipc_server_on_cli (ipc_server_t *server, ipc_server_cli_func_t oncli, void *arg)
{
    if (server) {
        server->oncli = oncli;
        server->carg  = arg;
    }
}

/*
 * IPC remote clients count
 */
int ipc_server_count (ipc_server_t *server)
{
    int i, cnt = 0;
    ipc_server_cli_t *cli;

    if (!server || !server->valid) {
        return  (0);
    }

    ipc_mutex_lock(&server->lock);

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH(cli, server->clis[i]) {
            if (cli->active) {
                cnt++;
            }
        }
    }

    ipc_mutex_unlock(&server->lock);

    return  (cnt);
}

/*
 * IPC server set this server send packet to client timeout.
 * `timeout` NULL means use default: IPC_SERVER_DEF_SEND_TIMEOUT
 */
bool ipc_server_send_timeout (ipc_server_t *server, bool cur_clis, const struct timespec *timeout)
{
    int i;
    struct timeval timeval;
    ipc_server_cli_t *cli;

    if (!server || !server->valid) {
        return  (false);
    }

    if (timeout) {
        timeval.tv_sec  = timeout->tv_sec;
        timeval.tv_usec = timeout->tv_nsec / 1000;
    } else {
        timeval = ipc_server_def_send_timeout;
    }

    ipc_mutex_lock(&server->lock);

    server->send_timeout = timeval;

    if (cur_clis) {
        for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
            LIST_FOREACH(cli, server->clis[i]) {
                ipc_socket_sndto(cli->sock, &timeval);
            }
        }
    }

    ipc_mutex_unlock(&server->lock);

    return  (true);
}

/*
 * IPC server is subscribed
 */
bool ipc_server_is_subscribed (ipc_server_t *server, const ipc_url_t *url)
{
    ipc_server_cli_t *cli;
    int i;

    if (!server || !server->valid) {
        return  (false);
    }
    if (!url || !url->url || !url->url_len) {
        return  (false);
    }

    ipc_mutex_lock(&server->lock);

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH(cli, server->clis[i]) {
            if (!cli->active) {
                continue;
            }
            if (ipc_server_cli_sub_match(cli, url)) {
                ipc_mutex_unlock(&server->lock);
                return  (true);
            }
        }
    }

    ipc_mutex_unlock(&server->lock);

    return  (false);
}

/*
 * IPC server do publish
 */
static bool ipc_server_do_publish (ipc_server_t *server, const ipc_url_t *url, const ipc_payload_t *payload)
{
    int i;
    size_t len;
    ipc_header_t *ipc_hdr;
    ipc_server_cli_t *cli;

    if (!server || !server->valid) {
        return  (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        return  (false);
    }

    ipc_mutex_lock(&server->lock);

    ipc_hdr = ipc_parser_init_header(server->sendbuf, IPC_TYPE_PUBLISH, 0, 0);

    if (!ipc_parser_set_url(ipc_hdr, url)) {
        ipc_mutex_unlock(&server->lock);
        return  (false);
    }
    if (payload) {
        if (!ipc_parser_set_payload(ipc_hdr, payload)) {
            ipc_mutex_unlock(&server->lock);
            return  (false);
        }
    }

    if (!ipc_parser_validate_header(ipc_hdr, &len)) {
        ipc_mutex_unlock(&server->lock);
        return  (false);
    }

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH(cli, server->clis[i]) {
            if (!cli->active) {
                continue;
            }
            if (ipc_server_cli_sub_match(cli, url)) {
                ipc_server_cli_sendmsg(cli, ipc_hdr, url, payload);
            }
        }
    }

    ipc_mutex_unlock(&server->lock);

    return  (true);
}

/*
 * IPC server publish
 */
bool ipc_server_publish (ipc_server_t *server, const ipc_url_t *url, const ipc_payload_t *payload)
{
    return  (ipc_server_do_publish(server, url, payload));
}

/*
 * IPC server add RPC listener
 */
bool ipc_server_add_listener (ipc_server_t *server,
                               const ipc_url_t *url, ipc_server_cmd_func_t callback, void *arg)
{
    int hash;
    size_t path_len;
    bool def, prefix;
    ipc_server_cmd_t *cmd, *need_free = NULL;

    if (!server || !server->valid) {
        return  (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/' || !callback) {
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

    cmd = (ipc_server_cmd_t *)malloc(sizeof(ipc_server_cmd_t) + url->url_len);
    if (!cmd) {
        return  (false);
    }

    bzero(cmd, sizeof(ipc_server_cmd_t));
    cmd->callback = callback;
    cmd->arg = arg;
    cmd->len = path_len;
    memcpy(cmd->url, url->url, path_len);
    cmd->url[path_len] = '\0';

    ipc_mutex_lock(&server->lock);

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

    ipc_mutex_unlock(&server->lock);

    if (need_free) {
        free(need_free);
    }

    return  (true);
}

/*
 * IPC server remove RPC listener
 */
void ipc_server_remove_listener (ipc_server_t *server, const ipc_url_t *url)
{
    int hash;
    size_t path_len;
    bool def, prefix;
    ipc_server_cmd_t *cmd, *cmd_temp, **header;

    if (!server || !server->valid) {
        return;
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
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

    ipc_mutex_lock(&server->lock);

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

    ipc_mutex_unlock(&server->lock);

    if (cmd) {
        free(cmd);
    }
}

/*
 * IPC remote client is subscribed
 */
bool ipc_server_cli_is_subscribed (ipc_server_t *server, ipc_cli_id_t id, const ipc_url_t *url)
{
    bool ret;
    ipc_server_cli_t *cli;

    if (!server || !server->valid) {
        return  (false);
    }
    if (!url || !url->url || !url->url_len) {
        return  (false);
    }

    ipc_mutex_lock(&server->lock);

    cli = ipc_server_cli_find(server, id);
    if (cli && cli->active) {
        ret = ipc_server_cli_sub_match(cli, url);
    } else {
        ret = false;
    }

    ipc_mutex_unlock(&server->lock);

    return  (ret);
}

/*
 * IPC remote client address
 */
bool ipc_server_cli_address (ipc_server_t *server, ipc_cli_id_t id, struct sockaddr *addr, socklen_t *namelen)
{
    ipc_server_cli_t *cli;

    if (!server || !server->valid) {
        return  (false);
    }

    ipc_mutex_lock(&server->lock);

    cli = ipc_server_cli_find(server, id);
    if (!cli || getpeername(cli->sock, addr, namelen)) {
        ipc_mutex_unlock(&server->lock);
        return  (false);
    }

    ipc_mutex_unlock(&server->lock);

    return  (true);
}

/*
 * IPC server RPC reply
 */
bool ipc_server_cli_reply (ipc_server_t *server, ipc_cli_id_t id,
                            uint8_t status, uint16_t seqno, const ipc_payload_t *payload)
{
    bool ret;
    ipc_server_cli_t *cli;
    ipc_header_t *ipc_hdr;

    if (!server || !server->valid) {
        return  (false);
    }

    ipc_mutex_lock(&server->lock);

    cli = ipc_server_cli_find(server, id);
    if (!cli) {
        ipc_mutex_unlock(&server->lock);
        return  (false);
    }

    ipc_hdr = ipc_parser_init_header(server->sendbuf, IPC_TYPE_RPC, status, seqno);

    if (payload) {
        if (!ipc_parser_set_payload(ipc_hdr, payload)) {
            ipc_mutex_unlock(&server->lock);
            return  (false);
        }
    }

    ret = ipc_server_cli_sendmsg(cli, ipc_hdr, NULL, payload);

    ipc_mutex_unlock(&server->lock);

    return  (ret);
}

/*
 * IPC remote client keepalive
 */
bool ipc_server_cli_keepalive (ipc_server_t *server, ipc_cli_id_t id, int keepalive)
{
    int en = 1;
    ipc_server_cli_t *cli;

#if !defined(__QNX__)
    int count = 3, idle = IPC_SERVER_KEEPALIVE_TIMEOUT;
#endif

    if (!server || !server->valid) {
        return  (false);
    }

    ipc_mutex_lock(&server->lock);

    cli = ipc_server_cli_find(server, id);
    if (!cli) {
        ipc_mutex_unlock(&server->lock);
        return  (false);
    }

    setsockopt(cli->sock, SOL_SOCKET, SO_KEEPALIVE, (const void *)&en, sizeof(int));

#if !defined(__QNX__)
    if (keepalive > 0) {
        idle = keepalive;
    }
    setsockopt(cli->sock, IPPROTO_TCP, TCP_KEEPIDLE, (const void *)&idle, sizeof(int));
    setsockopt(cli->sock, IPPROTO_TCP, TCP_KEEPINTVL, (const void *)&idle, sizeof(int));
    setsockopt(cli->sock, IPPROTO_TCP, TCP_KEEPCNT, (const void *)&count, sizeof(int));
#endif /*!__QNX__ */

    ipc_mutex_unlock(&server->lock);

    return  (true);
}

/*
 * IPC server get remote client id array
 */
int ipc_server_cli_array (ipc_server_t *server, ipc_cli_id_t ids[], int max_cnt)
{
    int i, cnt;
    ipc_server_cli_t *cli;

    if (!server || !server->valid || !ids || max_cnt <= 0) {
        return  (0);
    }

    cnt = 0;

    ipc_mutex_lock(&server->lock);

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH(cli, server->clis[i]) {
            ids[cnt++] = cli->id;
            if (cnt >= max_cnt) {
                goto    out;
            }
        }
    }

out:
    ipc_mutex_unlock(&server->lock);

    return  (cnt);
}

/*
 * IPC server set send packet to client timeout, NULL means send wait forever when congested.
 */
bool ipc_server_cli_send_timeout (ipc_server_t *server, ipc_cli_id_t id, const struct timespec *timeout)
{
    struct timeval timeval;
    ipc_server_cli_t *cli;

    if (!server || !server->valid) {
        return  (false);
    }

    if (timeout) {
        timeval.tv_sec  = timeout->tv_sec;
        timeval.tv_usec = timeout->tv_nsec / 1000;
    } else {
        timeval = server->send_timeout;
    }

    ipc_mutex_lock(&server->lock);

    cli = ipc_server_cli_find(server, id);
    if (cli) {
        ipc_socket_sndto(cli->sock, &timeval);
    }

    ipc_mutex_unlock(&server->lock);

    return  (cli ? true : false);
}

/*
 * IPC server send datagram
 */
bool ipc_server_cli_do_datagram (ipc_server_t *server, ipc_cli_id_t id, const ipc_url_t *url, const ipc_payload_t *payload)
{
    bool ret;
    size_t len;
    ipc_server_cli_t *cli;
    ipc_header_t *ipc_hdr;

    if (!server || !server->valid) {
        return  (false);
    }
    if (!url || !url->url || !url->url_len || url->url[0] != '/') {
        return  (false);
    }
    if (!payload) {
        return  (false);
    }

    ipc_mutex_lock(&server->lock);

    cli = ipc_server_cli_find(server, id);
    if (!cli) {
        ipc_mutex_unlock(&server->lock);
        return  (false);
    }

    ipc_hdr = ipc_parser_init_header(server->sendbuf, IPC_TYPE_DATAGRAM, 0, 0);

    if (!ipc_parser_set_url(ipc_hdr, url)) {
        ipc_mutex_unlock(&server->lock);
        return  (false);
    }

    if (!ipc_parser_set_payload(ipc_hdr, payload)) {
        ipc_mutex_unlock(&server->lock);
        return  (false);
    }

    if (!ipc_parser_validate_header(ipc_hdr, &len)) {
        ipc_mutex_unlock(&server->lock);
        return  (false);
    }

    ret = ipc_server_cli_sendmsg(cli, ipc_hdr, url, payload);

    ipc_mutex_unlock(&server->lock);

    return  (ret);
}

/*
 * IPC server send datagram
 */
bool ipc_server_cli_datagram (ipc_server_t *server, ipc_cli_id_t id, const ipc_url_t *url, const ipc_payload_t *payload)
{
    return  (ipc_server_cli_do_datagram(server, id, url, payload));
}

/*
 * IPC server set on datagram callback
 */
void ipc_server_on_datagram (ipc_server_t *server, ipc_server_dat_func_t callback, void *arg)
{
    if (server) {
        server->ondat = callback;
        server->darg  = arg;
    }
}

/*
 * IPC server checking event
 */
int ipc_server_fds (ipc_server_t *server, fd_set *rfds)
{
    int i, max_fd;
    ipc_server_cli_t *cli;

    if (!server || !server->valid || server->sock < 0) {
        return  (-1);
    }

    FD_SET(server->sock, rfds);
    max_fd = server->sock;

    FD_SET(server->evtfd[0], rfds);
    if (max_fd < server->evtfd[0]) {
        max_fd = server->evtfd[0];
    }

    ipc_mutex_lock(&server->lock);

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH(cli, server->clis[i]) {
            FD_SET(cli->sock, rfds);
            if (max_fd < cli->sock) {
                max_fd = cli->sock;
            }
        }
    }

    ipc_mutex_unlock(&server->lock);

    return  (max_fd);
}

/*
 * Command match
 */
static ipc_server_cmd_t *ipc_server_cmd_match (ipc_server_t *server, const ipc_url_t *url)
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

/*
 * IPC server packet input
 */
static bool ipc_server_input (void *arg, ipc_header_t *ipc_hdr)
{
    bool pass;
    uint8_t status, hs_buf[6];
    uint16_t seqno, cid;
    struct input_arg *input_arg = arg;
    ipc_server_t *server  = input_arg->server;
    ipc_server_cli_t *cli = input_arg->cli;
    ipc_server_sub_t *sub, *sub_temp;
    ipc_server_cmd_t *cmd;
    ipc_server_cmd_func_t callback;
    ipc_header_t *send_hdr;
    ipc_url_t url;
    ipc_payload_t payload, payload_reply;

    if (ipc_hdr->type == IPC_TYPE_NOOP || ipc_hdr->type == IPC_FLAG_REPLY) {
        return  (true);
    }

    seqno = ipc_parser_get_seqno(ipc_hdr);
    ipc_parser_get_url(ipc_hdr, &url);
    ipc_parser_get_payload(ipc_hdr, &payload);

    if (!cli->active) {
        cli->active = true;
    }

    if (ipc_hdr->type == IPC_TYPE_DATAGRAM) {
        if (server->ondat) {
            server->ondat(server->darg, server, cli->id, &url, &payload);
        }
        return  (server->valid);
    }

    ipc_mutex_lock(&server->lock);

    switch (ipc_hdr->type) {

    case IPC_TYPE_SERVINFO:
        send_hdr = ipc_parser_init_header(server->sendbuf, ipc_hdr->type, 0, seqno);
        cid = htonl(cli->id);
        payload_reply.data = &cid;
        payload_reply.data_len = sizeof(uint32_t);
        ipc_parser_set_payload(send_hdr, &payload_reply);
        ipc_server_cli_sendmsg(cli, send_hdr, NULL, &payload_reply);
        if (cli->hst.alive) {
            cli->hst.alive = 0;
            DELETE_FROM_LIST(&cli->hst, server->hst_h);
        }
        ipc_mutex_unlock(&server->lock);
        if (!cli->onconn) {
            cli->onconn = true;
            if (server->oncli) {
                server->oncli(server->carg, server, cli->id, true);
            }
        }
        break;

    case IPC_TYPE_RPC:
        if (url.url_len && url.url[0] == '/') {
            cmd = ipc_server_cmd_match(server, &url);
            if (cmd) {
                callback = cmd->callback;
                arg      = cmd->arg;
                ipc_mutex_unlock(&server->lock);
                callback(arg, server, cli->id, ipc_hdr, &url, &payload);
            } else {
                ipc_parser_init_header(server->sendbuf, ipc_hdr->type, IPC_STATUS_INVALID_URL, seqno);
                ipc_server_cli_sendmsg(cli, ipc_hdr, NULL, NULL);
                ipc_mutex_unlock(&server->lock);
            }
        } else {
            ipc_parser_init_header(server->sendbuf, ipc_hdr->type, IPC_STATUS_ARGUMENTS, seqno);
            ipc_server_cli_sendmsg(cli, ipc_hdr, NULL, NULL);
            ipc_mutex_unlock(&server->lock);
        }
        break;

    case IPC_TYPE_SUBSCRIBE:
        if (url.url_len && url.url[0] == '/') {
            LIST_FOREACH(sub, cli->subscribed) {
                if (sub->len == url.url_len && !memcmp(sub->url, url.url, sub->len)) {
                    break;
                }
            }
            if (!sub) {
                sub = (ipc_server_sub_t *)malloc(sizeof(ipc_server_sub_t) + url.url_len);
                if (!sub) {
                    status = IPC_STATUS_NO_MEMORY;
                } else {
                    bzero(sub, sizeof(ipc_server_sub_t));
                    sub->len = url.url_len;
                    memcpy(sub->url, url.url, sub->len);
                    sub->url[sub->len] = '\0';
                    INSERT_TO_HEADER(sub, cli->subscribed);
                    status = 0;
                }
            } else {
                status = 0;
            }
        } else {
            status = IPC_STATUS_ARGUMENTS;
        }
        ipc_parser_init_header(server->sendbuf, ipc_hdr->type, status, seqno);
        ipc_server_cli_sendmsg(cli, ipc_hdr, NULL, NULL);
        ipc_mutex_unlock(&server->lock);
        break;

    case IPC_TYPE_UNSUBSCRIBE:
        if (url.url_len && url.url[0] == '/') {
            LIST_FOREACH_SAFE(sub, sub_temp, cli->subscribed) {
                if (url.url_len != sub->len || memcmp(sub->url, url.url, sub->len)) {
                    continue;
                }
                DELETE_FROM_LIST(sub, cli->subscribed);
                free(sub);
                break;
            }
            status = 0;
        } else {
            LIST_FOREACH_SAFE(sub, sub_temp, cli->subscribed) {
                DELETE_FROM_LIST(sub, cli->subscribed);
                free(sub);
            }
            status = 0;
        }
        ipc_parser_init_header(server->sendbuf, ipc_hdr->type, status, seqno);
        ipc_server_cli_sendmsg(cli, ipc_hdr, NULL, NULL);
        ipc_mutex_unlock(&server->lock);
        break;

    case IPC_TYPE_PINGECHO:
        ipc_parser_init_header(server->sendbuf, ipc_hdr->type, 0, seqno);
        ipc_server_cli_sendmsg(cli, ipc_hdr, NULL, NULL);
        ipc_mutex_unlock(&server->lock);
        break;

    default:
        ipc_mutex_unlock(&server->lock);
        break;
    }

    return  (server->valid);
}

/*
 * IPC server input event
 */
void ipc_server_input_fds (ipc_server_t *server, const fd_set *rfds)
{
    int i;
    int sock;
    ssize_t num;
    socklen_t addr_len = sizeof(struct sockaddr_storage);
    struct sockaddr_storage addr;
    ipc_header_t *ipc_hdr;
    ipc_cli_id_t id;
    ipc_server_cli_t *cli, *cli_temp;
    ipc_server_hst_t *hst, *hst_temp;
    struct input_arg input_arg;

    if (!server || !server->valid) {
        return;
    }

    for (i = 0; i < IPC_CLI_HASH_SIZE; i++) {
        LIST_FOREACH_SAFE(cli, cli_temp, server->clis[i]) {
            if (FD_ISSET(cli->sock, rfds)) {
                num   = recv(cli->sock, server->recvbuf, IPC_MAX_PACKET_LENGTH, MSG_DONTWAIT);
                if (num > 0) {
                    input_arg.server = server;
                    input_arg.cli    = cli;
                    ipc_parser_input(&cli->recv, server->recvbuf,
                                        num, ipc_server_input, &input_arg);
                }

                if (num == 0 || (num < 0 && errno != EWOULDBLOCK)) {
                    if (cli->onconn) {
                        cli->onconn = false;
                        if (server->oncli) {
                            server->oncli(server->carg, server, cli->id, false);
                        }
                    }

                    ipc_mutex_lock(&server->lock);

                    ipc_server_cli_destroy(server, cli);

                    ipc_mutex_unlock(&server->lock);
                }
            }
        }
    }

    if (server->sock >= 0 && FD_ISSET(server->sock, rfds)) {
        sock = accept(server->sock, (struct sockaddr *)&addr, &addr_len);
        if (sock >= 0) {
            cli = (ipc_server_cli_t *)malloc(sizeof(ipc_server_cli_t));
            if (cli) {
                bzero(cli, sizeof(ipc_server_cli_t));
                cli->sock   = sock;
                cli->active = false;
                /* TODO: deal with init recv buffer. */
                ipc_parser_init_recv(&cli->recv);
                ipc_socket_sndto(sock, &server->send_timeout);

                ipc_mutex_lock(&server->lock);
                ipc_server_cli_init(server, cli);
                ipc_mutex_unlock(&server->lock);

            } else {
                close_socket(sock);
            }
        }
    }

    if (FD_ISSET(server->evtfd[0], rfds)) {
        ipc_event_pair_fetch(server->evtfd[0]);

        ipc_mutex_lock(&server->lock);

        LIST_FOREACH_SAFE(hst, hst_temp, server->hst_h) {
            if (hst->alive <= IPC_SERVER_DEF_HANDSHAKE_TIMEOUT) {
                hst->alive = 0;
                DELETE_FROM_LIST(hst, server->hst_h);

                cli = (ipc_server_cli_t *)((char *)hst - offsetof(ipc_server_cli_t, hst));
                shutdown_socket(cli->sock);
            }
        }

        ipc_mutex_unlock(&server->lock);
    }
}

/*
 * end
 */
