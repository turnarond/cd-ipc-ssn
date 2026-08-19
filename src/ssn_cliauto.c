/*
 * Copyright (c) 2023 ACOAUTO Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: ssn_cliauto.c SSN client auto reconnect implementation.
 *
 * Author: Yan Chaodong <yanchaodong@acoinfo.com>
 *
 */

#include "ssn_cliauto.h"
#include "ssn_client.h"
#include "util/ssn_log.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Client auto state */
typedef enum {
    SSN_CLIENT_AUTO_STATE_IDLE,
    SSN_CLIENT_AUTO_STATE_CONNECTING,
    SSN_CLIENT_AUTO_STATE_CONNECTED,
    SSN_CLIENT_AUTO_STATE_DISCONNECTED,
    SSN_CLIENT_AUTO_STATE_STOPPED
} ssn_client_auto_state_t;

/* Client auto structure */
struct ssn_client_auto {
    ssn_client_t *client;             /* SSN client handle */
    ssn_client_msg_handler_t onmsg;    /* Message callback */
    ssn_client_conn_func_t onconn;     /* Connection callback */
    void *arg;                        /* User argument */
    void *conn_arg;                   /* Connection callback argument */
    
    char *server;                     /* Server address */
    char **urls;                      /* URLs to subscribe */
    int url_cnt;                      /* URL count */
    
    unsigned int keepalive;           /* Keepalive interval (ms) */
    unsigned int conn_timeout;        /* Connection timeout (ms) */
    unsigned int reconn_delay;        /* Reconnect delay (ms) */
    
    ssn_client_auto_state_t state;    /* Current state */
    pthread_t thread;                 /* Auto reconnect thread */
    int running;                      /* Thread running flag */
    int ping_lost;                    /* Ping lost count */
    
    pthread_mutex_t mutex;            /* Mutex for state protection */
    pthread_cond_t cond;              /* Condition variable for state changes */
};

/* Client auto thread function */
static void *ssn_client_auto_thread(void *arg);

/* SSN client message callback */
static void ssn_client_auto_msg_cb(ssn_client_t *client, ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg);

/* Create client auto object */
ssn_client_auto_t *ssn_client_auto_create(void)
{
    ssn_client_auto_t *cliauto = (ssn_client_auto_t *)malloc(sizeof(ssn_client_auto_t));
    if (!cliauto) {
        return NULL;
    }

    memset(cliauto, 0, sizeof(ssn_client_auto_t));
    cliauto->state = SSN_CLIENT_AUTO_STATE_IDLE;
    cliauto->running = 0;
    cliauto->ping_lost = 0;

    pthread_mutex_init(&cliauto->mutex, NULL);
    pthread_cond_init(&cliauto->cond, NULL);

    return cliauto;
}

/* Delete client auto object */
void ssn_client_auto_delete(ssn_client_auto_t *cliauto)
{
    if (!cliauto) {
        return;
    }
    
    /* Stop auto reconnect thread */
    ssn_client_auto_stop(cliauto);
    
    /* Cleanup resources */
    if (cliauto->client) {
        ssn_client_close(cliauto->client);
        cliauto->client = NULL;
    }
    
    if (cliauto->server) {
        free(cliauto->server);
        cliauto->server = NULL;
    }
    
    if (cliauto->urls) {
        free(cliauto->urls);
        cliauto->urls = NULL;
    }
    
    pthread_mutex_destroy(&cliauto->mutex);
    pthread_cond_destroy(&cliauto->cond);
    
    free(cliauto);
}

/* Setup client auto callbacks */
bool ssn_client_auto_setup(ssn_client_auto_t *cliauto, ssn_client_conn_func_t onconn, void *arg)
{
    if (!cliauto) {
        return false;
    }
    
    cliauto->onconn = onconn;
    cliauto->conn_arg = arg;
    
    return true;
}

