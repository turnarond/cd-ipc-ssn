/*
 * Copyright (c) 2023 ACOAUTO Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: ipc_cliauto.c IPC client auto reconnect implementation.
 *
 * Author: Yan Chaodong <yanchaodong@acoinfo.com>
 *
 */

#include "cd_ipc_cliauto.h"
#include "cd_ipc_client.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Client auto state */
typedef enum {
    IPC_CLIENT_AUTO_STATE_IDLE,
    IPC_CLIENT_AUTO_STATE_CONNECTING,
    IPC_CLIENT_AUTO_STATE_CONNECTED,
    IPC_CLIENT_AUTO_STATE_DISCONNECTED,
    IPC_CLIENT_AUTO_STATE_STOPPED
} ipc_client_auto_state_t;

/* Client auto structure */
struct ipc_client_auto {
    ipc_client_t *client;             /* IPC client handle */
    ipc_client_msg_handler_t onmsg;    /* Message callback */
    ipc_client_conn_func_t onconn;     /* Connection callback */
    void *arg;                        /* User argument */
    void *conn_arg;                   /* Connection callback argument */
    
    char *server;                     /* Server address */
    char **urls;                      /* URLs to subscribe */
    int url_cnt;                      /* URL count */
    
    unsigned int keepalive;           /* Keepalive interval (ms) */
    unsigned int conn_timeout;        /* Connection timeout (ms) */
    unsigned int reconn_delay;        /* Reconnect delay (ms) */
    
    ipc_client_auto_state_t state;    /* Current state */
    pthread_t thread;                 /* Auto reconnect thread */
    int running;                      /* Thread running flag */
    int ping_lost;                    /* Ping lost count */
    
    pthread_mutex_t mutex;            /* Mutex for state protection */
    pthread_cond_t cond;              /* Condition variable for state changes */
};

/* Client auto thread function */
static void *ipc_client_auto_thread(void *arg);

/* IPC client message callback */
static void ipc_client_auto_msg_cb(ipc_client_t *client, ipc_url_ref_t *url, ipc_data_ref_t *data, void *arg);

/* IPC client connection callback */
static void ipc_client_auto_conn_cb(ipc_client_t *client, bool connected, void *arg);

/* Create client auto object */
ipc_client_auto_t *ipc_client_auto_create(ipc_client_msg_handler_t onmsg, void *arg)
{
    ipc_client_auto_t *cliauto = (ipc_client_auto_t *)malloc(sizeof(ipc_client_auto_t));
    if (!cliauto) {
        return NULL;
    }
    
    memset(cliauto, 0, sizeof(ipc_client_auto_t));
    cliauto->onmsg = onmsg;
    cliauto->arg = arg;
    cliauto->state = IPC_CLIENT_AUTO_STATE_IDLE;
    cliauto->running = 0;
    cliauto->ping_lost = 0;
    
    pthread_mutex_init(&cliauto->mutex, NULL);
    pthread_cond_init(&cliauto->cond, NULL);
    
    return cliauto;
}

