# 01_node_lifecycle 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的节点生命周期管理功能。它演示了如何创建、启动、停止和销毁节点，以及不同配置（基础、最小配置、仅服务端、仅客户端）下节点的生命周期状态。

## 功能特性

- 创建节点
- 启动节点
- 停止节点
- 销毁节点
- 获取节点状态（STOPPED / ACTIVE）与能力
- 基础配置 / 最小配置 / 仅服务端 / 仅客户端 四种节点形态

## 代码结构

```
01_node_lifecycle/
├── node_lifecycle.c  # 节点生命周期示例代码
├── Makefile          # 构建脚本
└── README.md         # 本说明文档
```

## 核心代码解析

### 测试1：基础生命周期（test_basic_lifecycle）

1. **节点配置**
   - `node_type` = "test"、`node_name` = "basic-node"
   - `listen_address` = "127.0.0.1"（裸主机名，不含协议前缀）+ `listen_port` = 8888
   - `capabilities` = RPC | PUBSUB | SERVER | CLIENT（0x000f）

2. **节点创建**：使用 `ssn_node_create` 创建节点，打印节点 ID / 类型 / 名称，创建后状态为 STOPPED

3. **节点启动**：使用 `ssn_node_start` 启动节点，启动后状态变为 ACTIVE

4. **节点停止**：使用 `ssn_node_stop` 停止节点，状态恢复为 STOPPED

5. **节点销毁**：使用 `ssn_node_destroy` 销毁节点

### 测试2：最小配置（test_minimal_config）

- 仅设置 `node_type` = "minimal"、`node_name` = "minimal"，其余字段使用默认值
- 打印默认能力（未设置能力时默认 RPC | PUBSUB | SERVER | CLIENT，即 0x000f）

### 测试3：仅服务端节点（test_server_only）

- `node_type` = "server"、`node_name` = "server-only"
- `listen_address` = "127.0.0.1" + `listen_port` = 8889，`capabilities` = SERVER | RPC（0x0005）
- 启动（创建 TCP 服务器监听 8889 端口）→ 停止 → 销毁

### 测试4：仅客户端节点（test_client_only）

- `node_type` = "client"、`node_name` = "client-only"
- `capabilities` = CLIENT | RPC（0x0009），无监听地址
- 创建 → 销毁（客户端在需要时按需创建，见 `ssn_node_get_client`）

## 运行示例

### 构建示例

```bash
cd examples/node/01_node_lifecycle
make clean && make
```

> 二进制已内置 rpath，构建完成后可直接运行，无需设置 `LD_LIBRARY_PATH`。

### 运行示例

```bash
make run
```

或者直接运行：

```bash
./node_lifecycle
```

## 预期输出

```
[INFO] [node_lifecycle.c:214] main(): Node lifecycle example started
[INFO] [node_lifecycle.c:20] test_basic_lifecycle(): Test 1: Basic node lifecycle
[INFO] [node_lifecycle.c:39] test_basic_lifecycle(): Node created successfully: id=<node_id>, type=test, name=basic-node
[INFO] [node_lifecycle.c:44] test_basic_lifecycle(): Node state: STOPPED
[INFO] [node_lifecycle.c:50] test_basic_lifecycle(): Node capabilities: 0x000f (SERVER|CLIENT|RPC|PUBSUB)
[INFO] [node_lifecycle.c:64] test_basic_lifecycle(): Node started successfully
[INFO] [node_lifecycle.c:68] test_basic_lifecycle(): Node state: ACTIVE
[INFO] [node_lifecycle.c:79] test_basic_lifecycle(): Node stopped successfully
[INFO] [node_lifecycle.c:83] test_basic_lifecycle(): Node state: STOPPED
[INFO] [node_lifecycle.c:89] test_basic_lifecycle(): Node destroyed successfully
[INFO] [node_lifecycle.c:99] test_minimal_config():
Test 2: Minimal configuration

[INFO] [node_lifecycle.c:115] test_minimal_config(): Node created with minimal config: id=<node_id>, type=minimal, name=minimal
[INFO] [node_lifecycle.c:120] test_minimal_config(): Default capabilities: 0x000f
[INFO] [node_lifecycle.c:124] test_minimal_config(): Minimal config node destroyed successfully
[INFO] [node_lifecycle.c:134] test_server_only():
Test 3: Server-only node

[INFO] [node_lifecycle.c:152] test_server_only(): Server-only node created: id=<node_id>, type=server, name=server-only
[INFO] [node_lifecycle.c:162] test_server_only(): Server-only node started successfully
[INFO] [node_lifecycle.c:173] test_server_only(): Server-only node destroyed successfully
[INFO] [node_lifecycle.c:183] test_client_only():
Test 4: Client-only node

[INFO] [node_lifecycle.c:199] test_client_only(): Client-only node created: id=<node_id>, type=client, name=client-only
[INFO] [node_lifecycle.c:204] test_client_only(): Client-only node destroyed successfully
[INFO] [node_lifecycle.c:223] main():
All lifecycle tests completed successfully!
```

> 注：
> - `<node_id>` 由 `节点类型_节点名称_主机名_时间戳` 生成（见 `ssn_node.c` 的 `generate_node_id`），每次运行不同，此处为占位符。
> - 实际输出中每条日志还包含时间戳与线程 ID 前缀（`[时间] [INFO] [线程ID] ...`），以上为便于阅读省略；`LOG_INFO("\n...")` 中的 `\n` 会使日志内容换行显示。
> - 库内部日志（如 `ssn_node.c` 中节点创建/启动/停止/销毁、传输工厂初始化等 INFO 日志）也会出现在实际输出中，其文件路径与行号以实际编译路径为准，此处未逐一列出。

## 注意事项

- 本示例使用 TCP 作为传输协议（`listen_address` 只接受裸主机名，协议前缀由库内部拼接）
- 测试3 使用端口 8889 创建第二个服务器，避免与测试1 的 8888 端口冲突
- 示例包含 4 个测试，全部通过后退出码为 0；示例不会长时间运行，完成后立即结束

## 相关 API

- `ssn_node_create` - 创建节点
- `ssn_node_start` - 启动节点
- `ssn_node_stop` - 停止节点
- `ssn_node_destroy` - 销毁节点
- `ssn_node_get_state` - 获取节点状态
- `ssn_node_get_capabilities` - 获取节点能力
