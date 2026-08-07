# 04_node_pubsub 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的节点发布/订阅功能。它演示了如何创建发布者节点与两个订阅者节点，并通过发布/订阅模式分发主题消息。

## 功能特性

- 创建发布者节点与订阅者节点
- 发布者节点监听连接（TCP）
- 订阅者节点订阅主题（订阅者1 订阅 `/news`；订阅者2 订阅 `/weather`）
- 发布者节点发布消息
- 订阅者节点接收并打印订阅的消息
- 后台服务端轮询线程驱动（`server_poll_thread`）

## 代码结构

```
04_node_pubsub/
├── node_pubsub.c  # 节点发布/订阅示例代码
├── Makefile       # 构建脚本
└── README.md      # 本说明文档
```

## 核心代码解析

1. **发布者节点配置与创建**
   - `node_type` = "publisher"、`node_name` = "Publisher"
   - `listen_address` = "127.0.0.1"（裸主机名，不含协议前缀）+ `listen_port` = 8892
   - `capabilities` = SERVER | PUBSUB
   - 使用 `ssn_node_start` 启动（内部创建 TCP 服务器监听 8892 端口）

2. **后台轮询线程**
   - 启动后台线程 `server_poll_thread` 循环调用 `ssn_node_poll(publisher_node, 100)`——**服务器节点是 poll 驱动的，需要后台轮询才能接受连接和处理订阅握手**

3. **订阅者节点创建与订阅**
   - 订阅者1：`node_type` = "subscriber"、`node_name` = "Subscriber1"，能力 CLIENT | PUBSUB，使用 `ssn_node_subscribe` 订阅 `/news`
   - 订阅者2：`node_type` = "subscriber"、`node_name` = "Subscriber2"，能力 CLIENT | PUBSUB，使用 `ssn_node_subscribe` 订阅 `/weather`
   - 消息回调在订阅时传入（订阅即注册 per-URL 消息处理），无需另行设置
   - 订阅建立后等待 2 秒

4. **消息发布**
   - 使用 `ssn_node_publish` 发布 2 条消息：
     - `/news`：Breaking news! Server is online（31 字节）
     - `/weather`：Today's weather is sunny（24 字节），两条之间间隔 1 秒

5. **消息接收**
   - 在 5 秒窗口内轮询三个节点，等待两个订阅者各自收到消息（`g_news_messages_received` / `g_weather_messages_received` 计数）
   - 收到后使用 `ssn_node_unsubscribe` 取消订阅

6. **清理**
   - 停止后台轮询线程，停止并销毁三个节点

## 运行示例

### 构建示例

```bash
cd examples/node/04_node_pubsub
make clean && make
```

> 二进制已内置 rpath，构建完成后可直接运行，无需设置 `LD_LIBRARY_PATH`。

### 运行示例

```bash
make run
```

或者直接运行：

```bash
./node_pubsub
```

## 预期输出

```
[INFO] [node_pubsub.c:321] main(): Node publish/subscribe example started
[INFO] [node_pubsub.c:83] test_node_pubsub(): Test: Node publish/subscribe
[INFO] [node_pubsub.c:100] test_node_pubsub(): Publisher node created: id=<node_id>, type=publisher, name=Publisher
[INFO] [node_pubsub.c:110] test_node_pubsub(): Publisher node started
[INFO] [node_pubsub.c:137] test_node_pubsub(): Subscriber1 node created: id=<node_id>, type=subscriber, name=Subscriber1
[INFO] [node_pubsub.c:150] test_node_pubsub(): Subscriber1 node started
[INFO] [node_pubsub.c:169] test_node_pubsub(): Subscriber2 node created: id=<node_id>, type=subscriber, name=Subscriber2
[INFO] [node_pubsub.c:183] test_node_pubsub(): Subscriber2 node started
[INFO] [node_pubsub.c:208] test_node_pubsub(): Subscriber1 subscribed to /news
[INFO] [node_pubsub.c:220] test_node_pubsub(): Subscriber2 subscribed to /weather
[INFO] [node_pubsub.c:226] test_node_pubsub(): Publisher publishing to /news: Breaking news! Server is online
[INFO] [node_pubsub.c:246] test_node_pubsub(): Publisher publishing to /weather: Today's weather is sunny
[INFO] [node_pubsub.c:263] test_node_pubsub(): Waiting for messages to be delivered...
[INFO] [node_pubsub.c:52] subscriber1_message_handler(): Subscriber1 received message on /news: Breaking news! Server is online
[INFO] [node_pubsub.c:72] subscriber2_message_handler(): Subscriber2 received message on /weather: Today's weather is sunny
[INFO] [node_pubsub.c:312] test_node_pubsub(): Publish/subscribe test completed successfully
[INFO] [node_pubsub.c:327] main():
All publish/subscribe tests completed successfully!
```

> 注：
> - `<node_id>` 由 `节点类型_节点名称_主机名_时间戳` 生成（见 `ssn_node.c` 的 `generate_node_id`），每次运行不同，此处为占位符。
> - 实际输出中每条日志还包含时间戳与线程 ID 前缀（`[时间] [INFO] [线程ID] ...`），以上为便于阅读省略；`LOG_INFO("\n...")` 中的 `\n` 会使日志内容换行显示。
> - 库内部日志（如 `ssn_node.c` 中节点创建/启动/停止/销毁、传输工厂初始化等 INFO 日志）也会出现在实际输出中，其文件路径与行号以实际编译路径为准，此处未逐一列出。

## 注意事项

- 本示例使用 TCP 作为传输协议
- 发布者节点监听 `127.0.0.1:8892`（`listen_address` 只接受裸主机名）
- 订阅者节点通过完整对端地址 `tcp://127.0.0.1:8892` 订阅
- **服务器节点为 poll 驱动**：示例启动后台线程轮询 `ssn_node_poll`，否则订阅握手与消息分发不会发生
- 订阅者1 只订阅 `/news`，订阅者2 只订阅 `/weather`，各只收到对应主题的消息
- 示例约运行 3~5 秒（订阅建立 2 秒 + 发布与投递确认后即退出），全部通过后退出码为 0

## 相关 API

- `ssn_node_create` - 创建节点
- `ssn_node_start` - 启动节点
- `ssn_node_stop` - 停止节点
- `ssn_node_destroy` - 销毁节点
- `ssn_node_subscribe` - 订阅主题
- `ssn_node_unsubscribe` - 取消订阅主题
- `ssn_node_publish` - 发布消息
- `ssn_node_poll` - 轮询节点事件
- `pthread_create` / `pthread_join` - 创建/等待后台轮询线程（POSIX 线程库）
