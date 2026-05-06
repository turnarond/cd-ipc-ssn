# 01_multithread 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的多线程 IPC 功能。它演示了如何在多线程环境中使用 IPC 客户端和服务器，以及如何处理并发请求。

## 功能特性

- 创建多线程 IPC 服务器
- 创建多个客户端线程
- 客户端线程并发发送请求
- 服务器处理并发请求
- 线程安全的消息处理

## 代码结构

```
01_multithread/
├── client.c      # 多线程客户端代码
├── server.c      # 多线程服务器代码
├── Makefile      # 构建脚本
└── README.md     # 本说明文档
```

## 核心代码解析

### 服务器端 (server.c)

1. **服务器创建与启动**
   - 使用 `ipc_server_create_with_options` 创建服务器
   - 设置服务器地址为 `unix:///tmp/multithread_server`
   - 启动服务器并开始监听

2. **多线程处理**
   - 主线程使用 `ipc_server_poll` 轮询服务器事件
   - 当接收到客户端消息时，在主线程中处理

3. **消息处理**
   - 注册消息处理回调函数 `message_handler`
   - 当接收到客户端消息时，打印消息内容并发送响应

### 客户端 (client.c)

1. **多线程客户端**
   - 创建 5 个客户端线程
   - 每个线程创建自己的 IPC 客户端
   - 每个线程连接到服务器并发送多个请求

2. **客户端连接与消息发送**
   - 每个线程使用 `ipc_client_create` 创建客户端
   - 连接到服务器地址 `unix:///tmp/multithread_server`
   - 每个线程发送 10 个消息

3. **响应处理**
   - 每个线程注册消息处理回调函数 `message_handler`
   - 当接收到服务器响应时，打印响应内容

## 运行示例

### 构建示例

```bash
cd examples/advanced/01_multithread
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
[INFO] [server.c:72] main(): Starting multithread IPC server...
[INFO] [server.c:89] main(): IPC server created successfully
[INFO] [server.c:104] main(): IPC server started on unix:///tmp/multithread_server
[INFO] [server.c:107] main(): Server running for 30 seconds...
[INFO] [server.c:62] connect_handler(): Client connected: id=0
[INFO] [server.c:44] message_handler(): Received message from client 0: Thread 0, Message 0
[INFO] [server.c:52] message_handler(): Sending response: Thread 0, Message 0 - Server Response
[INFO] [server.c:62] connect_handler(): Client connected: id=1
[INFO] [server.c:44] message_handler(): Received message from client 1: Thread 1, Message 0
[INFO] [server.c:52] message_handler(): Sending response: Thread 1, Message 0 - Server Response
[INFO] [server.c:62] connect_handler(): Client connected: id=2
[INFO] [server.c:44] message_handler(): Received message from client 2: Thread 2, Message 0
[INFO] [server.c:52] message_handler(): Sending response: Thread 2, Message 0 - Server Response
[INFO] [server.c:62] connect_handler(): Client connected: id=3
[INFO] [server.c:44] message_handler(): Received message from client 3: Thread 3, Message 0
[INFO] [server.c:52] message_handler(): Sending response: Thread 3, Message 0 - Server Response
[INFO] [server.c:62] connect_handler(): Client connected: id=4
[INFO] [server.c:44] message_handler(): Received message from client 4: Thread 4, Message 0
[INFO] [server.c:52] message_handler(): Sending response: Thread 4, Message 0 - Server Response
[INFO] [server.c:44] message_handler(): Received message from client 0: Thread 0, Message 1
[INFO] [server.c:52] message_handler(): Sending response: Thread 0, Message 1 - Server Response
[INFO] [server.c:44] message_handler(): Received message from client 1: Thread 1, Message 1
[INFO] [server.c:52] message_handler(): Sending response: Thread 1, Message 1 - Server Response
[INFO] [server.c:44] message_handler(): Received message from client 2: Thread 2, Message 1
[INFO] [server.c:52] message_handler(): Sending response: Thread 2, Message 1 - Server Response
[INFO] [server.c:44] message_handler(): Received message from client 3: Thread 3, Message 1
[INFO] [server.c:52] message_handler(): Sending response: Thread 3, Message 1 - Server Response
[INFO] [server.c:44] message_handler(): Received message from client 4: Thread 4, Message 1
[INFO] [server.c:52] message_handler(): Sending response: Thread 4, Message 1 - Server Response
...
[INFO] [server.c:62] connect_handler(): Client disconnected: id=0
[INFO] [server.c:62] connect_handler(): Client disconnected: id=1
[INFO] [server.c:62] connect_handler(): Client disconnected: id=2
[INFO] [server.c:62] connect_handler(): Client disconnected: id=3
[INFO] [server.c:62] connect_handler(): Client disconnected: id=4
[INFO] [server.c:116] main(): Stopping IPC server...
[INFO] [server.c:121] main(): IPC server destroyed
```

