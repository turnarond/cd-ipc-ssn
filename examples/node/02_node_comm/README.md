# 02_node_comm 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的节点间通信功能。它演示了如何创建服务端节点（Node A）与客户端节点（Node B），并通过节点抽象层在两者之间发送消息与回复。

## 功能特性

- 创建服务端节点与客户端节点
- 服务端节点监听连接（TCP）
- 客户端节点向服务端节点发送消息
- 服务端节点接收消息并回发回复
- 后台服务端轮询线程驱动（`server_poll_thread`）

## 代码结构

```
02_node_comm/
├── node_comm.c  # 节点间通信示例代码
├── Makefile     # 构建脚本
└── README.md    # 本说明文档
```

## 核心代码解析

1. **Node A（服务端节点）配置与创建**
   - `node_type` = "server"、`node_name` = "NodeA"
   - `listen_address` = "127.0.0.1"（裸主机名，不含协议前缀）+ `listen_port` = 8890
   - `capabilities` = SERVER | CLIENT | RPC | PUBSUB

2. **Node A 启动**
   - 使用 `ssn_node_start` 启动（内部创建 TCP 服务器监听 8890 端口）
   - 使用 `ssn_node_set_message_handler` 注册消息处理回调 `node_a_message_handler`
   - 启动后台线程 `server_poll_thread` 循环调用 `ssn_node_poll(node_a, 100)`——**服务器节点是 poll 驱动的，需要后台轮询才能接受连接和处理消息**

3. **Node B（客户端节点）配置与创建**
   - `node_type` = "client"、`node_name` = "NodeB"
   - `capabilities` = CLIENT | RPC | PUBSUB（无监听地址）
   - 使用 `ssn_node_start` 启动（客户端按需创建）

4. **消息发送与回复**
   - Node B 使用 `ssn_node_send_to_peer` 向 Node A（对端地址 `tcp://127.0.0.1:8890`）发送 "Hello from Node B!"（18 字节），消息路径 `/message`
   - Node A 的回调收到消息后，通过 `ssn_server_message` 直接回复 "Hello from Node A!"（18 字节），路径 `/reply`（注意：回调上下文中不能重入节点 API，因此直接经 server 发送）
   - Node B 使用 `ssn_node_set_client_message_handler` 注册回复处理回调 `node_b_message_handler`
   - 主流程在 10 秒窗口内轮询两个节点等待消息与回复完成

5. **清理**
   - 停止后台轮询线程，停止并销毁两个节点

## 运行示例

### 构建示例

```bash
cd examples/node/02_node_comm
make clean && make
```

> 二进制已内置 rpath，构建完成后可直接运行，无需设置 `LD_LIBRARY_PATH`。

### 运行示例

```bash
make run
```

或者直接运行：

```bash
./node_comm
```

## 预期输出

```
[INFO] [node_comm.c:249] main(): Node communication example started
[INFO] [node_comm.c:101] test_node_communication(): Test: Node-to-node communication
[INFO] [node_comm.c:119] test_node_communication(): Node A created: id=<node_id>, type=server, name=NodeA
[INFO] [node_comm.c:129] test_node_communication(): Node A started
[INFO] [node_comm.c:159] test_node_communication(): Node B created: id=<node_id>, type=client, name=NodeB
[INFO] [node_comm.c:172] test_node_communication(): Node B started
[INFO] [node_comm.c:185] test_node_communication(): Node B sending message to Node A: Hello from Node B!
[INFO] [node_comm.c:199] test_node_communication(): Waiting for communication to complete...
[INFO] [node_comm.c:53] node_a_message_handler(): Node A received message: Hello from Node B!
[INFO] [node_comm.c:72] node_a_message_handler(): Node A sent reply: Hello from Node A!
[INFO] [node_comm.c:90] node_b_message_handler(): Node B received reply: Hello from Node A!
[INFO] [node_comm.c:240] test_node_communication(): Communication test completed successfully
[INFO] [node_comm.c:255] main():
All communication tests completed successfully!
```

> 注：
> - `<node_id>` 由 `节点类型_节点名称_主机名_时间戳` 生成（见 `ssn_node.c` 的 `generate_node_id`），每次运行不同，此处为占位符。
> - 实际输出中每条日志还包含时间戳与线程 ID 前缀（`[时间] [INFO] [线程ID] ...`），以上为便于阅读省略；`LOG_INFO("\n...")` 中的 `\n` 会使日志内容换行显示。
> - 库内部日志（如 `ssn_node.c` 中节点创建/启动/停止/销毁、传输工厂初始化等 INFO 日志）也会出现在实际输出中，其文件路径与行号以实际编译路径为准，此处未逐一列出。

## 注意事项

- 本示例使用 TCP 作为传输协议
- Node A 监听 `127.0.0.1:8890`（`listen_address` 只接受裸主机名）
- Node B 通过完整对端地址 `tcp://127.0.0.1:8890` 发送消息
- **服务器节点为 poll 驱动**：示例启动后台线程轮询 `ssn_node_poll`，否则连接握手与消息处理不会发生
- 示例约运行 1~2 秒（等待消息与回复完成后即退出），全部通过后退出码为 0

## 相关 API

- `ssn_node_create` - 创建节点
- `ssn_node_start` - 启动节点
- `ssn_node_stop` - 停止节点
- `ssn_node_destroy` - 销毁节点
- `ssn_node_set_message_handler` - 设置节点消息处理回调
- `ssn_node_set_client_message_handler` - 设置客户端消息处理回调
- `ssn_node_send_to_peer` - 向对端节点发送消息
- `ssn_node_poll` - 轮询节点事件
- `pthread_create` / `pthread_join` - 创建/等待后台轮询线程（POSIX 线程库）
