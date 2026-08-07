# 04_transport_selection 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的传输协议选择功能。它演示了如何在 Unix Socket、TCP、UDP 三种传输协议之间切换，以及 UDP 传输的框架限制。

## 功能特性

- Unix Socket 传输：创建服务器并连接、发送消息
- TCP 传输：创建服务器并连接、发送消息
- UDP 传输：框架限制演示（UDP 不支持 server 模式握手，客户端连接必然失败）
- 服务端事件循环线程驱动（`server_poll_thread`）

## 代码结构

```
04_transport_selection/
├── transport_selection.c  # 传输协议选择示例代码
├── Makefile               # 构建脚本
└── README.md              # 本说明文档
```

## 核心代码解析

### 测试1：Unix Socket 传输（test_unix_socket）

- 创建服务器，地址为 `unix:///tmp/unix_socket_server`
- 启动服务端事件循环线程 `server_poll_thread` 驱动 `ssn_server_poll`（连接握手需要服务端 poll 驱动）
- 创建客户端连接服务器，向 `/test` 发送 "Hello via Unix Socket"（21 字节）消息
- 发送成功后等待 2 秒，清理资源

### 测试2：TCP 传输（test_tcp）

- 创建服务器，地址为 `tcp://127.0.0.1:8888`
- 启动服务端事件循环线程，创建客户端连接并发送 "Hello via TCP"（13 字节）消息
- 发送成功后等待 2 秒，清理资源

### 测试3：UDP 传输（框架限制演示，test_udp）

- **UDP 为无连接传输，框架不支持 UDP server 模式握手**（`udp_transport_accept` 恒返回 NULL，见 `src/transports/ssn_transport_udp.c` 与传输层设计文档限制标注），`ssn_server` 无法运行于 UDP 之上
- 因此本测试**不创建服务器**，仅创建客户端尝试连接 `udp://127.0.0.1:9999`
- 客户端 connect 依赖服务端握手应答，连接必然在接收超时（6 秒）后失败——**连接失败即符合预期**，用于演示该框架限制

## 运行示例

### 构建示例

```bash
cd examples/advanced/04_transport_selection
make clean && make
```

> 二进制已内置 rpath，构建完成后可直接运行，无需设置 `LD_LIBRARY_PATH`。

### 运行示例

```bash
make run
```

或者直接运行：

```bash
./transport_selection
```

## 预期输出

```
[INFO] [transport_selection.c:295] main(): Transport selection example started
[INFO] [transport_selection.c:61] test_unix_socket(): Test 1: Unix Socket transport
[INFO] [transport_selection.c:86] test_unix_socket(): Unix Socket server started on unix:///tmp/unix_socket_server
[INFO] [transport_selection.c:116] test_unix_socket(): Unix Socket client connected
[INFO] [transport_selection.c:138] test_unix_socket(): Message sent via Unix Socket
[INFO] [transport_selection.c:149] test_unix_socket(): Test 1 passed
[INFO] [transport_selection.c:158] test_tcp():
Test 2: TCP transport

[INFO] [transport_selection.c:183] test_tcp(): TCP server started on tcp://127.0.0.1:8888
[INFO] [transport_selection.c:213] test_tcp(): TCP client connected
[INFO] [transport_selection.c:235] test_tcp(): Message sent via TCP
[INFO] [transport_selection.c:246] test_tcp(): Test 2 passed
[INFO] [transport_selection.c:260] test_udp():
Test 3: UDP transport（限制演示：客户端连接必然失败）

[INFO] [transport_selection.c:281] test_udp(): UDP client connect failed as expected (UDP 不支持 server 模式握手，框架限制)
[INFO] [transport_selection.c:286] test_udp(): Test 3 passed (UDP limitation demonstrated)
[INFO] [transport_selection.c:303] main():
All transport tests completed successfully!
```

> 注：
> - 实际输出中每条日志还包含时间戳与线程 ID 前缀（`[时间] [INFO] [线程ID] ...`），以上为便于阅读省略；`LOG_INFO("\n...")` 中的 `\n` 会使日志内容换行显示。
> - 服务器不回送应答，客户端注册的消息处理回调（"Message received"）不会被触发。

## 注意事项

- 测试1 与测试2 创建真实服务器完成收发；**测试3 的 UDP 连接失败即符合预期**（框架限制演示）
- 测试3 因连接等待接收超时，运行约 6 秒；全部测试运行约 12 秒
- 三个测试全部通过后退出码为 0

## 相关 API

- `ssn_server_create` - 创建 IPC 服务器
- `ssn_server_start` - 启动 IPC 服务器
- `ssn_server_destroy` - 销毁 IPC 服务器
- `ssn_client_create` - 创建 IPC 客户端
- `ssn_client_set_on_message` - 设置消息处理回调
- `ssn_client_connect` - 连接到服务器
- `ssn_client_message` - 发送消息
- `ssn_client_close` - 关闭 IPC 客户端
- `pthread_create` / `pthread_join` - 创建/等待服务端事件循环线程（POSIX 线程库）
