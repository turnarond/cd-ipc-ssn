# 02_error_handling 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的错误处理功能。它演示了连接失败、RPC 调用失败、无服务器时连接失败以及连接重试等错误场景的处理方式。

## 功能特性

- 连接不存在的服务器（连接失败处理）
- RPC 调用服务器不响应的方法（超时失败处理）
- 服务器未启动时的连接失败处理
- 连接失败后的多次重试与错误恢复
- 通过返回值与回调（`hdr == NULL` 表示失败）判断错误

## 代码结构

```
02_error_handling/
├── error_handling.c  # 错误处理示例代码
├── Makefile          # 构建脚本
└── README.md         # 本说明文档
```

## 核心代码解析

### 测试1：连接不存在的服务器（test_connection_error）

- 创建客户端，以 2 秒超时连接 `unix:///tmp/non_existent_server`
- 连接必然失败，`ssn_client_connect` 返回失败即视为预期错误

### 测试2：RPC 调用不存在的方法（test_rpc_error）

- 创建服务器（监听 `unix:///tmp/error_server`）并注册 `/non_existent_method`，该方法的 handler 不回复任何响应（`no_reply_handler`）
- 启动服务端事件循环线程 `server_poll_thread` 驱动 `ssn_server_poll`（连接握手与请求分发需要服务端 poll 驱动）
- 客户端以 200 毫秒超时调用该方法；`ssn_client_call` 发送成功即返回 0，调用失败必须通过回调判断——回调收到 `hdr == NULL` 表示服务器未响应/超时，即预期失败成立（`expect_fail_cb`）

### 测试3：服务器未启动时连接失败（test_connection_without_server）

- 创建服务器对象但不调用 `ssn_server_start`
- 客户端以 2 秒超时连接，连接失败即视为预期错误

### 测试4：错误恢复与重试（test_error_recovery）

- 以 1 秒超时连续 3 次尝试连接不存在的服务器，每次失败后等待 500 毫秒重试
- 3 次全部失败后判定错误恢复流程符合预期

## 运行示例

### 构建示例

```bash
cd examples/advanced/02_error_handling
make clean && make
```

> 二进制已内置 rpath，构建完成后可直接运行，无需设置 `LD_LIBRARY_PATH`。

### 运行示例

```bash
make run
```

或者直接运行：

```bash
./error_handling
```

## 预期输出

```
[INFO] [error_handling.c:302] main(): Error handling example started
[INFO] [error_handling.c:25] test_connection_error(): Test 1: Connection to non-existent server
[INFO] [error_handling.c:47] test_connection_error(): Test 1 passed (expected error)
[INFO] [error_handling.c:107] test_rpc_error():
Test 2: RPC call to non-existent method

[INFO] [error_handling.c:196] test_rpc_error(): Test 2 passed (expected error)
[INFO] [error_handling.c:211] test_connection_without_server():
Test 3: Connection failure without server

[INFO] [error_handling.c:242] test_connection_without_server(): Test 3 passed (expected error)
[INFO] [error_handling.c:255] test_error_recovery():
Test 4: Error recovery

[INFO] [error_handling.c:275] test_error_recovery(): Connection attempt 1/3
[INFO] [error_handling.c:280] test_error_recovery(): Connection attempt 1 failed, retrying...
[INFO] [error_handling.c:275] test_error_recovery(): Connection attempt 2/3
[INFO] [error_handling.c:280] test_error_recovery(): Connection attempt 2 failed, retrying...
[INFO] [error_handling.c:275] test_error_recovery(): Connection attempt 3/3
[INFO] [error_handling.c:280] test_error_recovery(): Connection attempt 3 failed, retrying...
[INFO] [error_handling.c:290] test_error_recovery(): Test 4 passed (error recovery handled)
[INFO] [error_handling.c:311] main():
All error handling tests completed successfully!
```

> 注：
> - 实际输出中每条日志还包含时间戳与线程 ID 前缀（`[时间] [INFO] [线程ID] ...`），以上为便于阅读省略；`LOG_INFO("\n...")` 中的 `\n` 会使日志内容换行显示。
> - 连接失败时库内部（`ssn_client.c` / `ssn_error.c`）可能输出 ERROR 级错误日志，属预期现象。

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 示例会故意触发各种失败场景，**连接失败即符合预期**，全部测试通过后退出码为 0
- 测试2 通过「服务器收到请求但不回复」使客户端回调收到 `hdr == NULL`，从而判定 RPC 调用失败
- 测试4 共尝试 3 次连接（每次超时 1 秒、间隔 500 毫秒），连接失败时快速返回，运行约 2~5 秒
- 全部测试运行约 5~15 秒（取决于连接失败返回速度，测试2 的 RPC 轮询与测试4 的 3 次重试为主要耗时项）

## 相关 API

- `ssn_client_create` - 创建 IPC 客户端
- `ssn_client_connect` - 连接到服务器
- `ssn_client_call` - 调用 RPC 方法
- `ssn_client_poll` - 轮询客户端事件
- `ssn_client_close` - 关闭 IPC 客户端
- `ssn_server_create` - 创建 IPC 服务器
- `ssn_server_start` - 启动 IPC 服务器
- `ssn_server_add_method` - 添加 RPC 方法
- `ssn_server_destroy` - 销毁 IPC 服务器
- `pthread_create` / `pthread_join` - 创建/等待服务端事件循环线程（POSIX 线程库）
