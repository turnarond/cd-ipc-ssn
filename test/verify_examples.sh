#!/bin/bash
# 验证全部 17 个示例构建（15 个 C 示例 + 2 个 C++ 框架示例）
# 以脚本位置定位仓库根目录（与调用时的 cwd 无关）
set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR/.." || exit 1

# 预检：动态库必须已构建（示例链接 -Lbuild -lssn_transport）
if [ ! -f "build/libssn_transport.so" ]; then
    echo "错误：build/libssn_transport.so 不存在，请先构建库（mkdir -p build && cd build && cmake .. && make）"
    exit 1
fi

ok=0
fail=0
for d in examples/basic/01_hello_world examples/basic/02_rpc_call examples/basic/03_pubsub \
         examples/basic/04_node_basic examples/advanced/01_multithread examples/advanced/02_error_handling \
         examples/advanced/03_timeout examples/advanced/04_transport_selection \
         examples/protocols/01_unix_socket examples/protocols/02_tcp examples/protocols/03_udp \
         examples/node/01_node_lifecycle examples/node/02_node_comm examples/node/03_node_rpc \
         examples/node/04_node_pubsub examples/cpp/01_echo_service examples/cpp/02_pubsub_chat; do
    if (cd "$d" && make clean >/dev/null 2>&1 && make >/dev/null 2>&1); then
        ok=$((ok + 1))
    else
        fail=$((fail + 1))
        echo "FAIL: $d"
    fi
done
echo "RESULT: $ok ok, $fail failed"
[ "$fail" -eq 0 ]
