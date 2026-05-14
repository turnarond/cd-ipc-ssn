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

/* SSN client connection callback */
static void ssn_client_auto_conn_cb(ssn_client_t *client, bool connected, void *arg);

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
        return false;
    }
    
    pthread_mutex_lock(&cliauto->mutex);
    
    if (cliauto->state != SSN_CLIENT_AUTO_STATE_IDLE && 
        cliauto->state != SSN_CLIENT_AUTO_STATE_STOPPED) {
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
    
    /* Create SSN client and wire internal publish routing */
    cliauto->client = ssn_client_create();
    if (cliauto->client) {
        ssn_client_set_on_publish(cliauto->client, ssn_client_auto_msg_cb, cliauto);
    }
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
    cliauto->state = SSN_CLIENT_AUTO_STATE_CONNECTING;
    
    if (pthread_create(&cliauto->thread, NULL, ssn_client_auto_thread, cliauto) != 0) {
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

    while (cliauto->running) {
        switch (cliauto->state) {
            case SSN_CLIENT_AUTO_STATE_CONNECTING:
                {
                    /* Set connection timeout */
                    timeout.tv_sec = cliauto->conn_timeout / 1000;
                    timeout.tv_nsec = (cliauto->conn_timeout % 1000) * 1000000;
                    
                    /* Try to connect */
                    bool connected = ssn_client_connect(cliauto->client, cliauto->server, &timeout);
                    if (connected) {
                        /* Connection successful */
                        cliauto->state = SSN_CLIENT_AUTO_STATE_CONNECTED;
                        cliauto->ping_lost = 0;

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
                        usleep(cliauto->reconn_delay * 1000);
                    }
                }
                break;
                
            case SSN_CLIENT_AUTO_STATE_CONNECTED:
                {
                    /* Block in poll for keepalive interval to detect
                     * disconnection and process incoming messages */
                    ssn_client_poll(cliauto->client, cliauto->keepalive);

                    /* Check if connection is still alive */
                    if (!ssn_client_is_connect(cliauto->client)) {
                        cliauto->state = SSN_CLIENT_AUTO_STATE_DISCONNECTED;

                        /* Call connection callback */
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
                    cliauto->state = SSN_CLIENT_AUTO_STATE_CONNECTING;
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

/* SSN client connection callback */
static void ssn_client_auto_conn_cb(ssn_client_t *client, bool connected, void *arg)
{
    ssn_client_auto_t *cliauto = (ssn_client_auto_t *)arg;
    
    /* Update connection state */
    if (connected) {
        cliauto->state = SSN_CLIENT_AUTO_STATE_CONNECTED;
        cliauto->ping_lost = 0;
    } else {
        cliauto->state = SSN_CLIENT_AUTO_STATE_DISCONNECTED;
    }
    
    /* Call user connection callback */
    if (cliauto->onconn) {
        cliauto->onconn(cliauto->conn_arg, cliauto, connected);
    }
}
