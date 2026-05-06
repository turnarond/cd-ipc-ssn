# 03_node_rpc 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的节点 RPC 功能。它演示了如何在节点之间进行远程过程调用。

## 功能特性

- 创建两个节点
- 启动两个节点
- 在节点上注册 RPC 方法
- 节点间 RPC 调用
- 处理 RPC 响应

## 代码结构

```
03_node_rpc/
├── node_rpc.c  # 节点 RPC 示例代码
├── Makefile    # 构建脚本
└── README.md   # 本说明文档
```

## 核心代码解析

### 节点 RPC 示例 (node_rpc.c)

1. **节点1配置与创建**
   - 设置节点1类型为 "example"
   - 设置节点1名称为 "rpc_server_node"
   - 配置节点1能力（RPC、Server）
   - 设置节点1监听地址为 "unix:///tmp/rpc_server_node"
   - 使用 `ipc_node_create` 创建节点1

2. **节点2配置与创建**
   - 设置节点2类型为 "example"
   - 设置节点2名称为 "rpc_client_node"
   - 配置节点2能力（RPC、Client）
   - 设置节点2监听地址为 "unix:///tmp/rpc_client_node"
   - 使用 `ipc_node_create` 创建节点2

3. **RPC 方法注册**
   - 在节点1上注册 `add_handler` 处理 `/math/add` 方法
   - 在节点1上注册 `subtract_handler` 处理 `/math/subtract` 方法

4. **节点启动**
   - 使用 `ipc_node_start` 启动节点1
   - 使用 `ipc_node_start` 启动节点2

5. **节点间 RPC 调用**
   - 节点2调用节点1的 `/math/add` 方法，计算 5 + 3
   - 节点2调用节点1的 `/math/subtract` 方法，计算 10 - 4

6. **RPC 响应处理**
   - 注册 RPC 响应处理回调函数 `rpc_reply_handler`
   - 当接收到 RPC 响应时，打印响应结果

7. **节点停止与销毁**
   - 使用 `ipc_node_stop` 停止节点1
   - 使用 `ipc_node_stop` 停止节点2
   - 使用 `ipc_node_destroy` 销毁节点1
   - 使用 `ipc_node_destroy` 销毁节点2

## 运行示例

### 构建示例

```bash
cd examples/node/03_node_rpc
make
```

### 运行示例

```bash
make run
```

或者直接运行：

```bash
./node_rpc
```

## 预期输出

```
[INFO] [node_rpc.c:172] main(): Starting node RPC example...
[INFO] [ipc_node.c:170] ipc_node_create(): node created successfully, id=example_rpc_server_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456, type=example, name=rpc_server_node
[INFO] [node_rpc.c:182] main(): Server node created successfully
[INFO] [ipc_node.c:170] ipc_node_create(): node created successfully, id=example_rpc_client_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456, type=example, name=rpc_client_node
[INFO] [node_rpc.c:192] main(): Client node created successfully
[INFO] [node_rpc.c:197] main(): Registering RPC methods on server node...
[INFO] [node_rpc.c:207] main(): RPC methods registered successfully
[INFO] [node_rpc.c:212] main(): Starting server node...
[INFO] [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/transports/ssn_transport_factory.c:102] ssn_transport_factory_init(): Transport factory initialized successfully
[INFO] [ipc_node.c:230] ipc_node_start(): server started on unix:///tmp/rpc_server_node
[INFO] [ipc_node.c:244] ipc_node_start(): node started successfully, id=example_rpc_server_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_rpc.c:217] main(): Server node started successfully
[INFO] [node_rpc.c:222] main(): Starting client node...
[INFO] [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/transports/ssn_transport_factory.c:102] ssn_transport_factory_init(): Transport factory initialized successfully
[INFO] [ipc_node.c:230] ipc_node_start(): server started on unix:///tmp/rpc_client_node
[INFO] [ipc_node.c:244] ipc_node_start(): node started successfully, id=example_rpc_client_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_rpc.c:227] main(): Client node started successfully
[INFO] [node_rpc.c:232] main(): Testing node RPC calls...
[INFO] [node_rpc.c:46] add_handler(): RPC method called: /math/add with parameters: {"a": 5, "b": 3}
[INFO] [node_rpc.c:68] add_handler(): RPC method /math/add returned: 8
[INFO] [node_rpc.c:98] rpc_reply_handler(): RPC call successful, result: 8
[INFO] [node_rpc.c:78] subtract_handler(): RPC method called: /math/subtract with parameters: {"a": 10, "b": 4}
[INFO] [node_rpc.c:100] subtract_handler(): RPC method /math/subtract returned: 6
[INFO] [node_rpc.c:98] rpc_reply_handler(): RPC call successful, result: 6
[INFO] [node_rpc.c:242] main(): Node RPC test completed
[INFO] [node_rpc.c:247] main(): Running for 5 seconds...
[INFO] [node_rpc.c:252] main(): Stopping nodes...
[INFO] [ipc_node.c:272] ipc_node_stop(): server stopped
[INFO] [ipc_node.c:286] ipc_node_stop(): node stopped successfully, id=example_rpc_server_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [ipc_node.c:272] ipc_node_stop(): server stopped
[INFO] [ipc_node.c:286] ipc_node_stop(): node stopped successfully, id=example_rpc_client_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_rpc.c:257] main(): Nodes stopped successfully
[INFO] [node_rpc.c:262] main(): Destroying nodes...
[INFO] [ipc_node.c:322] ipc_node_destroy(): node destroyed, id=example_rpc_server_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [ipc_node.c:322] ipc_node_destroy(): node destroyed, id=example_rpc_client_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_rpc.c:267] main(): Nodes destroyed successfully
[INFO] [node_rpc.c:269] main(): Node RPC example completed
```

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 节点1监听地址为 `unix:///tmp/rpc_server_node`
- 节点2监听地址为 `unix:///tmp/rpc_client_node`
- 节点运行 5 秒后自动停止

## 相关 API

- `ipc_node_create` - 创建节点
- `ipc_node_start` - 启动节点
- `ipc_node_stop` - 停止节点
- `ipc_node_destroy` - 销毁节点
- `ipc_node_get_client` - 获取节点的客户端
- `ipc_node_get_server` - 获取节点的服务器
- `ipc_node_add_rpc_method` - 向节点添加 RPC 方法
- `ipc_client_call` - 调用 RPC 方法
