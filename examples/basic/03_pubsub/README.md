# 03_pubsub 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的发布/订阅（Publish/Subscribe）功能。它演示了如何创建一个发布者服务器，以及如何创建多个订阅者客户端订阅不同的主题。

## 功能特性

- 创建发布者服务器
- 创建订阅者客户端
- 订阅者订阅不同的主题（/news, /weather, /sports）
- 发布者向不同主题发布消息
- 订阅者接收并处理订阅的消息

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
   - 设置服务器地址为 `unix:///tmp/pubsub_server`
   - 启动服务器并开始监听

2. **消息发布**
   - 向 `/news` 主题发布新闻消息
   - 向 `/weather` 主题发布天气消息
   - 向 `/sports` 主题发布体育消息

### 订阅者1 (subscriber.c)

1. **订阅者创建与连接**
   - 使用 `ssn_client_create` 创建客户端
   - 连接到服务器地址 `unix:///tmp/pubsub_server`

2. **主题订阅**
   - 订阅 `/news` 主题
   - 订阅 `/weather` 主题

3. **消息处理**
   - 注册消息处理回调函数 `message_handler`
   - 当接收到订阅的消息时，打印消息内容

### 订阅者2 (subscriber2.c)

1. **订阅者创建与连接**
   - 使用 `ssn_client_create` 创建客户端
   - 连接到服务器地址 `unix:///tmp/pubsub_server`

2. **主题订阅**
   - 订阅 `/sports` 主题

3. **消息处理**
   - 注册消息处理回调函数 `message_handler`
   - 当接收到订阅的消息时，打印消息内容

## 运行示例

### 构建示例

```bash
cd examples/basic/03_pubsub
make
```

### 运行示例

```bash
make run
```

或者分别运行发布者和订阅者：

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
[INFO] [publisher.c:160] main(): Starting pubsub publisher...
[INFO] [publisher.c:177] main(): Publisher created successfully
[INFO] [publisher.c:187] main(): Publisher started on unix:///tmp/pubsub_server
[INFO] [publisher.c:190] main(): Waiting for subscribers to connect...
[INFO] [publisher.c:146] connect_handler(): Client connected: id=0
[INFO] [publisher.c:146] connect_handler(): Client connected: id=1
[INFO] [publisher.c:197] main(): Publishing messages...
[INFO] [publisher.c:55] publish_message(): Publishing to /news: Breaking news! Server is online
[INFO] [publisher.c:55] publish_message(): Publishing to /weather: Today's weather is sunny
[INFO] [publisher.c:55] publish_message(): Publishing to /news: Another breaking news story
[INFO] [publisher.c:55] publish_message(): Publishing to /sports: Sports update: Team won the game
[INFO] [publisher.c:205] main(): Waiting for messages to be delivered...
[INFO] [publisher.c:146] connect_handler(): Client disconnected: id=0
[INFO] [publisher.c:146] connect_handler(): Client disconnected: id=1
[INFO] [publisher.c:211] main(): Publisher stopped
```

### 订阅者1输出

```
[INFO] [subscriber.c:86] main(): Starting pubsub subscriber 1...
[INFO] [subscriber.c:95] main(): Subscriber created successfully
[INFO] [subscriber.c:105] main(): Connected to publisher: unix:///tmp/pubsub_server
[INFO] [subscriber.c:110] main(): Subscribing to topics...
[INFO] [subscriber.c:113] main(): Subscribed to /news
[INFO] [subscriber.c:117] main(): Subscribed to /weather
[INFO] [subscriber.c:121] main(): Waiting for messages...
[INFO] [subscriber.c:47] message_handler(): Received message on /news: Breaking news! Server is online
[INFO] [subscriber.c:47] message_handler(): Received message on /weather: Today's weather is sunny
[INFO] [subscriber.c:47] message_handler(): Received message on /news: Another breaking news story
[INFO] [subscriber.c:125] main(): Subscriber 1 closed
```

### 订阅者2输出

```
[INFO] [subscriber2.c:76] main(): Starting pubsub subscriber 2...
[INFO] [subscriber2.c:85] main(): Subscriber created successfully
[INFO] [subscriber2.c:95] main(): Connected to publisher: unix:///tmp/pubsub_server
[INFO] [subscriber2.c:100] main(): Subscribing to topics...
[INFO] [subscriber2.c:103] main(): Subscribed to /sports
[INFO] [subscriber2.c:107] main(): Waiting for messages...
[INFO] [subscriber2.c:47] message_handler(): Received message on /sports: Sports update: Team won the game
[INFO] [subscriber2.c:111] main(): Subscriber 2 closed
```

## 注意事项

- 本示例使用 Unix Socket 作为传输协议
- 服务器地址为 `unix:///tmp/pubsub_server`
- 发布者运行 15 秒后自动停止
- 订阅者等待 10 秒后退出

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
- `ssn_client_set_on_message` - 设置消息处理回调
- `ssn_client_poll` - 轮询客户端事件
- `ssn_client_close` - 关闭 IPC 客户端
