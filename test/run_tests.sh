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
    test_transport            # 传输层（67 断言）
    test_node_basic           # 节点基础（3 用例）
    test_node                 # 节点完整（6 用例）
    test_protocol             # 协议层（25 断言）
    test_protocol_integration # 协议集成（19 用例）
    example_server            # 服务端 API 功能测试（8 用例）
    example_client            # 客户端 API 功能测试（12 用例）
    test_hash_table           # 哈希表（50 断言，含字符串键回归）
)

# C++ 服务框架套件（v2.4.0，自包含测试，无需外部服务端）
CPP_TESTS=(
    test_cpp_service_base     # 生命周期状态机 + 钩子顺序（41 断言）
    test_cpp_service_task     # 线程池任务调度（21 断言）
    test_cpp_service_manager  # Run 编排 + 信号停止（10 断言）
    test_cpp_ssn_service      # 服务端基类 IPC 回环（87 断言）
    test_cpp_ssn_client       # 客户端调用/订阅（30 断言）
    test_cpp_json             # 类型安全层 DTO（11 断言）
    test_cpp_stability        # 稳定性套件（285 断言，回调异常/并发/超时风暴/生命周期/信号风暴）
)

PASS=0
FAIL=0

# 运行单个套件并聚合结果（调用方保证 cwd 为 build/，位置无关）
run_one() {
    echo ""
    echo "=== 运行 $1 ==="
    if ./"$1"; then
        echo "[PASS] $1"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $1"
        FAIL=$((FAIL + 1))
    fi
}

for t in "${TESTS[@]}" "${CPP_TESTS[@]}"; do
    run_one "$t"
done

echo ""
echo "=== 测试完成：$PASS 个套件通过，$FAIL 个套件失败 ==="

if [ "$FAIL" -ne 0 ]; then
    echo "高级测试（需手工运行服务端）可另行执行：test_comprehensive / test_thread_safety / test_stress"
    exit 1
fi

exit 0
