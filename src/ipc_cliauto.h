/*
 * Copyright (c) 2023 ACOAUTO Team.
 * All rights reserved.
 *
 * Detailed license information can be found in the LICENSE file.
 *
 * File: vsoa_cliauto.h Vehicle SOA client auto robot.
 *
 * Author: Han.hui <hanhui@acoinfo.com>
 *
 */

#ifndef IPC_CLIAUTO_H
#define IPC_CLIAUTO_H

#include "ipc_client.h"

/* This module is a VSOA client extension module that provides automatic client connection, 
 * automatic handling of broken links, automatic subscribe to events, and automatic processing of event loops. 
 * Using this robot module can reduce the size of client code, 
 * and developers only need to focus on the business itself */

/* Warning: Except `ipc_client_auto_handle` other functions are not allowed to be called in this client
 * event loop thread context is not allowed to be called in the client event loop,
 * such as asynchronous RPC callback, or subscription message callback, etc. */

/* Max ping lost count */
#define VSOA_CLIENT_AUTO_MAX_PING_LOST  3

#ifdef __cplusplus
extern "C" {
#endif

/* Structure declaration */
struct ipc_client_auto;

/* Client auto type */
typedef struct ipc_client_auto ipc_client_auto_t;

/* Client on connect callback 
 * If `connect` is true, it means the connection is successful and the subscription is successful.
 * At this time, the robot has performed a subscription operation and has not yet performed data consistency operations.
 * If `connect` is false, it means the link is disconnected. When the link is disconnected, 
 * the client auto module will automatically reconnect.
 * When the connection is successful, `info` indicates the server information */
typedef void (*ipc_client_conn_func_t)(void *arg, ipc_client_auto_t *cliauto, bool connect);

/* VSOA client auto create 
 * The callback function will be called in the client auto thread context */
ipc_client_auto_t *ipc_client_auto_create(ipc_client_msg_handler_t onmsg, void *arg);

/* VSOA client auto delete
 * After this function is called, the `cliauto` object becomes invalid and is not allowed to be used again */
void ipc_client_auto_delete(ipc_client_auto_t *cliauto);

/* VSOA client auto setup
 * The callback function will be called in the client auto thread context */
bool ipc_client_auto_setup(ipc_client_auto_t *cliauto, ipc_client_conn_func_t onconn, void *arg);

/* VSOA client auto start
 * `server` can be ip:port or service hostname, client auto will automatically use position to query hostname address.
 * `urls` and `cnt`: URLs to subscribe to when the link is established.
 * `keepalive`: How often (millisecond) to perform a ping operation, the minimum is 50ms, and the ping timeout is same as this value.
 * `conn_timeout`: Connection timeout, unit: millisecond, the minimum is 20ms.
 * `reconn_delay`: When the connection times out or the password is incorrect or the network is disconnected, 
 *                 the retry waiting time, unit: millisecond, the minimum is 20ms.
 * If turbo ping is enable, `keepalive` is recommended to be an integer multiple of `turbo`. */
bool ipc_client_auto_start(ipc_client_auto_t *cliauto, const char *server, 
                            char * const urls[], int url_cnt, 
                            unsigned int keepalive, unsigned int conn_timeout, unsigned int reconn_delay);

/* VSOA client auto stop
 * `ipc_client_auto_start` and `ipc_client_auto_stop` must be called sequentially */
bool ipc_client_auto_stop(ipc_client_auto_t *cliauto);

/* VSOA client auto get client handle 
 * This handle is only used for communication, 
 * and cannot perform state operations such as connection closing, 
 * otherwise it will destroy the client auto logic.
 * This client handle is valid after `vsoa_client_auto_create` and invalid after `vsoa_client_auto_delete` */
ipc_client_t *ipc_client_auto_handle(ipc_client_auto_t *cliauto);

#ifdef __cplusplus
}
#endif

#endif /* IPC_CLIAUTO_H */
/*
 * end
 */
