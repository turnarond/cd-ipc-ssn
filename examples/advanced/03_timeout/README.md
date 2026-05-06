# 03_timeout 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的超时管理功能。它演示了如何设置和处理各种超时情况，包括连接超时、RPC 调用超时等。

## 功能特性

- 设置连接超时
- 设置 RPC 调用超时
- 处理超时错误
- 超时回调处理

## 代码结构

```
03_timeout/
├── timeout.c  # 超时管理示例代码
├── Makefile   # 构建脚本
└── README.md  # 本说明文档
```

## 核心代码解析

### 超时管理示例 (timeout.c)

1. **超时设置**
   - 设置连接超时
   - 设置 RPC 调用超时

2. **连接超时测试**
   - 尝试连接到不存在的服务器
   - 测试连接超时处理

3. **RPC 超时测试**
   - 调用一个会超时的 RPC 方法
   - 测试 RPC 超时处理

4. **超时回调**
   - 注册超时回调函数
   - 处理超时事件

## 运行示例

### 构建示例

```bash
cd examples/advanced/03_timeout
make
```

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
[INFO] [timeout.c:118] main(): Starting timeout management example...
[INFO] [timeout.c:128] main(): Testing connection timeout...
[INFO] [timeout.c:138] main(): Setting connection timeout to 2 seconds
[INFO] [timeout.c:143] main(): Attempting to connect to non-existent server...
[ERROR] [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/cd_ipc_error.c:110] ipc_handle_error(): [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/cd_ipc_client.c:614] ipc_client_connect: Network read failed - recv failed after multiple retries
[INFO] [timeout.c:46] timeout_handler(): Timeout occurred: Network read failed - recv failed after multiple retries
[INFO] [timeout.c:153] main(): Connection timeout test completed
[INFO] [timeout.c:158] main(): Testing RPC call timeout...
[INFO] [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/transports/ssn_transport_factory.c:102] ssn_transport_factory_init(): Transport factory initialized successfully
[INFO] [timeout.c:168] main(): Client created successfully
[INFO] [timeout.c:178] main(): Setting RPC timeout to 1 second
[INFO] [timeout.c:183] main(): Making RPC call with short timeout...
[INFO] [timeout.c:188] main(): Waiting for RPC response...
[INFO] [timeout.c:46] timeout_handler(): Timeout occurred: RPC call timeout
[INFO] [timeout.c:193] main(): RPC timeout test completed
[INFO] [timeout.c:198] main(): Timeout management example completed
```

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 示例会故意尝试连接不存在的服务器，以测试连接超时
- 示例会设置短 RPC 超时时间，以测试 RPC 超时

## 相关 API

- `ipc_client_create` - 创建 IPC 客户端
- `ipc_client_connect` - 连接到服务器
- `ipc_client_call` - 调用 RPC 方法
- `ipc_client_set_on_timeout` - 设置超时处理回调
- `ipc_client_close` - 关闭 IPC 客户端
