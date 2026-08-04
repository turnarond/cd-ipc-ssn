# 02_node_comm 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的节点间通信功能。它演示了如何创建两个节点，并在它们之间进行通信。

## 功能特性

- 创建两个节点
- 启动两个节点
- 节点间消息传递
- 节点间 RPC 调用
- 节点间发布/订阅

## 代码结构

```
02_node_comm/
├── node_comm.c  # 节点间通信示例代码
├── Makefile     # 构建脚本
└── README.md    # 本说明文档
```

## 核心代码解析

### 节点间通信示例 (node_comm.c)

1. **节点1配置与创建**
   - 设置节点1类型为 "example"
   - 设置节点1名称为 "node1"
   - 配置节点1能力（RPC、Pub/Sub、Server、Client）
   - 设置节点1监听地址为 "tcp://127.0.0.1:8890"
   - 使用 `ssn_node_create` 创建节点1

2. **节点2配置与创建**
   - 设置节点2类型为 "example"
   - 设置节点2名称为 "node2"
   - 配置节点2能力（RPC、Pub/Sub、Server、Client）
   - 设置节点2连接地址为 "tcp://127.0.0.1:8890"
   - 使用 `ssn_node_create` 创建节点2

3. **节点启动**
   - 使用 `ssn_node_start` 启动节点1
   - 使用 `ssn_node_start` 启动节点2

4. **节点间通信**
   - 节点1向节点2发送消息
   - 节点2向节点1发送消息
   - 节点1调用节点2的 RPC 方法
   - 节点2发布消息，节点1订阅

5. **节点停止与销毁**
   - 使用 `ssn_node_stop` 停止节点1
   - 使用 `ssn_node_stop` 停止节点2
   - 使用 `ssn_node_destroy` 销毁节点1
   - 使用 `ssn_node_destroy` 销毁节点2

## 运行示例

### 构建示例

```bash
cd examples/node/02_node_comm
make
```

### 运行示例

```bash
make run
```

或者直接运行：

```bash
./node_comm
```

## 预期输出

```
[INFO] [node_comm.c:142] main(): Starting node communication example...
[INFO] [ssn_node.c:170] ssn_node_create(): node created successfully, id=example_node1_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456, type=example, name=node1
[INFO] [node_comm.c:152] main(): Node 1 created successfully
[INFO] [ssn_node.c:170] ssn_node_create(): node created successfully, id=example_node2_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456, type=example, name=node2
[INFO] [node_comm.c:162] main(): Node 2 created successfully
[INFO] [node_comm.c:167] main(): Starting node 1...
[INFO] [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/transports/ssn_transport_factory.c:102] ssn_transport_factory_init(): Transport factory initialized successfully
[INFO] [ssn_node.c:230] ssn_node_start(): server started on tcp://127.0.0.1:8890
[INFO] [ssn_node.c:244] ssn_node_start(): node started successfully, id=example_node1_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_comm.c:172] main(): Node 1 started successfully
[INFO] [node_comm.c:177] main(): Starting node 2...
[INFO] [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/transports/ssn_transport_factory.c:102] ssn_transport_factory_init(): Transport factory initialized successfully
[INFO] [ssn_node.c:230] ssn_node_start(): server started on tcp://127.0.0.1:8890
[INFO] [ssn_node.c:244] ssn_node_start(): node started successfully, id=example_node2_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_comm.c:182] main(): Node 2 started successfully
[INFO] [node_comm.c:187] main(): Testing node communication...
[INFO] [node_comm.c:46] message_handler(): Node 1 received message: Hello from Node 2!
[INFO] [node_comm.c:46] message_handler(): Node 2 received message: Hello from Node 1!
[INFO] [node_comm.c:197] main(): Node communication test completed
[INFO] [node_comm.c:202] main(): Running for 5 seconds...
[INFO] [node_comm.c:207] main(): Stopping nodes...
[INFO] [ssn_node.c:272] ssn_node_stop(): server stopped
[INFO] [ssn_node.c:286] ssn_node_stop(): node stopped successfully, id=example_node1_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [ssn_node.c:272] ssn_node_stop(): server stopped
[INFO] [ssn_node.c:286] ssn_node_stop(): node stopped successfully, id=example_node2_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_comm.c:212] main(): Nodes stopped successfully
[INFO] [node_comm.c:217] main(): Destroying nodes...
[INFO] [ssn_node.c:322] ssn_node_destroy(): node destroyed, id=example_node1_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [ssn_node.c:322] ssn_node_destroy(): node destroyed, id=example_node2_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_comm.c:222] main(): Nodes destroyed successfully
[INFO] [node_comm.c:224] main(): Node communication example completed
```

## 注意事项

- 本示例使用 TCP 作为传输协议
- 节点1监听地址为 `tcp://127.0.0.1:8890`
- 节点2连接地址为 `tcp://127.0.0.1:8890`
- 节点运行 5 秒后自动停止

## 相关 API

- `ssn_node_create` - 创建节点
- `ssn_node_start` - 启动节点
- `ssn_node_stop` - 停止节点
- `ssn_node_destroy` - 销毁节点
- `ssn_node_get_client` - 获取节点的客户端
- `ssn_node_get_server` - 获取节点的服务器
- `ssn_node_send_to_peer` - 节点间发送消息
- `ssn_node_rpc_call` - 调用 RPC 方法
- `ssn_node_subscribe` - 订阅主题
- `ssn_node_publish` - 发布消息
