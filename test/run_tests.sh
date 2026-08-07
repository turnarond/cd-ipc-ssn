#!/bin/bash
# 构建并运行 cd-ipc-ssn 自动化测试套件

set -u

echo "=== 构建并运行 SSN 测试 ==="

# 以脚本自身位置定位仓库根目录（与调用时的 cwd 无关）
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$REPO_ROOT/build"

# 创建 build 目录
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi
cd "$BUILD_DIR"

# 配置并构建（构建所有目标）
echo "--- 运行 CMake ---"
cmake ..
echo "--- 构建 ---"
make -j4 || { echo "[FAIL] 构建失败"; exit 1; }

# 自动化测试套件（均为自包含测试，无需外部服务端）
TESTS=(
    test_transport            # 传输层（55 用例）
    test_node_basic           # 节点基础（3 用例）
    test_node                 # 节点完整（6 用例）
    test_protocol             # 协议层（25 用例）
    test_protocol_integration # 协议集成（19 用例）
    example_server            # 服务端 API 功能测试（4 用例）
    example_client            # 客户端 API 功能测试（5 用例）
)

PASS=0
FAIL=0
for t in "${TESTS[@]}"; do
    echo ""
    echo "=== 运行 $t ==="
    if ./"$t"; then
        echo "[PASS] $t"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $t"
        FAIL=$((FAIL + 1))
    fi
done

echo ""
echo "=== 测试完成：$PASS 个套件通过，$FAIL 个套件失败 ==="

if [ "$FAIL" -ne 0 ]; then
    echo "高级测试（需手工运行服务端）可另行执行：test_comprehensive / test_thread_safety / test_stress"
    exit 1
fi

exit 0
