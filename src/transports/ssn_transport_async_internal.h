/*
 * ssn_transport_async_internal.h - 传输层内部异步连接接口
 *
 * 仅供 libssn_framework 的 Node 后端使用：
 *   - 不安装（CMake 安装列表不含本文件）；
 *   - 不进公共文档、不使用 SSN_API 宏，因此不算公开 C API；
 *   - 但必须显式 default 可见性：NodeBackend/PollDriver 编在 libssn_framework，
 *     而本文件实现在 libssn_transport，C_VISIBILITY_PRESET hidden 下未导出的
 *     符号无法跨 .so 解析（口径见 v3.0.0 实施计划文首）。
 * test/verify_exports.sh 以白名单校验这些内部符号，白名单外的新增视为回归。
 */

#ifndef SSN_TRANSPORT_ASYNC_INTERNAL_H
#define SSN_TRANSPORT_ASYNC_INTERNAL_H

#include "ssn_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SSN_INTERNAL_API __attribute__((visibility("default")))

/* 非阻塞连接状态机：begin 发起，finish 在可写事件后判定结果 */
typedef enum {
    SSN_CONNECT_CONNECTED = 0,
    SSN_CONNECT_IN_PROGRESS,
    SSN_CONNECT_FAILED
} ssn_connect_state_t;

/* 分派入口：按 transport->type 转发到各传输实现 */
SSN_INTERNAL_API ssn_connect_state_t
ssn_transport_connect_begin_internal(ssn_transport_t* transport,
                                     const ssn_address_t* addr);
SSN_INTERNAL_API ssn_connect_state_t
ssn_transport_connect_finish_internal(ssn_transport_t* transport);

/* 各传输实现（同库内可见，无需导出） */
ssn_connect_state_t ssn_tcp_connect_begin(ssn_transport_t* transport,
                                          const ssn_address_t* addr);
ssn_connect_state_t ssn_tcp_connect_finish(ssn_transport_t* transport);
ssn_connect_state_t ssn_unix_connect_begin(ssn_transport_t* transport,
                                           const ssn_address_t* addr);
ssn_connect_state_t ssn_unix_connect_finish(ssn_transport_t* transport);
ssn_connect_state_t ssn_udp_connect_begin(ssn_transport_t* transport,
                                          const ssn_address_t* addr);
ssn_connect_state_t ssn_udp_connect_finish(ssn_transport_t* transport);

#ifdef __cplusplus
}
#endif

#endif /* SSN_TRANSPORT_ASYNC_INTERNAL_H */
