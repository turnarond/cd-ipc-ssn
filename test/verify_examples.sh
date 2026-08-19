#!/bin/bash
# 验证全部 19 个示例目录构建（15 个 C 示例 + 4 个 C++ 框架示例）
# + find_package(ssn) 集成（cmake_integration 消费示例）
# 以脚本位置定位仓库根目录（与调用时的 cwd 无关）
set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR/.." || exit 1

# 预检：动态库必须已构建（示例链接 -Lbuild -lssn_transport / -lssn_framework）
if [ ! -f "build/libssn_transport.so" ]; then
    echo "错误：build/libssn_transport.so 不存在，请先构建库（mkdir -p build && cd build && cmake .. && make）"
    exit 1
fi
if [ ! -f "build/libssn_framework.so" ]; then
    echo "错误：build/libssn_framework.so 不存在，请先构建库（mkdir -p build && cd build && cmake .. && make）"
    exit 1
fi

ok=0
fail=0
for d in examples/basic/01_hello_world examples/basic/02_rpc_call examples/basic/03_pubsub \
         examples/basic/04_node_basic examples/advanced/01_multithread examples/advanced/02_error_handling \
         examples/advanced/03_timeout examples/advanced/04_transport_selection \
         examples/protocols/01_unix_socket examples/protocols/02_tcp examples/protocols/03_udp \
         examples/node/01_node_lifecycle examples/node/02_node_comm examples/node/03_node_rpc \
         examples/node/04_node_pubsub examples/cpp/01_echo_service examples/cpp/02_pubsub_chat \
         examples/cpp/03_robust_client examples/cpp/04_concurrent_client; do
    if (cd "$d" && make clean >/dev/null 2>&1 && make >/dev/null 2>&1); then
        ok=$((ok + 1))
    else
        fail=$((fail + 1))
        echo "FAIL: $d"
    fi
done

# 运行冒烟：hello_world（用户旅程核心路径——服务端事件循环与客户端握手时序回归）。
# 后台起 server（脚本在服务端无阻塞处理时即应响应客户端），client 须成功连接并发送消息。
smoke_ok=0
smoke_fail=0
hello_dir="examples/basic/01_hello_world"
if (cd "$hello_dir" && ./server >/dev/null 2>&1 &) ; then
    # server 后台启动后等待其就绪（unix socket 文件出现或短等待）
    sleep 1
    if (cd "$hello_dir" && timeout 15 ./client 2>&1 | grep -q "Connected to server"); then
        smoke_ok=1
        echo "SMOKE PASS: hello_world server+client 往返"
    else
        smoke_fail=1
        echo "SMOKE FAIL: hello_world client 未能连接服务器"
    fi
    # 等待 server 自然退出（运行 10 秒）或终止残留进程
    sleep 11
    pkill -f "$hello_dir/server" >/dev/null 2>&1 || true
else
    smoke_fail=1
    echo "SMOKE FAIL: hello_world server 启动失败"
fi

echo "RESULT: $ok ok, $fail failed (build); smoke: $((smoke_ok)) ok, $((smoke_fail)) failed"

# find_package(ssn) 集成验证：安装到临时前缀 → 消费工程（cmake_integration）
# 配置/构建/运行（C 库与 C++ 框架双消费者）。失败视为整体失败。
pkg_ok=1
pkg_prefix="$(mktemp -d /tmp/ssn-pkg-XXXXXX)"
if cmake --install build --prefix "$pkg_prefix" >/dev/null 2>&1 \
   && (cd examples/cmake_integration \
       && rm -rf build \
       && cmake -S . -B build -DCMAKE_PREFIX_PATH="$pkg_prefix" >/dev/null 2>&1 \
       && cmake --build build >/dev/null 2>&1 \
       && LD_LIBRARY_PATH="$pkg_prefix/lib" ./build/hello_ssn >/dev/null 2>&1 \
       && LD_LIBRARY_PATH="$pkg_prefix/lib" ./build/hello_framework >/dev/null 2>&1); then
    echo "PKG PASS: find_package(ssn) 集成（C + C++ 消费者）"
else
    pkg_ok=0
    echo "PKG FAIL: find_package(ssn) 集成"
fi
rm -rf "$pkg_prefix"

[ "$fail" -eq 0 ] && [ "$smoke_fail" -eq 0 ] && [ "$pkg_ok" -eq 1 ]
