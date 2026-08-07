// ssn_global.h

#ifndef SSN_GLOBAL_H
#define SSN_GLOBAL_H

#include "vsi/ipc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Structure declaration */
struct ssn_client;
struct ssn_server;

typedef struct ssn_server ssn_server_t;
typedef struct ssn_client ssn_client_t;

/* 全局资源声明（供 ssn_client.c / ssn_server.c 使用） */
extern struct ssn_client *g_ssn_client_list;
extern ipc_mutex_t  *g_ssn_client_lock;
extern int g_ssn_client_timer_exit;    /* 客户端定时器线程退出标志（ssn_global_cleanup 置位） */

extern struct ssn_server *g_ssn_server_list;
extern ipc_mutex_t  *g_ssn_server_lock;
extern int g_ssn_server_timer_exit;   /* 服务端定时器线程退出标志（ssn_global_cleanup 置位） */

/* 显式初始化函数（供不支持 constructor 的平台使用） */
int ssn_global_init(void);
void ssn_global_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // SSN_GLOBAL_H