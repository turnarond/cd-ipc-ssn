# 04_transport_selection 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的传输协议选择功能。它演示了如何选择不同的传输协议（Unix Socket、TCP、UDP），以及如何配置和使用这些协议。

## 功能特性

- 选择 Unix Socket 传输协议
- 选择 TCP 传输协议
- 选择 UDP 传输协议
- 配置传输协议参数
- 测试不同传输协议的性能

## 代码结构

```
04_transport_selection/
├── transport_selection.c  # 传输协议选择示例代码
├── Makefile               # 构建脚本
└── README.md              # 本说明文档
```

## 核心代码解析

### 传输协议选择示例 (transport_selection.c)

1. **Unix Socket 协议**
   - 使用 `unix://` 前缀指定 Unix Socket 协议
   - 配置 Unix Socket 路径
   - 测试 Unix Socket 连接

2. **TCP 协议**
   - 使用 `tcp://` 前缀指定 TCP 协议
   - 配置 TCP 服务器地址和端口
   - 测试 TCP 连接

3. **UDP 协议**
   - 使用 `udp://` 前缀指定 UDP 协议
   - 配置 UDP 服务器地址和端口
   - 测试 UDP 连接

4. **传输协议参数配置**
   - 配置发送超时
   - 配置接收超时
   - 配置连接超时

## 运行示例

### 构建示例

```bash
cd examples/advanced/04_transport_selection
make
```

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
[INFO] [transport_selection.c:148] main(): Starting transport selection example...
[INFO] [transport_selection.c:158] main(): Testing Unix Socket transport...
[INFO] [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/transports/ssn_transport_factory.c:102] ssn_transport_factory_init(): Transport factory initialized successfully
[INFO] [transport_selection.c:168] main(): Unix Socket client created successfully
[INFO] [transport_selection.c:178] main(): Setting Unix Socket parameters
[INFO] [transport_selection.c:183] main(): Testing Unix Socket connection...
[INFO] [transport_selection.c:188] main(): Unix Socket test completed
[INFO] [transport_selection.c:193] main(): Testing TCP transport...
[INFO] [transport_selection.c:203] main(): TCP client created successfully
[INFO] [transport_selection.c:213] main(): Setting TCP parameters
[INFO] [transport_selection.c:218] main(): Testing TCP connection...
[INFO] [transport_selection.c:223] main(): TCP test completed
[INFO] [transport_selection.c:228] main(): Testing UDP transport...
[INFO] [transport_selection.c:238] main(): UDP client created successfully
[INFO] [transport_selection.c:248] main(): Setting UDP parameters
[INFO] [transport_selection.c:253] main(): Testing UDP connection...
[INFO] [transport_selection.c:258] main(): UDP test completed
[INFO] [transport_selection.c:263] main(): Transport selection example completed
```

## 注意事项

- 本示例展示了如何选择不同的传输协议
- 示例会尝试连接到不同的服务器地址，以测试不同的传输协议
- 示例会配置不同的传输协议参数，以展示如何优化传输性能

## 相关 API

- `ssn_client_create` - 创建 IPC 客户端
- `ssn_client_connect` - 连接到服务器
- `ssn_client_send_timeout` - 设置发送超时
- `ssn_client_close` - 关闭 IPC 客户端
