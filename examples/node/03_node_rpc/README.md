# 03_node_rpc 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的节点 RPC 功能。它演示了如何创建服务端节点与客户端节点，在服务端节点上注册 RPC 方法，并由客户端节点发起远程调用。

## 功能特性

- 创建服务端节点与客户端节点
- 在服务端节点上注册 RPC 方法（`/math/add`、`/math/subtract`）
- 客户端节点发起 RPC 调用
- 处理 RPC 响应
- 后台服务端轮询线程驱动（`server_poll_thread`）

## 代码结构

```
03_node_rpc/
├── node_rpc.c  # 节点 RPC 示例代码
├── Makefile    # 构建脚本
└── README.md   # 本说明文档
```

## 核心代码解析

1. **服务端节点配置与创建**
   - `node_type` = "server"、`node_name` = "ServerNode"
   - `listen_address` = "127.0.0.1"（裸主机名，不含协议前缀）+ `listen_port` = 8891
   - `capabilities` = SERVER | RPC

2. **服务端节点启动与 RPC 方法注册**
   - 使用 `ssn_node_start` 启动（内部创建 TCP 服务器监听 8891 端口）
   - 使用 `ssn_node_add_rpc_method` 注册 `/math/add` 与 `/math/subtract` 两个方法
   - 启动后台线程 `server_poll_thread` 循环调用 `ssn_node_poll(server_node, 100)`——**服务器节点是 poll 驱动的，需要后台轮询才能接受连接和处理请求**

3. **客户端节点配置与创建**
   - `node_type` = "client"、`node_name` = "ClientNode"
   - `capabilities` = CLIENT | RPC（无监听地址）

4. **RPC 调用**
   - 使用 `ssn_node_rpc_call` 调用服务端节点（对端地址 `tcp://127.0.0.1:8891`）：
     - `/math/add`：参数 `{"a": 5, "b": 3}`，结果 8
     - `/math/subtract`：参数 `{"a": 10, "b": 4}`，结果 6
   - 每次调用后在 5 秒窗口内轮询两个节点等待响应完成（`g_rpc_complete` 标志）

5. **响应处理**
   - 注册 RPC 响应处理回调 `rpc_reply_handler`，收到响应时打印结果

6. **清理**
   - 停止后台轮询线程，停止并销毁两个节点

## 运行示例

### 构建示例

```bash
cd examples/node/03_node_rpc
make clean && make
```

> 二进制已内置 rpath，构建完成后可直接运行，无需设置 `LD_LIBRARY_PATH`。

### 运行示例

```bash
make run
```

或者直接运行：

```bash
./node_rpc
```

## 预期输出

```
[INFO] [node_rpc.c:335] main(): Node RPC example started
[INFO] [node_rpc.c:149] test_node_rpc(): Test: Node-to-node RPC
[INFO] [node_rpc.c:166] test_node_rpc(): Server node created: id=<node_id>, type=server, name=ServerNode
[INFO] [node_rpc.c:176] test_node_rpc(): Server node started
[INFO] [node_rpc.c:187] test_node_rpc(): RPC method /math/add registered
[INFO] [node_rpc.c:194] test_node_rpc(): RPC method /math/subtract registered
[INFO] [node_rpc.c:219] test_node_rpc(): Client node created: id=<node_id>, type=client, name=ClientNode
[INFO] [node_rpc.c:230] test_node_rpc(): Client node started
[INFO] [node_rpc.c:233] test_node_rpc(): Client calling /math/add with {"a": 5, "b": 3}
[INFO] [node_rpc.c:53] add_handler(): Server received RPC call: /math/add with {"a": 5, "b": 3}
[INFO] [node_rpc.c:75] add_handler(): Server sent RPC response: 8
[INFO] [node_rpc.c:135] rpc_reply_handler(): Client received RPC response: 8
[INFO] [node_rpc.c:274] test_node_rpc(): Client calling /math/subtract with {"a": 10, "b": 4}
[INFO] [node_rpc.c:94] subtract_handler(): Server received RPC call: /math/subtract with {"a": 10, "b": 4}
[INFO] [node_rpc.c:116] subtract_handler(): Server sent RPC response: 6
[INFO] [node_rpc.c:135] rpc_reply_handler(): Client received RPC response: 6
[INFO] [node_rpc.c:326] test_node_rpc(): RPC test completed successfully
[INFO] [node_rpc.c:341] main():
All RPC tests completed successfully!
```

> 注：
> - `<node_id>` 由 `节点类型_节点名称_主机名_时间戳` 生成（见 `ssn_node.c` 的 `generate_node_id`），每次运行不同，此处为占位符。
> - 实际输出中每条日志还包含时间戳与线程 ID 前缀（`[时间] [INFO] [线程ID] ...`），以上为便于阅读省略；`LOG_INFO("\n...")` 中的 `\n` 会使日志内容换行显示。
> - 库内部日志（如 `ssn_node.c` 中节点创建/启动/停止/销毁、传输工厂初始化等 INFO 日志）也会出现在实际输出中，其文件路径与行号以实际编译路径为准，此处未逐一列出。

## 注意事项

- 本示例使用 TCP 作为传输协议
- 服务端节点监听 `127.0.0.1:8891`（`listen_address` 只接受裸主机名）
- 客户端节点通过完整对端地址 `tcp://127.0.0.1:8891` 发起调用
- **服务器节点为 poll 驱动**：示例启动后台线程轮询 `ssn_node_poll`，否则连接握手与请求分发不会发生
- 示例约运行 2~4 秒（两次 RPC 调用完成后即退出），全部通过后退出码为 0

## 相关 API

- `ssn_node_create` - 创建节点
- `ssn_node_start` - 启动节点
- `ssn_node_stop` - 停止节点
- `ssn_node_destroy` - 销毁节点
- `ssn_node_add_rpc_method` - 向节点添加 RPC 方法
- `ssn_node_rpc_call` - 发起 RPC 调用
- `ssn_node_poll` - 轮询节点事件
- `pthread_create` / `pthread_join` - 创建/等待后台轮询线程（POSIX 线程库）
