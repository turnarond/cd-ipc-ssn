# 02_rpc_call 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的 RPC（远程过程调用）功能。它演示了如何创建一个 RPC 服务器，注册 RPC 方法，以及如何创建一个客户端调用这些 RPC 方法。

## 功能特性

- 创建 RPC 服务器
- 注册 RPC 方法（加法、减法、乘法、除法）
- 创建 RPC 客户端
- 客户端连接到服务器
- 客户端调用 RPC 方法
- 服务器处理 RPC 请求并返回结果
- 客户端处理 RPC 响应（含除零错误响应）

## 代码结构

```
02_rpc_call/
├── client.c      # 客户端代码
├── server.c      # 服务器代码
├── Makefile      # 构建脚本
└── README.md     # 本说明文档
```

## 核心代码解析

### 服务器端 (server.c)

1. **服务器创建与启动**
   - 使用 `ssn_server_create_with_options` 创建服务器
   - 服务器地址为 `unix:///tmp/rpc_server`
   - 使用 `ssn_server_start` 启动服务器并开始监听

2. **RPC 方法注册**
   - 使用 `ssn_server_add_method` 注册 4 个方法：
     - `/math/add`（`add_handler`）
     - `/math/subtract`（`subtract_handler`）
     - `/math/multiply`（`multiply_handler`）
     - `/math/divide`（`divide_handler`）

3. **RPC 方法实现**
   - 各 handler 解析 JSON 风格参数（`sscanf`），计算结果后通过 `ssn_server_response` 返回
   - `divide_handler` 处理除零错误：除数为 0 时返回错误响应 `Error: Division by zero`

### 客户端 (client.c)

1. **客户端创建与连接**
   - 使用 `ssn_client_create` 创建客户端
   - 使用 `ssn_client_connect` 连接到服务器地址 `unix:///tmp/rpc_server`（连接超时 5 秒）

2. **RPC 调用**
   - `make_rpc_call` 封装调用流程：`ssn_client_call` 发送请求，随后轮询 `ssn_client_poll` 等待响应（超时 5 秒）
   - 依次调用 5 次：`/math/add`（5+3）、`/math/subtract`（10-4）、`/math/multiply`（6*7）、`/math/divide`（20/4）、`/math/divide` 除零测试（10/0）

3. **响应处理**
   - 注册 RPC 响应处理回调函数 `rpc_reply_handler`
   - 收到响应时打印结果；除零测试中收到错误响应 `Error: Division by zero`

## 运行示例

### 构建示例

```bash
cd examples/basic/02_rpc_call
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
[INFO] [server.c:218] main(): Starting RPC server...
[INFO] [server.c:235] main(): RPC server created successfully
[INFO] [server.c:251] main(): RPC methods registered successfully
[INFO] [server.c:260] main(): RPC server started on unix:///tmp/rpc_server
[INFO] [server.c:263] main(): Server running for 15 seconds...
[INFO] [server.c:207] connect_handler(): Client connected: id=0
[INFO] [server.c:33] add_handler(): RPC method called: /math/add with parameters: {"a": 5, "b": 3}
[INFO] [server.c:55] add_handler(): RPC method /math/add returned: 8
[INFO] [server.c:74] subtract_handler(): RPC method called: /math/subtract with parameters: {"a": 10, "b": 4}
[INFO] [server.c:96] subtract_handler(): RPC method /math/subtract returned: 6
[INFO] [server.c:115] multiply_handler(): RPC method called: /math/multiply with parameters: {"a": 6, "b": 7}
[INFO] [server.c:137] multiply_handler(): RPC method /math/multiply returned: 42
[INFO] [server.c:156] divide_handler(): RPC method called: /math/divide with parameters: {"a": 20, "b": 4}
[INFO] [server.c:190] divide_handler(): RPC method /math/divide returned: 5
[INFO] [server.c:156] divide_handler(): RPC method called: /math/divide with parameters: {"a": 10, "b": 0}
[ERROR] [server.c:168] divide_handler(): Division by zero
[INFO] [server.c:209] connect_handler(): Client disconnected: id=0
[INFO] [server.c:272] main(): Stopping RPC server...
[INFO] [server.c:277] main(): RPC server destroyed
```

### 客户端输出

```
[INFO] [client.c:92] main(): Starting RPC client...
[INFO] [client.c:101] main(): RPC client created successfully
[INFO] [client.c:116] main(): Connected to server: unix:///tmp/rpc_server
[INFO] [client.c:119] main(): Testing RPC methods...
[INFO] [client.c:122] main():
Testing /math/add

[INFO] [client.c:36] rpc_reply_handler(): RPC call successful, result: 8
[INFO] [client.c:126] main():
Testing /math/subtract

[INFO] [client.c:36] rpc_reply_handler(): RPC call successful, result: 6
[INFO] [client.c:130] main():
Testing /math/multiply

[INFO] [client.c:36] rpc_reply_handler(): RPC call successful, result: 42
[INFO] [client.c:134] main():
Testing /math/divide

[INFO] [client.c:36] rpc_reply_handler(): RPC call successful, result: 5
[INFO] [client.c:138] main():
Testing /math/divide (division by zero)

[INFO] [client.c:36] rpc_reply_handler(): RPC call successful, result: Error: Division by zero
[INFO] [client.c:142] main():
Waiting for all responses...

[INFO] [client.c:151] main(): RPC client closed
```

> 注：实际输出中每条日志还包含时间戳与线程 ID 前缀（`[时间] [INFO] [线程ID] ...`），以上为便于阅读省略；`LOG_INFO("\nTesting ...")` 中的 `\n` 会使日志内容换行显示（即 "main(): " 后跟空行再输出测试名），与上述一致。

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 服务器地址为 `unix:///tmp/rpc_server`
- 服务器运行 15 秒后自动停止（事件循环 `ssn_server_poll` + `sleep(1)` 共 15 次）
- 客户端依次发起 5 次 RPC 调用，每次最多等待 5 秒响应
- 除零测试中服务器返回错误响应，客户端按正常响应打印 `Error: Division by zero`

## 相关 API

- `ssn_server_create_with_options` - 创建 IPC 服务器
- `ssn_server_start` - 启动 IPC 服务器
- `ssn_server_add_method` - 添加 RPC 方法
- `ssn_server_set_connect_handler` - 设置连接处理回调
- `ssn_server_poll` - 轮询服务器事件
- `ssn_server_response` - 发送 RPC 响应
- `ssn_server_destroy` - 销毁 IPC 服务器
- `ssn_client_create` - 创建 IPC 客户端
- `ssn_client_connect` - 连接到服务器
- `ssn_client_call` - 调用 RPC 方法
- `ssn_client_poll` - 轮询客户端事件
- `ssn_client_disconnect` - 断开与服务器的连接
- `ssn_client_close` - 关闭 IPC 客户端
