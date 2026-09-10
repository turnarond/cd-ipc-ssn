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

# 关键公开 API（frame 线协议 / error 错误处理 / node getters）
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
    ssn_node_get_client
    ssn_node_get_server
    ssn_client_create
    ssn_client_connect
    ssn_client_poll
    ssn_client_call
    ssn_client_ping
    ssn_server_create
    ssn_server_start
    ssn_server_poll
    ssn_server_publish
    ssn_node_create
    ssn_node_poll
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

if [ "$missing" -eq 0 ]; then
    echo "导出符号校验通过：$(echo "$exported" | wc -l) 个 ssn_ 符号，${#REQUIRED[@]} 个关键 API 全部导出"
    exit 0
fi
echo "共 $missing 个关键符号缺失"
exit 1