/* Start client auto reconnect */
bool ssn_client_auto_start(ssn_client_auto_t *cliauto, const char *server, 
                            char * const urls[], int url_cnt, 
                            unsigned int keepalive, unsigned int conn_timeout, unsigned int reconn_delay)
{
    if (!cliauto || !server) {
        LOG_ERROR("cliauto start: invalid argument (cliauto=%p, server=%p)",
                  (void *)cliauto, (void *)server);
        return false;
    }
    
    pthread_mutex_lock(&cliauto->mutex);
    
    if (cliauto->state != SSN_CLIENT_AUTO_STATE_IDLE && 
        cliauto->state != SSN_CLIENT_AUTO_STATE_STOPPED) {
        LOG_WARN("cliauto start: already running (state=%d), start rejected",
                 (int)cliauto->state);
        pthread_mutex_unlock(&cliauto->mutex);
        return false;
    }
    
    /* Save parameters */
    cliauto->server = strdup(server);
    if (!cliauto->server) {
        LOG_ERROR("cliauto start: strdup server failed (errno %d)", errno);
        pthread_mutex_unlock(&cliauto->mutex);
        return false;
    }
    
    if (urls && url_cnt > 0) {
        cliauto->urls = (char **)malloc(sizeof(char *) * url_cnt);
        if (!cliauto->urls) {
            LOG_ERROR("cliauto start: malloc urls failed (errno %d)", errno);
            free(cliauto->server);
            cliauto->server = NULL;
            pthread_mutex_unlock(&cliauto->mutex);
            return false;
        }
        
        for (int i = 0; i < url_cnt; i++) {
            if (!urls[i]) {
                LOG_ERROR("cliauto start: urls[%d] is NULL", i);
                for (int j = 0; j < i; j++) free(cliauto->urls[j]);
                free(cliauto->urls);
                cliauto->urls = NULL;
                free(cliauto->server);
                cliauto->server = NULL;
                pthread_mutex_unlock(&cliauto->mutex);
                return false;
            }
            cliauto->urls[i] = strdup(urls[i]);
            if (!cliauto->urls[i]) {
                LOG_ERROR("cliauto start: strdup urls[%d] failed (errno %d)", i, errno);
                for (int j = 0; j <= i; j++) free(cliauto->urls[j]);
                free(cliauto->urls);
                cliauto->urls = NULL;
                free(cliauto->server);
                cliauto->server = NULL;
                pthread_mutex_unlock(&cliauto->mutex);
                return false;
            }
        }
        cliauto->url_cnt = url_cnt;
    }
    
    /* Set parameters with minimum values */
    cliauto->keepalive = keepalive < 50 ? 50 : keepalive;
    cliauto->conn_timeout = conn_timeout < 20 ? 20 : conn_timeout;
    cliauto->reconn_delay = reconn_delay < 20 ? 20 : reconn_delay;
    
    /* Create SSN client and wire internal publish routing */
    cliauto->client = ssn_client_create();
    if (cliauto->client) {
        ssn_client_set_on_publish(cliauto->client, ssn_client_auto_msg_cb, cliauto);
    }
    if (!cliauto->client) {
        LOG_ERROR("cliauto start: ssn_client_create failed");
        if (cliauto->urls) {
            for (int i = 0; i < cliauto->url_cnt; i++) {
                free(cliauto->urls[i]);
            }
            free(cliauto->urls);
            cliauto->urls = NULL;
        }
        free(cliauto->server);
        cliauto->server = NULL;
        pthread_mutex_unlock(&cliauto->mutex);
        return false;
    }
    
    /* Start auto reconnect thread */
    cliauto->running = 1;
    cliauto->state = SSN_CLIENT_AUTO_STATE_CONNECTING;
    
    if (pthread_create(&cliauto->thread, NULL, ssn_client_auto_thread, cliauto) != 0) {
        LOG_ERROR("cliauto start: pthread_create failed (errno %d)", errno);
        cliauto->running = 0;
        cliauto->state = SSN_CLIENT_AUTO_STATE_STOPPED;
        
        ssn_client_close(cliauto->client);
        cliauto->client = NULL;
        
        if (cliauto->urls) {
            for (int i = 0; i < cliauto->url_cnt; i++) {
                free(cliauto->urls[i]);
            }
            free(cliauto->urls);
            cliauto->urls = NULL;
        }
        free(cliauto->server);
        cliauto->server = NULL;
        
        pthread_mutex_unlock(&cliauto->mutex);
        return false;
    }
    
    pthread_mutex_unlock(&cliauto->mutex);
    return true;
}

