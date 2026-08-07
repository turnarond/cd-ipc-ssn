# 03_pubsub 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的发布/订阅（Publish/Subscribe）功能。它演示了如何创建一个发布者服务器，以及如何创建多个订阅者客户端订阅不同的主题。

## 功能特性

- 创建发布者服务器
- 创建订阅者客户端
- 订阅者订阅不同主题（订阅者1 订阅 `/news`；订阅者2 订阅 `/weather` 与 `/sports`）
- 发布者向不同主题发布消息
- 订阅者接收并打印订阅的消息

## 代码结构

```
03_pubsub/
├── publisher.c   # 发布者代码
├── subscriber.c  # 订阅者1代码
├── subscriber2.c # 订阅者2代码
├── Makefile      # 构建脚本
└── README.md     # 本说明文档
```

## 核心代码解析

### 发布者 (publisher.c)

1. **发布者创建与启动**
   - 使用 `ssn_server_create_with_options` 创建服务器
   - 服务器地址为 `unix:///tmp/pubsub_server`
   - 使用 `ssn_server_start` 启动服务器并开始监听

2. **等待订阅者连接**
   - 启动后先等待 5 秒（事件循环 `ssn_server_poll` + `sleep(1)` 共 5 次），等待订阅者建立连接

3. **消息发布**
   - 使用 `ssn_server_publish` 依次发布 4 条消息，每条间隔 2 秒：
     - `/news`：Breaking news! Server is online
     - `/weather`：Today's weather is sunny
     - `/news`：Another breaking news story
     - `/sports`：Sports update: Team won the game
   - 发布完成后等待 5 秒投递完成，再销毁服务器

4. **连接处理**
   - 注册连接处理回调函数 `connect_handler`
   - 当订阅者连接或断开时，打印订阅者状态

### 订阅者1 (subscriber.c)

1. **订阅者创建与连接**
   - 使用 `ssn_client_create` 创建客户端
   - 使用 `ssn_client_connect` 连接到服务器地址 `unix:///tmp/pubsub_server`（连接超时 5 秒）

2. **主题订阅**
   - 使用 `ssn_client_subscribe` 订阅 `/news` 主题

3. **消息处理**
   - 轮询 `ssn_client_poll` 接收消息，运行 15 秒后取消订阅并退出

### 订阅者2 (subscriber2.c)

1. **订阅者创建与连接**
   - 使用 `ssn_client_create` 创建客户端
   - 使用 `ssn_client_connect` 连接到服务器地址 `unix:///tmp/pubsub_server`（连接超时 5 秒）

2. **主题订阅**
   - 使用 `ssn_client_subscribe` 订阅 `/weather` 与 `/sports` 两个主题

3. **消息处理**
   - 轮询 `ssn_client_poll` 接收消息，运行 15 秒后依次取消订阅并退出

## 运行示例

### 构建示例

```bash
cd examples/basic/03_pubsub
make clean && make
```

> 二进制已内置 rpath，构建完成后可直接运行，无需设置 `LD_LIBRARY_PATH`。

### 运行示例

```bash
make run
```

或者分别在终端运行发布者和订阅者：

```bash
# 运行发布者
./publisher

# 运行订阅者1（在另一个终端）
./subscriber

# 运行订阅者2（在另一个终端）
./subscriber2
```

## 预期输出

### 发布者输出

