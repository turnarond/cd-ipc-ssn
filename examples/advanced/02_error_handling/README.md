# 02_error_handling 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的错误处理功能。它演示了如何处理各种错误情况，包括连接失败、超时、消息发送失败等。

## 功能特性

- 处理连接失败错误
- 处理超时错误
- 处理消息发送失败错误
- 处理服务器关闭错误
- 错误恢复和重试机制

## 代码结构

```
02_error_handling/
├── error_handling.c  # 错误处理示例代码
├── Makefile          # 构建脚本
└── README.md         # 本说明文档
```

## 核心代码解析

### 错误处理示例 (error_handling.c)

1. **错误处理策略**
   - 定义错误处理回调函数 `error_handler`
   - 注册错误处理回调函数

2. **连接错误处理**
   - 尝试连接到不存在的服务器地址
   - 处理连接失败错误

3. **超时错误处理**
   - 设置短超时时间
   - 处理超时错误

4. **消息发送错误处理**
   - 尝试向已关闭的连接发送消息
   - 处理消息发送失败错误

5. **错误恢复**
   - 实现简单的错误恢复和重试机制

## 运行示例

### 构建示例

```bash
cd examples/advanced/02_error_handling
make
```

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
[INFO] [error_handling.c:118] main(): Starting error handling example...
[INFO] [error_handling.c:128] main(): Registering error handler
[INFO] [error_handling.c:133] main(): Testing connection to non-existent server...
[ERROR] [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/cd_ipc_error.c:110] ipc_handle_error(): [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/cd_ipc_client.c:614] ipc_client_connect: Network read failed - recv failed after multiple retries
[INFO] [error_handling.c:46] error_handler(): Error occurred: Network read failed - recv failed after multiple retries
[INFO] [error_handling.c:143] main(): Connection failed as expected
[INFO] [error_handling.c:148] main(): Testing timeout error...
[INFO] [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/transports/ssn_transport_factory.c:102] ssn_transport_factory_init(): Transport factory initialized successfully
[INFO] [error_handling.c:158] main(): Client created successfully
[INFO] [error_handling.c:168] main(): Setting short timeout
[INFO] [error_handling.c:173] main(): Testing timeout with RPC call...
[INFO] [error_handling.c:178] main(): Waiting for timeout...
[INFO] [error_handling.c:183] main(): Timeout test completed
[INFO] [error_handling.c:188] main(): Testing message sending error...
[INFO] [error_handling.c:198] main(): Closing client
[INFO] [error_handling.c:203] main(): Trying to send message after client close...
[ERROR] [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/cd_ipc_error.c:110] ipc_handle_error(): [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/cd_ipc_client.c:903] ipc_client_send: invalid client handle.
[INFO] [error_handling.c:46] error_handler(): Error occurred: invalid client handle.
[INFO] [error_handling.c:213] main(): Message sending failed as expected
[INFO] [error_handling.c:218] main(): Error handling example completed
```

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 示例会故意尝试连接不存在的服务器，以测试错误处理
- 示例会设置短超时时间，以测试超时错误
- 示例会尝试向已关闭的连接发送消息，以测试消息发送失败错误

## 相关 API

- `ipc_client_create` - 创建 IPC 客户端
- `ipc_client_connect` - 连接到服务器
- `ipc_client_call` - 调用 RPC 方法
- `ipc_client_send` - 发送消息
- `ipc_client_set_on_error` - 设置错误处理回调
- `ipc_client_close` - 关闭 IPC 客户端