/* Stop client auto reconnect */
bool ssn_client_auto_stop(ssn_client_auto_t *cliauto)
{
    if (!cliauto) {
        return false;
    }
    
    pthread_mutex_lock(&cliauto->mutex);
    
    if (cliauto->state == SSN_CLIENT_AUTO_STATE_IDLE || 
        cliauto->state == SSN_CLIENT_AUTO_STATE_STOPPED) {
        pthread_mutex_unlock(&cliauto->mutex);
        return false;
    }
    
    /* Signal thread to stop */
    cliauto->running = 0;
    cliauto->state = SSN_CLIENT_AUTO_STATE_STOPPED;
    
    pthread_cond_signal(&cliauto->cond);
    pthread_mutex_unlock(&cliauto->mutex);
    
    /* Wait for thread to exit */
    pthread_join(cliauto->thread, NULL);
    
    /* Cleanup resources */
    if (cliauto->client) {
        ssn_client_close(cliauto->client);
        cliauto->client = NULL;
    }
    
    if (cliauto->server) {
        free(cliauto->server);
        cliauto->server = NULL;
    }
    
    if (cliauto->urls) {
        for (int i = 0; i < cliauto->url_cnt; i++) {
            free(cliauto->urls[i]);
        }
        free(cliauto->urls);
        cliauto->urls = NULL;
    }
    
    cliauto->url_cnt = 0;
    cliauto->ping_lost = 0;
    
    return true;
}

/* Get client handle */
ssn_client_t *ssn_client_auto_handle(ssn_client_auto_t *cliauto)
{
    if (!cliauto) {
        return NULL;
    }
    
    return cliauto->client;
}

