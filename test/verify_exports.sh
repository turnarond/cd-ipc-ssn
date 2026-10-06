#!/bin/bash
# 校验公开 API 导出完整性（回归 P1-1：-fvisibility=hidden 下未标 SSN_API 的函数不导出）
#
# 背景：CMakeLists 对 ssn_transport 设置 C_VISIBILITY_PRESET hidden，只有带
# SSN_API 的符号导出。曾出现 ssn_frame.h / ssn_error.h / ssn_node.h 的部分
# 公开函数漏标 SSN_API → 外部 find_package(ssn) 消费者链接失败。
# 本脚本用 nm -D 断言关键公开符号必须导出，防止回归。
#
# 以脚本位置定位仓库根目录（与调用时的 cwd 无关）
set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
# 支持 BUILD_DIR 环境变量（CI 用默认 build/，本地验证可用 build-asan 等）
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
LIB="$BUILD_DIR/libssn_transport.so"

if [ ! -f "$LIB" ]; then
    echo "错误：$LIB 不存在，请先构建库（cd build && cmake .. && make）"
    exit 1
fi

# 关键公开 API（frame 线协议 / error 错误处理 / client / server）
# 旧 Node C API（ssn_node_*）已于 v3.0.0 移除，不再进入正向断言，
# 由下方"旧符号否定断言"保证其彻底消失。
REQUIRED=(
    ssn_create_header
    ssn_stream_init
    ssn_stream_feed
    ssn_get_url
    ssn_get_data
    ssn_packet_input
    ssn_send_message
    ssn_handle_error
    ssn_ecode_message
    ssn_ecode_category
    ssn_ecode_subcategory
    ssn_ecode_code
    ssn_client_create
    ssn_client_connect
    ssn_client_poll
    ssn_client_call
    ssn_client_ping
    ssn_server_create
    ssn_server_start
    ssn_server_poll
    ssn_server_publish
    ssn_rpc_handle_reply
    ssn_rpc_handle_request
    ssn_pubsub_handle_message
    ssn_msg_handle_data
)

exported=$(nm -D --defined-only "$LIB" 2>/dev/null | awk '{print $3}' | grep '^ssn_' | sort -u)

missing=0
for sym in "${REQUIRED[@]}"; do
    if ! echo "$exported" | grep -qx "$sym"; then
        echo "FAIL: $sym 未导出（缺 SSN_API？）"
        missing=$((missing + 1))
    fi
done

# 旧 Node C API 否定断言（v3.0.0 移除）：任何 ssn_node_* 符号导出均视为回归，
# C 消费者应迁移至 ssn::Node（libssn_framework，include/ssn/node/Node.hpp）
legacy=$(echo "$exported" | grep -c '^ssn_node_')
if [ "$legacy" -ne 0 ]; then
    echo "FAIL: 旧 Node C API 符号仍导出 $legacy 个（v3.0.0 已移除，C 消费者迁移 ssn::Node）"
    echo "$exported" | grep '^ssn_node_' | head -5
    missing=$((missing + 1))
fi

# 内部符号白名单：跨 .so 使用的私有接口（见 src/transports/ssn_transport_async_internal.h）。
# 这些符号必须导出才能被 libssn_framework 解析，但不属于公开 C API；
# 白名单外出现任何 *_internal 导出符号按内部接口泄漏回归处理。
INTERNAL_ALLOWED=(
    ssn_transport_connect_begin_internal
    ssn_transport_connect_finish_internal
)
for sym in $(nm -D --defined-only "$LIB" 2>/dev/null | awk '{print $3}' | \
        grep '_internal$' | sort -u); do
    allowed=0
    for known in "${INTERNAL_ALLOWED[@]}"; do
        [ "$sym" = "$known" ] && allowed=1
    done
    if [ "$allowed" -eq 0 ]; then
        echo "FAIL: 内部符号 $sym 不在白名单（新增内部接口需同步 INTERNAL_ALLOWED 与文档口径）"
        missing=$((missing + 1))
    fi
done

# 反向校验：私有实现符号不得出现在 libssn_framework 动态符号表。
# 缺陷背景：给含嵌套私有 Impl 的公开类整类标注 SSN_FRAMEWORK_API，可见性会
# 传递给嵌套类，Node::Impl 的成员函数全部落进 .dynsym（实测 11 个）。
FW_LIB="$BUILD_DIR/libssn_framework.so"
if [ -f "$FW_LIB" ]; then
    leaked=$(nm -D -C "$FW_LIB" 2>/dev/null | \
        grep -cE 'ssn::Node::Impl|ssn::detail::(EventQueue|PeerRegistry|PeerSession)')
    if [ "$leaked" -ne 0 ]; then
        echo "FAIL: libssn_framework 导出了 $leaked 个私有实现符号"
        nm -D -C "$FW_LIB" | \
            grep -E 'ssn::Node::Impl|ssn::detail::(EventQueue|PeerRegistry|PeerSession)' | head -5
        missing=$((missing + 1))
    fi
fi

if [ "$missing" -eq 0 ]; then
    echo "导出符号校验通过：$(echo "$exported" | wc -l) 个 ssn_ 符号，${#REQUIRED[@]} 个关键 API 全部导出"
    exit 0
fi
echo "共 $missing 项符号校验失败（关键符号缺失或私有实现泄漏）"
exit 1
