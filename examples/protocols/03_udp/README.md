# 03_udp 示例

> 说明：示例演示 UDP 传输的客户端收发能力；**UDP 服务端握手不受支持**（框架限制：UDP 不支持 server 模式握手），示例的 server 端仅为演示 bind/recv 流程

## 示例说明

本示例展示了 cd-ipc-ssn 库的 UDP 传输协议功能。它演示了 UDP 传输下服务器 bind/recv 流程与客户端 connect 行为，以及 UDP 不支持 server 模式握手的框架限制。

## 功能特性

- 使用 UDP 作为传输协议
- 创建 UDP 服务器（bind/recv 流程演示）
- 创建 UDP 客户端
- 客户端 connect 依赖服务端握手应答，必然失败（框架限制演示）

## 代码结构

```
03_udp/
├── client.c      # UDP 客户端代码
├── server.c      # UDP 服务器代码
├── Makefile      # 构建脚本
└── README.md     # 本说明文档
```

## 核心代码解析

### 服务器端 (server.c)

1. **服务器创建与启动**
   - 使用 `ssn_server_create_with_options` 创建服务器
   - 服务器地址为 `udp://127.0.0.1:9999`
   - 使用 `ssn_server_start` 启动服务器（bind UDP 套接字）

2. **消息处理**
   - 注册消息处理回调函数 `message_handler`
   - 注意：UDP 为无连接传输，服务端 accept 恒返回 NULL，框架不向消息回调分发内容，因此实际运行中不会输出 "Received message" 日志

3. **连接处理**
   - 注册连接处理回调函数 `connect_handler`
   - 注意：UDP 为无连接传输，连接/断开回调不会触发（见文首框架限制标注），因此实际运行不会输出连接状态日志

### 客户端 (client.c)

1. **客户端创建**
   - 使用 `ssn_client_create` 创建客户端

2. **连接失败路径（预期行为）**
   - 限制演示：UDP 为无连接传输，框架不支持 UDP server 模式握手（`udp_transport_accept` 恒返回 NULL，见传输层设计文档限制标注），客户端 connect 发送握手请求后等待服务端应答，必然在接收超时（6 秒）后失败
   - **连接失败即符合预期**：失败路径下打印提示日志并直接退出（退出码 0），后续消息发送路径不会执行

## 运行示例

### 构建示例

```bash
cd examples/protocols/03_udp
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
[INFO] [server.c:63] main(): Starting UDP server...
[INFO] [server.c:80] main(): UDP server created successfully
[INFO] [server.c:95] main(): UDP server started on udp://127.0.0.1:9999
[INFO] [server.c:98] main(): Server running for 10 seconds...
[INFO] [server.c:107] main(): Stopping UDP server...
```

> 注：UDP 为无连接传输，服务端 accept 恒返回 NULL，连接/断开回调不会触发，消息回调也不会被分发，因此服务器输出中不含连接状态与消息日志（与文首限制标注一致）。

### 客户端输出

```
[INFO] [client.c:41] main(): Starting UDP client...
[INFO] [client.c:50] main(): UDP client created successfully
[INFO] [client.c:65] main(): UDP client connect failed as expected（框架限制：UDP 不支持 server 握手）
[INFO] [client.c:67] main(): UDP client closed
```

> 注：
> - 实际输出中每条日志还包含时间戳与线程 ID 前缀（`[时间] [INFO] [线程ID] ...`），以上为便于阅读省略。
> - 客户端 connect 在接收超时（6 秒）后失败，连接失败路径为**预期行为**，不会打印 "UDP client connected to ..." 与消息发送相关日志。

## 注意事项

- 本示例使用 UDP 作为传输协议
- 服务器地址为 `udp://127.0.0.1:9999`
- **UDP 不支持 server 模式握手是框架限制**：客户端连接失败即符合预期（退出码 0），示例用于演示该限制
- 服务器运行 10 秒后自动停止（事件循环 `ssn_server_poll` + `sleep(1)` 共 10 次）
- 客户端在连接接收超时（约 6 秒）后失败退出

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
- `ssn_client_close` - 关闭 IPC 客户端
