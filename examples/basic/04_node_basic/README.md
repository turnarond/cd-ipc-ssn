# 04_node_basic 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的节点抽象层基础功能。它演示了如何创建一个节点，启动节点，以及如何使用节点的基本功能。

## 功能特性

- 创建节点
- 配置节点属性（类型、名称、能力）
- 启动节点
- 获取节点状态和能力
- 停止节点
- 销毁节点

## 代码结构

```
04_node_basic/
├── node_basic.c  # 节点基础示例代码
├── Makefile      # 构建脚本
└── README.md     # 本说明文档
```

## 核心代码解析

### 节点基础示例 (node_basic.c)

1. **节点配置**
   - 设置节点类型为 "example"
   - 设置节点名称为 "basic_node"
   - 配置节点能力（RPC、Pub/Sub、Server、Client）
   - 设置监听地址为 "unix:///tmp/node_basic"

2. **节点创建与启动**
   - 使用 `ipc_node_create` 创建节点
   - 使用 `ipc_node_start` 启动节点
   - 检查节点启动状态

3. **节点信息获取**
   - 使用 `ipc_node_get_state` 获取节点状态
   - 使用 `ipc_node_get_capabilities` 获取节点能力
   - 打印节点信息

4. **节点停止与销毁**
   - 使用 `ipc_node_stop` 停止节点
   - 使用 `ipc_node_destroy` 销毁节点

## 运行示例

### 构建示例

```bash
cd examples/basic/04_node_basic
make
```

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
[INFO] [node_basic.c:82] main(): Starting node basic example...
[INFO] [ipc_node.c:170] ipc_node_create(): node created successfully, id=example_basic_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456, type=example, name=basic_node
[INFO] [node_basic.c:92] main(): Node created successfully
[INFO] [node_basic.c:97] main(): Node ID: example_basic_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_basic.c:98] main(): Node type: example
[INFO] [node_basic.c:99] main(): Node name: basic_node
[INFO] [node_basic.c:104] main(): Starting node...
[INFO] [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/transports/ssn_transport_factory.c:102] ssn_transport_factory_init(): Transport factory initialized successfully
[INFO] [ipc_node.c:230] ipc_node_start(): server started on unix:///tmp/node_basic
[INFO] [ipc_node.c:244] ipc_node_start(): node started successfully, id=example_basic_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_basic.c:109] main(): Node started successfully
[INFO] [node_basic.c:114] main(): Node state: ACTIVE
[INFO] [node_basic.c:119] main(): Node capabilities: RPC, Pub/Sub, Server, Client
[INFO] [node_basic.c:124] main(): Running for 5 seconds...
[INFO] [node_basic.c:129] main(): Stopping node...
[INFO] [ipc_node.c:272] ipc_node_stop(): server stopped
[INFO] [ipc_node.c:286] ipc_node_stop(): node stopped successfully, id=example_basic_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_basic.c:134] main(): Node stopped successfully
[INFO] [node_basic.c:139] main(): Destroying node...
[INFO] [ipc_node.c:322] ipc_node_destroy(): node destroyed, id=example_basic_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_basic.c:144] main(): Node destroyed successfully
[INFO] [node_basic.c:146] main(): Node basic example completed
```

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 节点监听地址为 `unix:///tmp/node_basic`
- 节点运行 5 秒后自动停止

## 相关 API

- `ipc_node_create` - 创建节点
- `ipc_node_start` - 启动节点
- `ipc_node_stop` - 停止节点
- `ipc_node_destroy` - 销毁节点
- `ipc_node_get_state` - 获取节点状态
- `ipc_node_get_capabilities` - 获取节点能力
