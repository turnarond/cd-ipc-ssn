# 04_node_pubsub 示例

## 示例说明

本示例展示了 cd-ipc-ssn 库的节点发布/订阅功能。它演示了如何在节点之间使用发布/订阅模式进行通信。

## 功能特性

- 创建两个节点
- 启动两个节点
- 节点发布消息
- 节点订阅消息
- 处理订阅的消息

## 代码结构

```
04_node_pubsub/
├── node_pubsub.c  # 节点发布/订阅示例代码
├── Makefile       # 构建脚本
└── README.md      # 本说明文档
```

## 核心代码解析

### 节点发布/订阅示例 (node_pubsub.c)

1. **节点1配置与创建**
   - 设置节点1类型为 "example"
   - 设置节点1名称为 "publisher_node"
   - 配置节点1能力（Pub/Sub、Server）
   - 设置节点1监听地址为 "tcp://127.0.0.1:8892"
   - 使用 `ssn_node_create` 创建节点1

2. **节点2配置与创建**
   - 设置节点2类型为 "example"
   - 设置节点2名称为 "subscriber_node"
   - 配置节点2能力（Pub/Sub、Client）
   - 设置节点2连接地址为 "tcp://127.0.0.1:8892"
   - 使用 `ssn_node_create` 创建节点2

3. **节点启动**
   - 使用 `ssn_node_start` 启动节点1
   - 使用 `ssn_node_start` 启动节点2

4. **消息订阅**
   - 节点2订阅 `/news` 主题
   - 节点2订阅 `/weather` 主题

5. **消息发布**
   - 节点1向 `/news` 主题发布新闻消息
   - 节点1向 `/weather` 主题发布天气消息

6. **消息处理**
   - 节点2注册消息处理回调函数 `message_handler`
   - 当接收到订阅的消息时，打印消息内容

7. **节点停止与销毁**
   - 使用 `ssn_node_stop` 停止节点1
   - 使用 `ssn_node_stop` 停止节点2
   - 使用 `ssn_node_destroy` 销毁节点1
   - 使用 `ssn_node_destroy` 销毁节点2

## 运行示例

### 构建示例

```bash
cd examples/node/04_node_pubsub
make
```

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
[INFO] [node_pubsub.c:152] main(): Starting node pubsub example...
[INFO] [ssn_node.c:170] ssn_node_create(): node created successfully, id=example_publisher_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456, type=example, name=publisher_node
[INFO] [node_pubsub.c:162] main(): Publisher node created successfully
[INFO] [ssn_node.c:170] ssn_node_create(): node created successfully, id=example_subscriber_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456, type=example, name=subscriber_node
[INFO] [node_pubsub.c:172] main(): Subscriber node created successfully
[INFO] [node_pubsub.c:177] main(): Starting publisher node...
[INFO] [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/transports/ssn_transport_factory.c:102] ssn_transport_factory_init(): Transport factory initialized successfully
[INFO] [ssn_node.c:230] ssn_node_start(): server started on tcp://127.0.0.1:8892
[INFO] [ssn_node.c:244] ssn_node_start(): node started successfully, id=example_publisher_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_pubsub.c:182] main(): Publisher node started successfully
[INFO] [node_pubsub.c:187] main(): Starting subscriber node...
[INFO] [/home/yanchaodong/work/acoinfo/edge-framework/src/comms/cd-ipc-ssn/src/transports/ssn_transport_factory.c:102] ssn_transport_factory_init(): Transport factory initialized successfully
[INFO] [ssn_node.c:230] ssn_node_start(): server started on tcp://127.0.0.1:8892
[INFO] [ssn_node.c:244] ssn_node_start(): node started successfully, id=example_subscriber_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_pubsub.c:192] main(): Subscriber node started successfully
[INFO] [node_pubsub.c:197] main(): Subscribing to topics...
[INFO] [node_pubsub.c:207] main(): Subscribed to /news
[INFO] [node_pubsub.c:212] main(): Subscribed to /weather
[INFO] [node_pubsub.c:217] main(): Testing node pubsub...
[INFO] [node_pubsub.c:46] message_handler(): Received message on /news: Breaking news! Node pubsub is working
[INFO] [node_pubsub.c:46] message_handler(): Received message on /weather: Today's weather is sunny
[INFO] [node_pubsub.c:227] main(): Node pubsub test completed
[INFO] [node_pubsub.c:232] main(): Running for 5 seconds...
[INFO] [node_pubsub.c:237] main(): Stopping nodes...
[INFO] [ssn_node.c:272] ssn_node_stop(): server stopped
[INFO] [ssn_node.c:286] ssn_node_stop(): node stopped successfully, id=example_publisher_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [ssn_node.c:272] ssn_node_stop(): server stopped
[INFO] [ssn_node.c:286] ssn_node_stop(): node stopped successfully, id=example_subscriber_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_pubsub.c:242] main(): Nodes stopped successfully
[INFO] [node_pubsub.c:247] main(): Destroying nodes...
[INFO] [ssn_node.c:322] ssn_node_destroy(): node destroyed, id=example_publisher_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [ssn_node.c:322] ssn_node_destroy(): node destroyed, id=example_subscriber_node_pc-Lenovo-WenTian-WR3220-G2_1713600000_123456
[INFO] [node_pubsub.c:252] main(): Nodes destroyed successfully
[INFO] [node_pubsub.c:254] main(): Node pubsub example completed
```

## 注意事项

- 本示例使用 TCP 作为传输协议
- 节点1监听地址为 `tcp://127.0.0.1:8892`
- 节点2连接地址为 `tcp://127.0.0.1:8892`
- 节点运行 5 秒后自动停止

## 相关 API

- `ssn_node_create` - 创建节点
- `ssn_node_start` - 启动节点
- `ssn_node_stop` - 停止节点
- `ssn_node_destroy` - 销毁节点
- `ssn_node_get_client` - 获取节点的客户端
- `ssn_node_get_server` - 获取节点的服务器
- `ssn_node_subscribe` - 订阅主题
- `ssn_node_set_client_message_handler` - 设置客户端消息处理回调
- `ssn_node_publish` - 发布消息
