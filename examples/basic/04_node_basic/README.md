# 04_node_basic 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的节点抽象层基础功能。它演示了如何配置、创建、启动、停止和销毁节点，以及如何获取节点的状态与能力。

## 功能特性

- 创建节点
- 配置节点属性（类型、名称、监听地址与端口、能力）
- 启动节点
- 获取节点状态和能力
- 获取节点运行统计
- 停止节点
- 销毁节点
- 最小配置（仅类型与名称）节点创建

## 代码结构

```
04_node_basic/
├── node_basic.c  # 节点基础示例代码
├── Makefile      # 构建脚本
└── README.md     # 本说明文档
```

## 核心代码解析

### 测试1：节点创建与销毁（test_node_creation）

1. **节点配置**
   - `node_type` = "test"
   - `node_name` = "basic-node"
   - `listen_address` = "127.0.0.1"（裸主机名，不含协议前缀）+ `listen_port` = 8888
   - `capabilities` = RPC | PUBSUB | SERVER | CLIENT（0x000f）

2. **节点创建与启动**
   - 使用 `ssn_node_create` 创建节点
   - 使用 `ssn_node_start` 启动节点（启动后节点内部创建 TCP 服务器监听 8888 端口）

3. **节点信息获取**
   - 使用 `ssn_node_get_state` 获取节点状态（STOPPED / ACTIVE）
   - 使用 `ssn_node_get_capabilities` 获取节点能力
   - 使用 `ssn_node_get_stats` 获取连接数与消息总数统计

4. **节点停止与销毁**
   - 使用 `ssn_node_stop` 停止节点
   - 使用 `ssn_node_destroy` 销毁节点

### 测试2：最小配置（test_minimal_config）

- 仅设置 `node_type` = "minimal"、`node_name` = "minimal-node"，其余字段使用默认值
- 验证节点创建与销毁流程

## 运行示例

### 构建示例

```bash
cd examples/basic/04_node_basic
make clean && make
```

> 二进制已内置 rpath，构建完成后可直接运行，无需设置 `LD_LIBRARY_PATH`。

### 运行示例

```bash
make run
```

或者直接运行：

```bash
./node_basic
```

## 预期输出

```
[INFO] [node_basic.c:137] main(): Starting node basic example...
[INFO] [node_basic.c:20] test_node_creation(): Node creation and destruction test
[INFO] [node_basic.c:39] test_node_creation(): Node created successfully: id=<node_id>, type=test, name=basic-node
[INFO] [node_basic.c:44] test_node_creation(): Node state: STOPPED
[INFO] [node_basic.c:50] test_node_creation(): Node capabilities: 0x000f (SERVER|CLIENT|RPC|PUBSUB)
[INFO] [node_basic.c:64] test_node_creation(): Node started successfully
[INFO] [node_basic.c:68] test_node_creation(): Node state: ACTIVE
[INFO] [node_basic.c:76] test_node_creation(): Node statistics: active_connections=0, total_messages=0
[INFO] [node_basic.c:87] test_node_creation(): Node stopped successfully
[INFO] [node_basic.c:91] test_node_creation(): Node state: STOPPED
[INFO] [node_basic.c:97] test_node_creation(): Node destroyed successfully
[INFO] [node_basic.c:107] test_minimal_config():
Minimal configuration test

[INFO] [node_basic.c:123] test_minimal_config(): Node created with minimal configuration: id=<node_id>
[INFO] [node_basic.c:127] test_minimal_config(): Minimal configuration node destroyed successfully
[INFO] [node_basic.c:144] main():
All tests passed!
```

> 注：
> - `<node_id>` 由 `节点类型_节点名称_主机名_时间戳` 生成（见 `ssn_node.c` 的 `generate_node_id`），每次运行不同，此处为占位符。
> - 实际输出中每条日志还包含时间戳与线程 ID 前缀（`[时间] [INFO] [线程ID] ...`），以上为便于阅读省略。
> - 库内部日志（如 `ssn_node.c` 中节点创建/启动/停止/销毁、传输工厂初始化等 INFO 日志）也会出现在实际输出中，其文件路径与行号以实际编译路径为准，此处未逐一列出。
> - `Node statistics` 中的数值为运行时统计，示例中启动后尚无连接与消息，通常为 0。

## 注意事项

- 本示例使用 TCP 作为传输协议（`listen_address = "127.0.0.1"` + `listen_port = 8888`，配置项只接受裸主机名，协议前缀由库内部拼接）
- 示例包含两个测试，全部通过后退出码为 0
- 示例不会长时间运行：每个测试创建、启动、停止、销毁节点后立即结束

## 相关 API

- `ssn_node_create` - 创建节点
- `ssn_node_start` - 启动节点
- `ssn_node_stop` - 停止节点
- `ssn_node_destroy` - 销毁节点
- `ssn_node_get_state` - 获取节点状态
- `ssn_node_get_capabilities` - 获取节点能力
- `ssn_node_get_stats` - 获取节点运行统计