### 客户端输出

```
[INFO] [client.c:92] main(): Starting multithread IPC client...
[INFO] [client.c:101] main(): Creating 5 client threads...
[INFO] [client.c:44] client_thread(): Thread 0: Starting
[INFO] [client.c:44] client_thread(): Thread 1: Starting
[INFO] [client.c:44] client_thread(): Thread 2: Starting
[INFO] [client.c:44] client_thread(): Thread 3: Starting
[INFO] [client.c:44] client_thread(): Thread 4: Starting
[INFO] [client.c:53] client_thread(): Thread 0: Client created successfully
[INFO] [client.c:63] client_thread(): Thread 0: Connected to server
[INFO] [client.c:53] client_thread(): Thread 1: Client created successfully
[INFO] [client.c:63] client_thread(): Thread 1: Connected to server
[INFO] [client.c:53] client_thread(): Thread 2: Client created successfully
[INFO] [client.c:63] client_thread(): Thread 2: Connected to server
[INFO] [client.c:53] client_thread(): Thread 3: Client created successfully
[INFO] [client.c:63] client_thread(): Thread 3: Connected to server
[INFO] [client.c:53] client_thread(): Thread 4: Client created successfully
[INFO] [client.c:63] client_thread(): Thread 4: Connected to server
[INFO] [client.c:71] client_thread(): Thread 0: Sending message 0
[INFO] [client.c:71] client_thread(): Thread 1: Sending message 0
[INFO] [client.c:71] client_thread(): Thread 2: Sending message 0
[INFO] [client.c:71] client_thread(): Thread 3: Sending message 0
[INFO] [client.c:71] client_thread(): Thread 4: Sending message 0
[INFO] [client.c:34] message_handler(): Thread 0: Received response: Thread 0, Message 0 - Server Response
[INFO] [client.c:34] message_handler(): Thread 1: Received response: Thread 1, Message 0 - Server Response
[INFO] [client.c:34] message_handler(): Thread 2: Received response: Thread 2, Message 0 - Server Response
[INFO] [client.c:34] message_handler(): Thread 3: Received response: Thread 3, Message 0 - Server Response
[INFO] [client.c:34] message_handler(): Thread 4: Received response: Thread 4, Message 0 - Server Response
[INFO] [client.c:71] client_thread(): Thread 0: Sending message 1
[INFO] [client.c:71] client_thread(): Thread 1: Sending message 1
[INFO] [client.c:71] client_thread(): Thread 2: Sending message 1
[INFO] [client.c:71] client_thread(): Thread 3: Sending message 1
[INFO] [client.c:71] client_thread(): Thread 4: Sending message 1
[INFO] [client.c:34] message_handler(): Thread 0: Received response: Thread 0, Message 1 - Server Response
[INFO] [client.c:34] message_handler(): Thread 1: Received response: Thread 1, Message 1 - Server Response
[INFO] [client.c:34] message_handler(): Thread 2: Received response: Thread 2, Message 1 - Server Response
[INFO] [client.c:34] message_handler(): Thread 3: Received response: Thread 3, Message 1 - Server Response
[INFO] [client.c:34] message_handler(): Thread 4: Received response: Thread 4, Message 1 - Server Response
...
[INFO] [client.c:83] client_thread(): Thread 0: Client closed
[INFO] [client.c:83] client_thread(): Thread 1: Client closed
[INFO] [client.c:83] client_thread(): Thread 2: Client closed
[INFO] [client.c:83] client_thread(): Thread 3: Client closed
[INFO] [client.c:83] client_thread(): Thread 4: Client closed
[INFO] [client.c:106] main(): All client threads completed
[INFO] [client.c:107] main(): Client closed
```

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 服务器地址为 `unix:///tmp/multithread_server`
- 服务器运行 30 秒后自动停止
- 客户端创建 5 个线程，每个线程发送 10 个消息

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
