# 01_node_lifecycle 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的节点生命周期管理功能。它演示了如何创建、启动、停止和销毁一个节点，以及如何管理节点的生命周期状态。

## 功能特性

- 创建节点
- 启动节点
- 停止节点
- 销毁节点
- 监控节点状态变化

## 代码结构

```
01_node_lifecycle/
├── node_lifecycle.c  # 节点生命周期示例代码
├── Makefile          # 构建脚本
└── README.md         # 本说明文档
```

## 核心代码解析

### 节点生命周期示例 (node_lifecycle.c)

1. **节点配置**
   - 设置节点类型为 "example"
   - 设置节点名称为 "lifecycle_node"
   - 配置节点能力（RPC、Pub/Sub、Server、Client）
   - 设置监听地址为 "unix:///tmp/node_lifecycle"

2. **节点创建**
   - 使用 `ipc_node_create` 创建节点
   - 检查节点创建状态
   - 打印节点信息

3. **节点启动**
   - 使用 `ipc_node_start` 启动节点
   - 检查节点启动状态
   - 打印节点状态

4. **节点停止**
   - 使用 `ipc_node_stop` 停止节点
   - 检查节点停止状态
   - 打印节点状态

5. **节点销毁**
   - 使用 `ipc_node_destroy` 销毁节点
   - 检查节点销毁状态

## 运行示例

### 构建示例

```bash
cd examples/node/01_node_lifecycle
make
```

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
[INFO] [node_lifecycle.c:102] main(): Starting node lifecycle example...
[INFO] [ipc_node.c:170] ipc_node_create(): node created successfully, id=example_lifecycle_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456, type=example, name=lifecycle_node
[INFO] [node_lifecycle.c:112] main(): Node created successfully
[INFO] [node_lifecycle.c:117] main(): Node ID: example_lifecycle_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_lifecycle.c:118] main(): Node type: example
[INFO] [node_lifecycle.c:119] main(): Node name: lifecycle_node
[INFO] [node_lifecycle.c:124] main(): Node state: STOPPED
[INFO] [node_lifecycle.c:129] main(): Starting node...
[INFO] [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/transports/ssn_transport_factory.c:102] ssn_transport_factory_init(): Transport factory initialized successfully
[INFO] [ipc_node.c:230] ipc_node_start(): server started on unix:///tmp/node_lifecycle
[INFO] [ipc_node.c:244] ipc_node_start(): node started successfully, id=example_lifecycle_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_lifecycle.c:134] main(): Node started successfully
[INFO] [node_lifecycle.c:139] main(): Node state: ACTIVE
[INFO] [node_lifecycle.c:144] main(): Running for 5 seconds...
[INFO] [node_lifecycle.c:149] main(): Stopping node...
[INFO] [ipc_node.c:272] ipc_node_stop(): server stopped
[INFO] [ipc_node.c:286] ipc_node_stop(): node stopped successfully, id=example_lifecycle_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_lifecycle.c:154] main(): Node stopped successfully
[INFO] [node_lifecycle.c:159] main(): Node state: STOPPED
[INFO] [node_lifecycle.c:164] main(): Destroying node...
[INFO] [ipc_node.c:322] ipc_node_destroy(): node destroyed, id=example_lifecycle_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_lifecycle.c:169] main(): Node destroyed successfully
[INFO] [node_lifecycle.c:171] main(): Node lifecycle example completed
```

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 节点监听地址为 `unix:///tmp/node_lifecycle`
- 节点运行 5 秒后自动停止

## 相关 API

- `ipc_node_create` - 创建节点
- `ipc_node_start` - 启动节点
- `ipc_node_stop` - 停止节点
- `ipc_node_destroy` - 销毁节点
- `ipc_node_get_state` - 获取节点状态
