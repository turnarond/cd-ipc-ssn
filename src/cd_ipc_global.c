/**
 * @file ipc_global.c
 * @brief 全局资源管理
 */

#include "cd_ipc_global.h"

/* 定义全局变量（不再是 static！） */
ipc_thread_t *g_ipc_client_timer = NULL;
ipc_client_t *g_ipc_client_list = NULL;
ipc_mutex_t  *g_ipc_client_lock = NULL;

ipc_thread_t *g_ipc_server_timer = NULL;
ipc_server_t *g_ipc_server_list = NULL;
ipc_mutex_t  *g_ipc_server_lock = NULL;

static int g_initialized = 0;

extern void *ipc_client_timer_handle(void *arg);
extern void *ipc_server_timer_handle(void *arg);

/**
 * @brief 初始化全局资源
 * @return 0 成功，-1 失败
 */
int ipc_global_init(void)
{
    if (__atomic_exchange_n(&g_initialized, 1, __ATOMIC_ACQ_REL)) {
        return 0; // 已初始化
    }

    if (ipc_platform_init() != 0) {
        __atomic_store_n(&g_initialized, 0, __ATOMIC_RELEASE);
        return -1;
    }

    // 初始化 client 全局资源
    if (ipc_mutex_init(&g_ipc_client_lock) != 0) goto fail;
    if (ipc_thread_create(&g_ipc_client_timer, ipc_client_timer_handle, NULL) != 0) goto fail_client_timer;

    // 初始化 server 全局资源
    if (ipc_mutex_init(&g_ipc_server_lock) != 0) goto fail_server_lock;
    if (ipc_thread_create(&g_ipc_server_timer, ipc_server_timer_handle, NULL) != 0) goto fail_server_timer;

    return 0;

    // 错误回滚
fail_server_timer:
    ipc_mutex_destroy(g_ipc_server_lock);
    g_ipc_server_lock = NULL;
fail_server_lock:
    ipc_thread_join(g_ipc_client_timer);
    g_ipc_client_timer = NULL;
fail_client_timer:
    ipc_mutex_destroy(g_ipc_client_lock);
    g_ipc_client_lock = NULL;
fail:
    ipc_platform_cleanup();
    __atomic_store_n(&g_initialized, 0, __ATOMIC_RELEASE);
    return -1;
}

/**
 * @brief 清理全局资源
 */
void ipc_global_cleanup(void)
{
    if (!__atomic_exchange_n(&g_initialized, 0, __ATOMIC_ACQ_REL)) {
        return; // 未初始化或已在清理
    }

    // 停止并等待 timer 线程（需你的 timer 支持退出信号）
    // 这里假设你有机制通知线程退出，例如写入 eventfd 或设置标志
    // 简化处理：直接 join（需确保线程会退出）
    if (g_ipc_client_timer) {
        ipc_thread_join(g_ipc_client_timer);
        g_ipc_client_timer = NULL;
    }
    if (g_ipc_server_timer) {
        ipc_thread_join(g_ipc_server_timer);
        g_ipc_server_timer = NULL;
    }

    // 销毁锁
    if (g_ipc_client_lock) {
        ipc_mutex_destroy(g_ipc_client_lock);
        g_ipc_client_lock = NULL;
    }
    if (g_ipc_server_lock) {
        ipc_mutex_destroy(g_ipc_server_lock);
        g_ipc_server_lock = NULL;
    }

    // 清理平台
    ipc_platform_cleanup();

    // 注意：g_ipc_client_list / g_ipc_server_list 应在业务层清空
    // （例如在 cleanup 前遍历并释放所有 client/server）
}

// ipc_global.c 末尾（或单独放回原文件）

#if defined(__GNUC__) || defined(__clang__)
/**
 * @brief 库构造函数
 */
__attribute__((constructor))
static void lib_constructor(void)
{
    (void)ipc_global_init();
}

/**
 * @brief 库析构函数
 */
__attribute__((destructor))
static void lib_destructor(void)
{
    ipc_global_cleanup();
}
#elif defined(IPC_PLATFORM_WINDOWS)
/**
 * @brief Windows DLL入口函数
 */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            ipc_global_init();
            break;
        case DLL_PROCESS_DETACH:
            ipc_global_cleanup();
            break;
    }
    return TRUE;
}
#endif