# 03_timeout 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的超时管理功能。它演示了连接超时、RPC 调用超时、消息发送行为以及空闲超时（idle timeout）等场景。

## 功能特性

- 连接不存在的服务器（连接超时处理）
- RPC 调用服务器不响应的方法（RPC 调用超时处理）
- 消息发送成功即返回（不触发发送超时）
- 服务器空闲超时（idle timeout）断开连接

## 代码结构

```
03_timeout/
├── timeout.c  # 超时管理示例代码
├── Makefile   # 构建脚本
└── README.md  # 本说明文档
```

## 核心代码解析

### 测试1：连接超时（test_connection_timeout）

- 创建客户端，以 1 秒超时连接 `unix:///tmp/non_existent_server`
- 连接必然超时失败，`ssn_client_connect` 返回失败即视为预期超时

### 测试2：RPC 调用超时（test_rpc_timeout）

- 创建服务器（监听 `unix:///tmp/timeout_server`）并注册 `/test` 方法，handler 不回复任何响应（`no_reply_handler`）
- 启动服务端事件循环线程 `server_poll_thread` 驱动 `ssn_server_poll`
- 客户端以 200 毫秒超时调用 `/test`；`ssn_client_call` 发送成功即返回 0，超时失败通过回调判断——回调收到 `hdr == NULL` 即预期超时成立（`expect_fail_cb`），随后 `ssn_client_poll` 驱动回调（1 秒 > 200 毫秒超时）

### 测试3：消息发送（test_message_timeout）

- 创建不处理消息的服务器，客户端向 `/large_message` 发送 4 KiB 消息
- `ssn_client_message` 发送成功即返回（>= 0 不触发发送超时），因此该测试不产生超时错误；若返回成功则输出 WARN 提示并继续

### 测试4：空闲超时（test_idle_timeout）

- 创建空闲超时为 5 秒的服务器（`server_options_t.idle_timeout_sec = 5`）
- 客户端连接后休眠 7 秒（超过空闲超时），再尝试发送消息：发送失败说明空闲超时已断开连接（`Test 4 passed (idle timeout occurred)`），否则输出 `Test 4: Connection still active`

## 运行示例

### 构建示例

```bash
cd examples/advanced/03_timeout
make clean && make
```

> 二进制已内置 rpath，构建完成后可直接运行，无需设置 `LD_LIBRARY_PATH`。

### 运行示例

```bash
make run
```

或者直接运行：

```bash
./timeout
```

## 预期输出

```
[INFO] [timeout.c:398] main(): Timeout example started
[INFO] [timeout.c:25] test_connection_timeout(): Test 1: Connection timeout
[INFO] [timeout.c:47] test_connection_timeout(): Test 1 passed (expected timeout)
[INFO] [timeout.c:107] test_rpc_timeout():
Test 2: RPC call timeout

[INFO] [timeout.c:196] test_rpc_timeout(): Test 2 passed (expected timeout)
[INFO] [timeout.c:211] test_message_timeout():
Test 3: Message send timeout

[WARN] [timeout.c:285] test_message_timeout(): Message send succeeded, but expected timeout
[INFO] [timeout.c:289] test_message_timeout(): Test 3 completed
[INFO] [timeout.c:304] test_idle_timeout():
Test 4: Idle timeout

[INFO] [timeout.c:362] test_idle_timeout(): Connected to server, waiting for idle timeout...
[INFO] [timeout.c:380] test_idle_timeout(): Test 4 passed (idle timeout occurred)
[INFO] [timeout.c:407] main():
All timeout tests completed successfully!
```

> 注：
> - 实际输出中每条日志还包含时间戳与线程 ID 前缀（`[时间] [INFO] [线程ID] ...`），以上为便于阅读省略；`LOG_INFO("\n...")` 中的 `\n` 会使日志内容换行显示。
> - 测试3 中若消息发送成功则输出 WARN 提示（发送不触发超时属预期行为）；测试4 若连接未被空闲超时断开，则输出 `Test 4: Connection still active`。
> - 连接失败/超时时库内部（`ssn_client.c` / `ssn_error.c`）可能输出 ERROR 级错误日志，属预期现象。

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 示例会故意触发超时场景，**超时/失败即符合预期**，全部测试通过后退出码为 0
- 测试2 通过「服务器收到请求但不回复」使客户端回调收到 `hdr == NULL`，从而判定 RPC 调用超时
- 测试4 运行约 7 秒（连接后休眠 7 秒等待空闲超时）；全部测试运行约 10~15 秒（连接不存在服务器的失败会快速返回，不等待完整超时）

## 相关 API

- `ssn_client_create` - 创建 IPC 客户端
- `ssn_client_connect` - 连接到服务器
- `ssn_client_call` - 调用 RPC 方法
- `ssn_client_message` - 发送消息
- `ssn_client_poll` - 轮询客户端事件
- `ssn_client_close` - 关闭 IPC 客户端
- `ssn_server_create` / `ssn_server_create_with_options` - 创建 IPC 服务器
- `ssn_server_start` - 启动 IPC 服务器
- `ssn_server_add_method` - 添加 RPC 方法
- `ssn_server_destroy` - 销毁 IPC 服务器
- `pthread_create` / `pthread_join` - 创建/等待服务端事件循环线程（POSIX 线程库）
