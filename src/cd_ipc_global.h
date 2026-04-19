// cd_ipc_global.h

#ifndef CD_IPC_GLOBAL_H
#define CD_IPC_GLOBAL_H

#include "vsi/ipc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Structure declaration */
struct ipc_client;
struct ipc_server;

typedef struct ipc_server ipc_server_t;
typedef struct ipc_client ipc_client_t;

/* 全局资源声明（供 cd_ipc_client.c / cd_ipc_server.c 使用） */
extern struct ipc_client *g_ipc_client_list;
extern ipc_mutex_t  *g_ipc_client_lock;

extern struct ipc_server *g_ipc_server_list;
extern ipc_mutex_t  *g_ipc_server_lock;

/* 显式初始化函数（供不支持 constructor 的平台使用） */
int ipc_global_init(void);
void ipc_global_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // CD_IPC_GLOBAL_H