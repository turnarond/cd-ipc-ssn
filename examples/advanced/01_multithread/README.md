# 01_multithread 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的多线程 IPC 功能。它演示了客户端多线程并发 RPC 调用，以及服务端单事件循环线程处理并发请求的模式。

## 功能特性

- 创建 IPC 服务器（单事件循环线程模型）
- 多个客户端线程共享同一个 `ssn_client` 实例并发 RPC 调用
- 服务器注册 `/echo` RPC 方法并回显请求内容
- 线程安全的回调处理（`thread_data_t` 区分调用来源）

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
   - 使用 `ssn_server_create_with_options` 创建服务器
   - 服务器地址为 `unix:///tmp/multithread_server`
   - 使用 `ssn_server_start` 启动服务器并开始监听

2. **单事件循环线程**
   - `ssn_server` 为单线程事件循环模型（由 `ssn_server_poll` 驱动）。多个线程并发 poll 同一 server 不受支持（会并发 accept/recv 同一套接字造成竞态、阻塞与崩溃），因此服务端仅创建 1 个事件循环线程 `server_thread`
   - `server_thread` 循环调用 `ssn_server_poll(server, 1000)` 处理连接与请求；主线程在销毁服务器前置位 `g_server_running` 停止标志，线程检测后退出并 join

3. **RPC 方法**
   - 注册 `/echo` RPC 方法（只注册一次），收到请求后通过 `ssn_server_response` 原样回显

4. **连接处理**
   - 注册连接处理回调函数 `connect_handler`，连接时按序号打印处理线程号

### 客户端 (client.c)

1. **多线程客户端**
   - 主线程创建 1 个 `ssn_client` 实例并连接服务器（`NUM_THREADS = 2`）
   - 创建 2 个客户端线程，**共享同一个客户端实例**（多线程并发调用同一客户端）

2. **客户端线程**
   - 每个线程向 `/echo` 发起 1 次 RPC 调用（消息内容 "Hello from thread N"），然后轮询 `ssn_client_poll` 等待响应（超时 5 秒）
   - 线程 1 与线程 2 分别延迟 0.1 秒 / 0.2 秒启动，错开调用时机

3. **响应处理**
   - 注册 RPC 响应处理回调 `rpc_reply_handler`，通过 `thread_data_t` 区分响应属于哪个线程

## 运行示例

### 构建示例

```bash
cd examples/advanced/01_multithread
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
[INFO] [server.c:126] main(): Starting multithreaded server...
[INFO] [server.c:143] main(): Multithreaded server created successfully
[INFO] [server.c:157] main(): RPC methods registered successfully
[INFO] [server.c:166] main(): Multithreaded server started on unix:///tmp/multithread_server
[INFO] [server.c:177] main(): Server running for 20 seconds...
[INFO] [server.c:107] server_thread(): Server thread <tid> started
[INFO] [server.c:90] connect_handler(): Thread 1: Handling connection from client 0
[INFO] [server.c:55] echo_handler(): Thread 1: RPC method /echo called with: Hello from thread 1
[INFO] [server.c:55] echo_handler(): Thread 1: RPC method /echo called with: Hello from thread 2
[INFO] [server.c:92] connect_handler(): Client disconnected: id=0
[INFO] [server.c:181] main(): Stopping multithreaded server...
[INFO] [server.c:117] server_thread(): Server thread <tid> stopped
[INFO] [server.c:187] main(): Multithreaded server stopped
[INFO] [server.c:192] main(): Multithreaded server destroyed
```

### 客户端输出

```
[INFO] [client.c:110] main(): Starting multithreaded client...
[INFO] [client.c:119] main(): Multithreaded client created successfully
[INFO] [client.c:134] main(): Connected to server: unix:///tmp/multithread_server
[INFO] [client.c:65] client_thread(): Client thread 1 started
[INFO] [client.c:65] client_thread(): Client thread 2 started
[INFO] [client.c:91] client_thread(): Thread 1: RPC call sent
[INFO] [client.c:91] client_thread(): Thread 2: RPC call sent
[INFO] [client.c:46] rpc_reply_handler(): Thread 1: RPC call successful: Hello from thread 1
[INFO] [client.c:46] rpc_reply_handler(): Thread 2: RPC call successful: Hello from thread 2
[INFO] [client.c:101] client_thread(): Client thread 1 stopped
[INFO] [client.c:101] client_thread(): Client thread 2 stopped
[INFO] [client.c:165] main(): Multithreaded client closed
```

> 注：
> - `<tid>` 为服务端事件循环线程的运行时线程 ID（`pthread_self() % 1000`），每次运行不同。
> - 实际输出中每条日志还包含时间戳与线程 ID 前缀（`[时间] [INFO] [线程ID] ...`），以上为便于阅读省略。
> - 客户端两个线程的日志（started / RPC call sent / RPC call successful / stopped）交错顺序与运行时机相关。

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 服务器地址为 `unix:///tmp/multithread_server`
- **服务端为单事件循环线程**：`ssn_server` 不支持多线程并发 poll，多线程并发能力由客户端多线程 RPC 调用演示
- 客户端 2 个线程共享同一个客户端实例，各发起 1 次 `/echo` RPC 调用（服务器原样回显）
- 服务器运行 20 秒后自动停止；客户端约 5~8 秒完成（`make run` 会等待服务器结束）

## 相关 API

- `ssn_server_create_with_options` - 创建 IPC 服务器
- `ssn_server_start` - 启动 IPC 服务器
- `ssn_server_add_method` - 添加 RPC 方法
- `ssn_server_response` - 发送 RPC 响应
- `ssn_server_poll` - 轮询服务器事件
- `ssn_server_destroy` - 销毁 IPC 服务器
- `ssn_client_create` - 创建 IPC 客户端
- `ssn_client_connect` - 连接到服务器
- `ssn_client_call` - 调用 RPC 方法
- `ssn_client_poll` - 轮询客户端事件
- `ssn_client_disconnect` - 断开与服务器的连接
- `ssn_client_close` - 关闭 IPC 客户端
- `pthread_create` / `pthread_join` - 创建/等待线程（POSIX 线程库）