/* Client auto thread function */
static void *ssn_client_auto_thread(void *arg)
{
    ssn_client_auto_t *cliauto = (ssn_client_auto_t *)arg;
    struct timespec timeout;
    bool connected;

    while (true) {
        /* 状态读写统一在 mutex 下（缺陷背景：原实现线程内无锁读写 state/running，
         * 与 stop 的锁内写构成数据竞争 UB；stop 后线程仍可能覆盖状态） */
        pthread_mutex_lock(&cliauto->mutex);
        if (!cliauto->running) {
            pthread_mutex_unlock(&cliauto->mutex);
            break;
        }
        ssn_client_auto_state_t state = cliauto->state;
        pthread_mutex_unlock(&cliauto->mutex);

        switch (state) {
            case SSN_CLIENT_AUTO_STATE_CONNECTING:
                {
                    /* Set connection timeout */
                    timeout.tv_sec = cliauto->conn_timeout / 1000;
                    timeout.tv_nsec = (cliauto->conn_timeout % 1000) * 1000000;

                    /* Try to connect */
                    connected = ssn_client_connect(cliauto->client,
                                                   cliauto->server, &timeout);
                    if (connected) {
                        /* Connection successful */
                        pthread_mutex_lock(&cliauto->mutex);
                        cliauto->state = SSN_CLIENT_AUTO_STATE_CONNECTED;
                        cliauto->ping_lost = 0;
                        pthread_mutex_unlock(&cliauto->mutex);

                        /* Subscribe to URLs */
                        if (cliauto->urls && cliauto->url_cnt > 0) {
                            for (int i = 0; i < cliauto->url_cnt; i++) {
                                ssn_url_ref_t url = {
                                    .url = cliauto->urls[i],
                                    .url_len = strlen(cliauto->urls[i])
                                };
                                ssn_client_subscribe(cliauto->client, &url,
                                                    NULL, NULL, cliauto->conn_timeout);
                            }
                        }

                        /* Call connection callback */
                        if (cliauto->onconn) {
                            cliauto->onconn(cliauto->conn_arg, cliauto, true);
                        }
                    } else {
                        /* Connection failed, wait and retry */
                        LOG_WARN("cliauto: connect to %s failed, retry in %u ms",
                                 cliauto->server, cliauto->reconn_delay);
                        usleep(cliauto->reconn_delay * 1000);
                    }
                }
                break;

            case SSN_CLIENT_AUTO_STATE_CONNECTED:
                {
                    /* 保活 ping（缺陷背景：原实现从不发送 PING_ECHO，半开连接——
                     * 服务端崩溃/网络中断且无 FIN/RST——永远感知不到，自动重连
                     * 永不触发；SSN_CLIENT_AUTO_MAX_PING_LOST 无任何引用）。
                     * 每次 tick：发送 PING_ECHO（服务端原样回显），连续
                     * SSN_CLIENT_AUTO_MAX_PING_LOST 次无应答判定断开。 */
                    if (ssn_client_ping(cliauto->client, 50)) {
                        pthread_mutex_lock(&cliauto->mutex);
                        cliauto->ping_lost = 0;
                        pthread_mutex_unlock(&cliauto->mutex);
                    } else {
                        pthread_mutex_lock(&cliauto->mutex);
                        cliauto->ping_lost++;
                        bool lost = cliauto->ping_lost >= SSN_CLIENT_AUTO_MAX_PING_LOST;
                        pthread_mutex_unlock(&cliauto->mutex);
                        if (lost) {
                            LOG_WARN("cliauto: ping lost %u times, connection dead",
                                     SSN_CLIENT_AUTO_MAX_PING_LOST);
                            pthread_mutex_lock(&cliauto->mutex);
                            cliauto->state = SSN_CLIENT_AUTO_STATE_DISCONNECTED;
                            pthread_mutex_unlock(&cliauto->mutex);

                            if (cliauto->onconn) {
                                cliauto->onconn(cliauto->conn_arg, cliauto, false);
                            }
                        }
                    }

                    /* 同时轮询以处理消息与 EOF 断开（FIN） */
                    ssn_client_poll(cliauto->client, 10);
                    if (!ssn_client_is_connect(cliauto->client)) {
                        pthread_mutex_lock(&cliauto->mutex);
                        cliauto->state = SSN_CLIENT_AUTO_STATE_DISCONNECTED;
                        pthread_mutex_unlock(&cliauto->mutex);

                        if (cliauto->onconn) {
                            cliauto->onconn(cliauto->conn_arg, cliauto, false);
                        }
                    }
                }
                break;

            case SSN_CLIENT_AUTO_STATE_DISCONNECTED:
                {
                    /* Wait before reconnecting */
                    usleep(cliauto->reconn_delay * 1000);
                    pthread_mutex_lock(&cliauto->mutex);
                    cliauto->state = SSN_CLIENT_AUTO_STATE_CONNECTING;
                    pthread_mutex_unlock(&cliauto->mutex);
                }
                break;

            case SSN_CLIENT_AUTO_STATE_STOPPED:
                /* Exit thread */
                return NULL;

            default:
                /* Idle state, wait for start */
                pthread_mutex_lock(&cliauto->mutex);
                pthread_cond_wait(&cliauto->cond, &cliauto->mutex);
                pthread_mutex_unlock(&cliauto->mutex);
                break;
        }
    }

    return NULL;
}

/* SSN client message callback */
static void ssn_client_auto_msg_cb(ssn_client_t *client, ssn_url_ref_t *url, ssn_data_ref_t *data, void *arg)
{
    ssn_client_auto_t *cliauto = (ssn_client_auto_t *)arg;

    /* Route through auto-client's onmsg if set, otherwise fall through */
    if (cliauto->onmsg) {
        cliauto->onmsg(client, url, data, cliauto->arg);
    }
}
