# Node Publish/Subscribe Example

This example demonstrates how to use publish/subscribe functionality between nodes in the node abstraction layer.

## Overview

The example shows how to:
1. Create publisher and subscriber nodes
2. Subscribe to topics
3. Publish messages to topics
4. Receive published messages

## Features Demonstrated

- Topic subscription
- Message publishing
- Multiple subscribers
- Message filtering by topic
- Event handling

## Build and Run

### Build

```bash
cd examples/node/04_node_pubsub
make
```

### Run

```bash
./node_pubsub
```

## Expected Output

```
[INFO] Node publish/subscribe example started
[INFO] Publisher node created: id=publisher_12345, type=publisher, name=Publisher
[INFO] Subscriber1 node created: id=subscriber1_67890, type=subscriber, name=Subscriber1
[INFO] Subscriber2 node created: id=subscriber2_54321, type=subscriber, name=Subscriber2
[INFO] Publisher node started
[INFO] Subscriber1 node started
[INFO] Subscriber2 node started
[INFO] Subscriber1 subscribed to /news
[INFO] Subscriber2 subscribed to /weather
[INFO] Publisher publishing to /news: Breaking news! Server is online
[INFO] Subscriber1 received message: Breaking news! Server is online
[INFO] Publisher publishing to /weather: Today's weather is sunny
[INFO] Subscriber2 received message: Today's weather is sunny
[INFO] All publish/subscribe tests completed successfully!
```

## Topics

- **/news** - News messages
- **/weather** - Weather information

## Node Configuration

- **Publisher Node**: Publishing messages to topics
- **Subscriber1 Node**: Subscribed to /news topic
- **Subscriber2 Node**: Subscribed to /weather topic