```
[INFO] [publisher.c:71] main(): Starting publisher...
[INFO] [publisher.c:88] main(): Publisher created successfully
[INFO] [publisher.c:100] main(): Publisher started on unix:///tmp/pubsub_server
[INFO] [publisher.c:103] main(): Waiting for subscribers to connect...
[INFO] [publisher.c:60] connect_handler(): Subscriber connected: id=0
[INFO] [publisher.c:60] connect_handler(): Subscriber connected: id=1
[INFO] [publisher.c:112] main():
Publishing messages...

[INFO] [publisher.c:42] publish_message(): Publishing message to /news: Breaking news! Server is online
[INFO] [publisher.c:42] publish_message(): Publishing message to /weather: Today's weather is sunny
[INFO] [publisher.c:42] publish_message(): Publishing message to /news: Another breaking news story
[INFO] [publisher.c:42] publish_message(): Publishing message to /sports: Sports update: Team won the game
[INFO] [publisher.c:135] main():
Waiting for messages to be delivered...

[INFO] [publisher.c:62] connect_handler(): Subscriber disconnected: id=0
[INFO] [publisher.c:62] connect_handler(): Subscriber disconnected: id=1
[INFO] [publisher.c:144] main(): Stopping publisher...
[INFO] [publisher.c:149] main(): Publisher destroyed
```

### 订阅者1输出

```
[INFO] [subscriber.c:42] main(): Starting subscriber...
[INFO] [subscriber.c:54] main(): Subscriber created successfully
[INFO] [subscriber.c:69] main(): Connected to publisher: unix:///tmp/pubsub_server
[INFO] [subscriber.c:83] main(): Subscribed to topic: /news
[INFO] [subscriber.c:86] main(): Running for 15 seconds...
[INFO] [subscriber.c:33] message_handler(): Received message on /news: Breaking news! Server is online
[INFO] [subscriber.c:33] message_handler(): Received message on /news: Another breaking news story
[INFO] [subscriber.c:99] main(): Unsubscribed from topic: /news
[INFO] [subscriber.c:108] main(): Subscriber closed
```

### 订阅者2输出

```
[INFO] [subscriber2.c:42] main(): Starting subscriber2...
[INFO] [subscriber2.c:54] main(): Subscriber2 created successfully
[INFO] [subscriber2.c:69] main(): Connected to publisher: unix:///tmp/pubsub_server
[INFO] [subscriber2.c:83] main(): Subscribed to topic: /weather
[INFO] [subscriber2.c:94] main(): Subscribed to topic: /sports
[INFO] [subscriber2.c:98] main(): Running for 15 seconds...
[INFO] [subscriber2.c:33] message_handler(): Received message on /weather: Today's weather is sunny
[INFO] [subscriber2.c:33] message_handler(): Received message on /sports: Sports update: Team won the game
[INFO] [subscriber2.c:111] main(): Unsubscribed from topic: /weather
[INFO] [subscriber2.c:117] main(): Unsubscribed from topic: /sports
[INFO] [subscriber2.c:126] main(): Subscriber2 closed
```

> 注：实际输出中每条日志还包含时间戳与线程 ID 前缀（`[时间] [INFO] [线程ID] ...`），以上为便于阅读省略；`LOG_INFO("\n...")` 中的 `\n` 会使日志内容换行显示；断开日志出现的具体位置与各进程的运行时机相关。

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 服务器地址为 `unix:///tmp/pubsub_server`
- 发布者先等待 5 秒订阅者连接，再依次发布 4 条消息（每条间隔 2 秒），最后等待 5 秒投递完成后停止
- 订阅者运行 15 秒后取消订阅并退出
- 订阅者1 只订阅 `/news`，因此只能收到 2 条新闻消息；订阅者2 订阅 `/weather` 与 `/sports`

## 相关 API

- `ssn_server_create_with_options` - 创建 IPC 服务器
- `ssn_server_start` - 启动 IPC 服务器
- `ssn_server_publish` - 发布消息到主题
- `ssn_server_set_connect_handler` - 设置连接处理回调
- `ssn_server_poll` - 轮询服务器事件
- `ssn_server_destroy` - 销毁 IPC 服务器
- `ssn_client_create` - 创建 IPC 客户端
- `ssn_client_connect` - 连接到服务器
- `ssn_client_subscribe` - 订阅主题
- `ssn_client_unsubscribe` - 取消订阅主题
- `ssn_client_set_on_message` - 设置消息处理回调
- `ssn_client_poll` - 轮询客户端事件
- `ssn_client_disconnect` - 断开与服务器的连接
- `ssn_client_close` - 关闭 IPC 客户端
