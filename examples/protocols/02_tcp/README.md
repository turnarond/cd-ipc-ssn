# 02_tcp 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的 TCP 传输协议功能。它演示了如何使用 TCP 作为传输协议进行网络通信。

## 功能特性

- 使用 TCP 作为传输协议
- 创建 TCP 服务器
- 创建 TCP 客户端
- 客户端连接到服务器
- 客户端向服务器发送消息（服务器接收并打印，不回送应答）

## 代码结构

```
02_tcp/
├── client.c      # TCP 客户端代码
├── server.c      # TCP 服务器代码
├── Makefile      # 构建脚本
└── README.md     # 本说明文档
```

## 核心代码解析

### 服务器端 (server.c)

1. **服务器创建与启动**
   - 使用 `ssn_server_create_with_options` 创建服务器
   - 服务器地址为 `tcp://127.0.0.1:8888`
   - 使用 `ssn_server_start` 启动服务器并开始监听

2. **消息处理**
   - 注册消息处理回调函数 `message_handler`
   - 当接收到客户端消息时，打印消息内容（不发送应答）

3. **连接处理**
   - 注册连接处理回调函数 `connect_handler`
   - 当客户端连接或断开时，打印连接状态

4. **事件循环**
   - 主循环调用 `ssn_server_poll(server, 100)` 处理连接与消息事件，共运行 10 秒后销毁服务器

### 客户端 (client.c)

1. **客户端创建与连接**
   - 使用 `ssn_client_create` 创建客户端
   - 使用 `ssn_client_connect` 连接到服务器地址 `tcp://127.0.0.1:8888`（连接超时 5 秒）

2. **消息发送**
   - 向服务器发送 "Hello from TCP client!"（22 字节），消息路径为 `/tcp/test`

3. **消息处理**
   - 注册消息处理回调函数 `message_handler`（服务器不回送应答，本示例中不会被触发）

## 运行示例

### 构建示例

```bash
cd examples/protocols/02_tcp
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
[INFO] [server.c:63] main(): Starting TCP server...
[INFO] [server.c:80] main(): TCP server created successfully
[INFO] [server.c:95] main(): TCP server started on tcp://127.0.0.1:8888
[INFO] [server.c:98] main(): Server running for 10 seconds...
[INFO] [server.c:52] connect_handler(): Client connected: id=0
[INFO] [server.c:34] message_handler(): Received message: Hello from TCP client!
[INFO] [server.c:54] connect_handler(): Client disconnected: id=0
[INFO] [server.c:107] main(): Stopping TCP server...
```

### 客户端输出

```
[INFO] [client.c:41] main(): Starting TCP client...
[INFO] [client.c:50] main(): TCP client created successfully
[INFO] [client.c:68] main(): TCP client connected to tcp://127.0.0.1:8888
[INFO] [client.c:89] main(): Message sent successfully
[INFO] [client.c:92] main(): Waiting for 2 seconds...
[INFO] [client.c:101] main(): TCP client disconnected
```

> 注：
> - 实际输出中每条日志还包含时间戳与线程 ID 前缀（`[时间] [INFO] [线程ID] ...`），以上为便于阅读省略。
> - 服务器不回送应答，因此客户端不会打印 "Received message from server" 日志。

## 注意事项

- 本示例使用 TCP 作为传输协议
- 服务器地址为 `tcp://127.0.0.1:8888`
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