/* Delete client auto object */
void ipc_client_auto_delete(ipc_client_auto_t *cliauto)
{
    if (!cliauto) {
        return;
    }
    
    /* Stop auto reconnect thread */
    ipc_client_auto_stop(cliauto);
    
    /* Cleanup resources */
    if (cliauto->client) {
        ipc_client_close(cliauto->client);
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
bool ipc_client_auto_setup(ipc_client_auto_t *cliauto, ipc_client_conn_func_t onconn, void *arg)
{
    if (!cliauto) {
        return false;
    }
    
    cliauto->onconn = onconn;
    cliauto->conn_arg = arg;
    
    return true;
}

/* Start client auto reconnect */
bool ipc_client_auto_start(ipc_client_auto_t *cliauto, const char *server, 
                            char * const urls[], int url_cnt, 
                            unsigned int keepalive, unsigned int conn_timeout, unsigned int reconn_delay)
{
    if (!cliauto || !server) {
        return false;
    }
    
    pthread_mutex_lock(&cliauto->mutex);
    
    if (cliauto->state != IPC_CLIENT_AUTO_STATE_IDLE && 
        cliauto->state != IPC_CLIENT_AUTO_STATE_STOPPED) {
        pthread_mutex_unlock(&cliauto->mutex);
        return false;
    }
    
    /* Save parameters */
    cliauto->server = strdup(server);
    if (!cliauto->server) {
        pthread_mutex_unlock(&cliauto->mutex);
        return false;
    }
    
    if (urls && url_cnt > 0) {
        cliauto->urls = (char **)malloc(sizeof(char *) * url_cnt);
        if (!cliauto->urls) {
            free(cliauto->server);
            cliauto->server = NULL;
            pthread_mutex_unlock(&cliauto->mutex);
            return false;
        }
        
        for (int i = 0; i < url_cnt; i++) {
            cliauto->urls[i] = strdup(urls[i]);
        }
        cliauto->url_cnt = url_cnt;
    }
    
    /* Set parameters with minimum values */
    cliauto->keepalive = keepalive < 50 ? 50 : keepalive;
    cliauto->conn_timeout = conn_timeout < 20 ? 20 : conn_timeout;
    cliauto->reconn_delay = reconn_delay < 20 ? 20 : reconn_delay;
    
    /* Create IPC client */
    cliauto->client = ipc_client_create(ipc_client_auto_msg_cb, cliauto);
    if (!cliauto->client) {
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
    cliauto->state = IPC_CLIENT_AUTO_STATE_CONNECTING;
    
    if (pthread_create(&cliauto->thread, NULL, ipc_client_auto_thread, cliauto) != 0) {
        cliauto->running = 0;
        cliauto->state = IPC_CLIENT_AUTO_STATE_STOPPED;
        
        ipc_client_close(cliauto->client);
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
bool ipc_client_auto_stop(ipc_client_auto_t *cliauto)
{
    if (!cliauto) {
        return false;
    }
    
    pthread_mutex_lock(&cliauto->mutex);
    
    if (cliauto->state == IPC_CLIENT_AUTO_STATE_IDLE || 
        cliauto->state == IPC_CLIENT_AUTO_STATE_STOPPED) {
        pthread_mutex_unlock(&cliauto->mutex);
        return false;
    }
    
    /* Signal thread to stop */
    cliauto->running = 0;
    cliauto->state = IPC_CLIENT_AUTO_STATE_STOPPED;
    
    pthread_cond_signal(&cliauto->cond);
    pthread_mutex_unlock(&cliauto->mutex);
    
    /* Wait for thread to exit */
    pthread_join(cliauto->thread, NULL);
    
    /* Cleanup resources */
    if (cliauto->client) {
        ipc_client_close(cliauto->client);
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
ipc_client_t *ipc_client_auto_handle(ipc_client_auto_t *cliauto)
{
    if (!cliauto) {
        return NULL;
    }
    
    return cliauto->client;
}

/* Client auto thread function */
static void *ipc_client_auto_thread(void *arg)
{
    ipc_client_auto_t *cliauto = (ipc_client_auto_t *)arg;
    struct timespec timeout;
    int ret;
    
    while (cliauto->running) {
        switch (cliauto->state) {
            case IPC_CLIENT_AUTO_STATE_CONNECTING:
                {
                    /* Set connection timeout */
                    timeout.tv_sec = cliauto->conn_timeout / 1000;
                    timeout.tv_nsec = (cliauto->conn_timeout % 1000) * 1000000;
                    
                    /* Try to connect */
                    ret = ipc_client_connect(cliauto->client, cliauto->server, &timeout);
                    if (ret) {
                        /* Connection successful */
                        cliauto->state = IPC_CLIENT_AUTO_STATE_CONNECTED;
                        cliauto->ping_lost = 0;
                        
                        /* Subscribe to URLs */
                        if (cliauto->urls && cliauto->url_cnt > 0) {
                            for (int i = 0; i < cliauto->url_cnt; i++) {
                                ipc_url_ref_t url = {
                                    .url = cliauto->urls[i],
                                    .url_len = strlen(cliauto->urls[i])
                                };
                                ipc_data_ref_t data = {
                                    .data = NULL,
                                    .length = 0
                                };
                                ipc_client_message(cliauto->client, &url, &data);
                            }
                        }
                        
                        /* Call connection callback */
                        if (cliauto->onconn) {
                            cliauto->onconn(cliauto->conn_arg, cliauto, true);
                        }
                    } else {
                        /* Connection failed, wait and retry */
                        usleep(cliauto->reconn_delay * 1000);
                    }
                }
                break;
                
            case IPC_CLIENT_AUTO_STATE_CONNECTED:
                {
                    /* Check if connection is still alive */
                    if (!ipc_client_is_connect(cliauto->client)) {
                        cliauto->state = IPC_CLIENT_AUTO_STATE_DISCONNECTED;
                        
                        /* Call connection callback */
                        if (cliauto->onconn) {
                            cliauto->onconn(cliauto->conn_arg, cliauto, false);
                        }
                        break;
                    }
                    
                    /* Send ping to keep connection alive */
                    // Note: IPC client might have its own ping mechanism
                    // For now, we just check connection status
                    
                    /* Wait for keepalive interval */
                    usleep(cliauto->keepalive * 1000);
                }
                break;
                
            case IPC_CLIENT_AUTO_STATE_DISCONNECTED:
                {
                    /* Wait before reconnecting */
                    usleep(cliauto->reconn_delay * 1000);
                    cliauto->state = IPC_CLIENT_AUTO_STATE_CONNECTING;
                }
                break;
                
            case IPC_CLIENT_AUTO_STATE_STOPPED:
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

/* IPC client message callback */
static void ipc_client_auto_msg_cb(ipc_client_t *client, ipc_url_ref_t *url, ipc_data_ref_t *data, void *arg)
{
    ipc_client_auto_t *cliauto = (ipc_client_auto_t *)arg;
    
    /* Forward message to user callback */
    if (cliauto->onmsg) {
        cliauto->onmsg(client, url, data, cliauto->arg);
    }
}

/* IPC client connection callback */
static void ipc_client_auto_conn_cb(ipc_client_t *client, bool connected, void *arg)
{
    ipc_client_auto_t *cliauto = (ipc_client_auto_t *)arg;
    
    /* Update connection state */
    if (connected) {
        cliauto->state = IPC_CLIENT_AUTO_STATE_CONNECTED;
        cliauto->ping_lost = 0;
    } else {
        cliauto->state = IPC_CLIENT_AUTO_STATE_DISCONNECTED;
    }
    
    /* Call user connection callback */
    if (cliauto->onconn) {
        cliauto->onconn(cliauto->conn_arg, cliauto, connected);
    }
}
