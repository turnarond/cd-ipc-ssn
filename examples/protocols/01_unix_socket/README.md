# 01_unix_socket 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的 Unix Socket 传输协议功能。它演示了如何使用 Unix Socket 作为传输协议进行进程间通信。

## 功能特性

- 使用 Unix Socket 作为传输协议
- 创建 Unix Socket 服务器
- 创建 Unix Socket 客户端
- 客户端连接到服务器
- 客户端与服务器之间的消息传递

## 代码结构

```
01_unix_socket/
├── client.c      # Unix Socket 客户端代码
├── server.c      # Unix Socket 服务器代码
├── Makefile      # 构建脚本
└── README.md     # 本说明文档
```

## 核心代码解析

### 服务器端 (server.c)

1. **服务器创建与启动**
   - 使用 `ssn_server_create_with_options` 创建服务器
   - 设置服务器地址为 `unix:///tmp/unix_socket_server`
   - 启动服务器并开始监听

2. **消息处理**
   - 注册消息处理回调函数 `message_handler`
   - 当接收到客户端消息时，打印消息内容并发送响应

3. **连接处理**
   - 注册连接处理回调函数 `connect_handler`
   - 当客户端连接或断开时，打印连接状态

### 客户端 (client.c)

1. **客户端创建与连接**
   - 使用 `ssn_client_create` 创建客户端
   - 连接到服务器地址 `unix:///tmp/unix_socket_server`

2. **消息发送**
   - 向服务器发送 "Hello from Unix Socket client!" 消息
   - 等待服务器响应

3. **响应处理**
   - 注册消息处理回调函数 `message_handler`
   - 当接收到服务器响应时，打印响应内容

## 运行示例

### 构建示例

```bash
cd examples/protocols/01_unix_socket
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
[INFO] [server.c:67] main(): Starting Unix Socket server...
[INFO] [server.c:84] main(): Unix Socket server created successfully
[INFO] [server.c:99] main(): Unix Socket server started on unix:///tmp/unix_socket_server
[INFO] [server.c:102] main(): Server running for 10 seconds...
[INFO] [server.c:56] connect_handler(): Client connected: id=0
[INFO] [server.c:38] message_handler(): Received message: Hello from Unix Socket client!
[INFO] [server.c:46] message_handler(): Sending response: Hello from Unix Socket server!
[INFO] [server.c:56] connect_handler(): Client disconnected: id=0
[INFO] [server.c:111] main(): Stopping Unix Socket server...
[INFO] [server.c:116] main(): Unix Socket server destroyed
```

### 客户端输出

```
[INFO] [client.c:44] main(): Starting Unix Socket client...
[INFO] [client.c:53] main(): Unix Socket client created successfully
[INFO] [client.c:63] main(): Connected to server: unix:///tmp/unix_socket_server
[INFO] [client.c:71] main(): Sending message: Hello from Unix Socket client!
[INFO] [client.c:34] message_handler(): Received message: Hello from Unix Socket server!
[INFO] [client.c:81] main(): Client closed
```

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 服务器地址为 `unix:///tmp/unix_socket_server`
- 服务器运行 10 秒后自动停止
- 客户端发送消息后等待 5 秒后退出

## 相关 API

- `ssn_server_create_with_options` - 创建 IPC 服务器
- `ssn_server_start` - 启动 IPC 服务器
- `ssn_server_set_message_handler` - 设置消息处理回调
- `ssn_server_set_connect_handler` - 设置连接处理回调
- `ssn_server_poll` - 轮询服务器事件
- `ssn_server_destroy` - 销毁 IPC 服务器
- `ssn_client_create` - 创建 IPC 客户端
- `ssn_client_connect` - 连接到服务器
- `ssn_client_message` - 发送消息
- `ssn_client_set_on_message` - 设置消息处理回调
- `ssn_client_poll` - 轮询客户端事件
- `ssn_client_close` - 关闭 IPC 客户端
