# 01_unix_socket 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的 Unix Socket 传输协议功能。它演示了如何使用 Unix Socket 作为传输协议进行进程间通信。

## 功能特性

- 使用 Unix Socket 作为传输协议
- 创建 Unix Socket 服务器
- 创建 Unix Socket 客户端
- 客户端连接到服务器
- 客户端向服务器发送消息（服务器接收并打印，不回送应答）
- 启动时清理历史 socket 文件、结束时清理 socket 文件

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
   - 启动前检查 `/tmp/unix_socket_server` 是否存在，存在则先删除（避免 bind 失败）
   - 使用 `ssn_server_create_with_options` 创建服务器
   - 服务器地址为 `unix:///tmp/unix_socket_server`
   - 使用 `ssn_server_start` 启动服务器并开始监听

2. **消息处理**
   - 注册消息处理回调函数 `message_handler`
   - 当接收到客户端消息时，打印消息内容（不发送应答）

3. **连接处理**
   - 注册连接处理回调函数 `connect_handler`
   - 当客户端连接或断开时，打印连接状态

4. **事件循环与清理**
   - 主循环调用 `ssn_server_poll(server, 100)` 处理连接与消息事件，共运行 10 秒
   - 销毁服务器后删除 socket 文件 `/tmp/unix_socket_server`

### 客户端 (client.c)

1. **客户端创建与连接**
   - 使用 `ssn_client_create` 创建客户端
   - 使用 `ssn_client_connect` 连接到服务器地址 `unix:///tmp/unix_socket_server`（连接超时 5 秒）

2. **消息发送**
   - 向服务器发送 "Hello from Unix Socket client!"（30 字节），消息路径为 `/unix/test`

3. **消息处理**
   - 注册消息处理回调函数 `message_handler`（服务器不回送应答，本示例中不会被触发）

## 运行示例

### 构建示例

```bash
cd examples/protocols/01_unix_socket
make clean && make
```

> 二进制已内置 rpath，构建完成后可直接运行，无需设置 `LD_LIBRARY_PATH`。

### 运行示例

```bash
make run
```

或者分别在两个终端运行服务器和客户端：

```bash
# 运行服务器
./server

# 运行客户端（在另一个终端）
./client
```

## 预期输出

### 服务器输出

```
[INFO] [server.c:66] main(): Starting Unix Socket server...
[INFO] [server.c:73] main(): Removed existing file: /tmp/unix_socket_server
[INFO] [server.c:91] main(): Unix Socket server created successfully
[INFO] [server.c:106] main(): Unix Socket server started on unix:///tmp/unix_socket_server
[INFO] [server.c:109] main(): Server running for 10 seconds...
[INFO] [server.c:55] connect_handler(): Client connected: id=0
[INFO] [server.c:37] message_handler(): Received message: Hello from Unix Socket client!
[INFO] [server.c:57] connect_handler(): Client disconnected: id=0
[INFO] [server.c:118] main(): Stopping Unix Socket server...
[INFO] [server.c:125] main(): Cleaned up socket file: /tmp/unix_socket_server
```

### 客户端输出

```
[INFO] [client.c:41] main(): Starting Unix Socket client...
[INFO] [client.c:50] main(): Unix Socket client created successfully
[INFO] [client.c:68] main(): Unix Socket client connected to unix:///tmp/unix_socket_server
[INFO] [client.c:89] main(): Message sent successfully
[INFO] [client.c:92] main(): Waiting for 2 seconds...
[INFO] [client.c:101] main(): Unix Socket client disconnected
```

> 注：
> - 实际输出中每条日志还包含时间戳与线程 ID 前缀（`[时间] [INFO] [线程ID] ...`），以上为便于阅读省略。
> - 若 `/tmp/unix_socket_server` 不存在（首次运行），服务器不会打印 "Removed existing file" 日志。
> - 服务器不回送应答，因此客户端不会打印 "Received message from server" 日志。

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 服务器地址为 `unix:///tmp/unix_socket_server`
- 服务器运行 10 秒后自动停止（事件循环 `ssn_server_poll` + `sleep(1)` 共 10 次）
- 客户端发送消息后等待 2 秒退出

## 相关 API

- `ssn_server_create_with_options` - 创建 IPC 服务器
- `ssn_server_start` - 启动 IPC 服务器
- `ssn_server_set_message_handler` - 设置消息处理回调
- `ssn_server_set_connect_handler` - 设置连接处理回调
- `ssn_server_poll` - 轮询服务器事件
- `ssn_server_destroy` - 销毁 IPC 服务器
- `ssn_client_create` - 创建 IPC 客户端
- `ssn_client_set_on_message` - 设置消息处理回调
- `ssn_client_connect` - 连接到服务器
- `ssn_client_message` - 发送消息
- `ssn_client_disconnect` - 断开与服务器的连接
- `ssn_client_close` - 关闭 IPC 客户端
