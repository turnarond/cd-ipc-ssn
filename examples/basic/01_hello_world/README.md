# 01_hello_world 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的基础客户端-服务器通信功能。它演示了如何创建一个简单的 IPC 服务器，以及如何创建一个客户端连接到该服务器并发送消息。

## 功能特性

- 创建 IPC 服务器
- 创建 IPC 客户端
- 客户端连接到服务器
- 客户端向服务器发送消息
- 服务器接收并处理客户端消息
- 服务器向客户端发送响应

## 代码结构

```
01_hello_world/
├── client.c      # 客户端代码
├── server.c      # 服务器代码
├── Makefile      # 构建脚本
└── README.md     # 本说明文档
```

## 核心代码解析

### 服务器端 (server.c)

1. **服务器创建与启动**
   - 使用 `ipc_server_create_with_options` 创建服务器
   - 设置服务器地址为 `unix:///tmp/hello_server`
   - 启动服务器并开始监听

2. **消息处理**
   - 注册消息处理回调函数 `message_handler`
   - 当接收到客户端消息时，打印消息内容并发送响应

3. **连接处理**
   - 注册连接处理回调函数 `connect_handler`
   - 当客户端连接或断开时，打印连接状态

### 客户端 (client.c)

1. **客户端创建与连接**
   - 使用 `ipc_client_create` 创建客户端
   - 连接到服务器地址 `unix:///tmp/hello_server`

2. **消息发送**
   - 向服务器发送 "Hello, IPC Server!" 消息
   - 等待服务器响应

3. **响应处理**
   - 注册消息处理回调函数 `message_handler`
   - 当接收到服务器响应时，打印响应内容

## 运行示例

### 构建示例

```bash
cd examples/basic/01_hello_world
make
```

### 运行示例

```bash
make run
```

或者分别运行服务器和客户端：

```bash
# 运行服务器
./server

# 运行客户端（在另一个终端）
./client
```

## 预期输出

### 服务器输出

```
[INFO] [server.c:67] main(): Starting Hello World IPC server...
[INFO] [server.c:84] main(): IPC server created successfully
[INFO] [server.c:99] main(): IPC server started on unix:///tmp/hello_server
[INFO] [server.c:102] main(): Server running for 10 seconds...
[INFO] [server.c:56] connect_handler(): Client connected: id=0
[INFO] [server.c:38] message_handler(): Received message: Hello, IPC Server!
[INFO] [server.c:46] message_handler(): Sending response: Hello, IPC Client!
[INFO] [server.c:56] connect_handler(): Client disconnected: id=0
[INFO] [server.c:111] main(): Stopping IPC server...
[INFO] [server.c:116] main(): IPC server destroyed
```

### 客户端输出

```
[INFO] [client.c:44] main(): Starting Hello World IPC client...
[INFO] [client.c:53] main(): IPC client created successfully
[INFO] [client.c:63] main(): Connected to server: unix:///tmp/hello_server
[INFO] [client.c:71] main(): Sending message: Hello, IPC Server!
[INFO] [client.c:34] message_handler(): Received message: Hello, IPC Client!
[INFO] [client.c:81] main(): Client closed
```

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 服务器地址为 `unix:///tmp/hello_server`
- 服务器运行 10 秒后自动停止
- 客户端发送消息后等待 5 秒后退出

## 相关 API

- `ipc_server_create_with_options` - 创建 IPC 服务器
- `ipc_server_start` - 启动 IPC 服务器
- `ipc_server_set_message_handler` - 设置消息处理回调
- `ipc_server_set_connect_handler` - 设置连接处理回调
- `ipc_server_poll` - 轮询服务器事件
- `ipc_server_destroy` - 销毁 IPC 服务器
- `ipc_client_create` - 创建 IPC 客户端
- `ipc_client_connect` - 连接到服务器
- `ipc_client_send` - 发送消息
- `ipc_client_set_on_message` - 设置消息处理回调
- `ipc_client_poll` - 轮询客户端事件
- `ipc_client_close` - 关闭 IPC 客户端
