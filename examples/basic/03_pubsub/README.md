# Publish/Subscribe Example

This example demonstrates how to use the publish/subscribe functionality of cd-ipc-ssn library.

## Overview

The example consists of three programs:
1. `publisher.c` - Creates an IPC server that publishes messages to topics
2. `subscriber.c` - Creates an IPC client that subscribes to topics
3. `subscriber2.c` - Another subscriber to demonstrate multiple subscribers

## Features Demonstrated

- Topic publishing from server
- Topic subscription from clients
- Multiple subscribers receiving the same message
- Message filtering by topic
- Clean shutdown of publisher and subscribers

## Build and Run

### Build

```bash
cd examples/basic/03_pubsub
make
```

### Run

1. Start the publisher:
   ```bash
   ./publisher
   ```

2. In another terminal, start the first subscriber:
   ```bash
   ./subscriber
   ```

3. In a third terminal, start the second subscriber:
   ```bash
   ./subscriber2
   ```

## Expected Output

### Publisher Output

```
[INFO] Publisher created successfully
[INFO] Publisher started on /tmp/pubsub_server
[INFO] Publishing message to /news: Breaking news! Server is online
[INFO] Publishing message to /weather: Today's weather is sunny
[INFO] Publishing message to /news: Another breaking news story
[INFO] Publisher stopped
[INFO] Publisher destroyed
```

### Subscriber Output

```
[INFO] Subscriber created successfully
[INFO] Connected to publisher: /tmp/pubsub_server
[INFO] Subscribed to topic: /news
[INFO] Received message on /news: Breaking news! Server is online
[INFO] Received message on /news: Another breaking news story
[INFO] Subscriber destroyed
```

### Subscriber2 Output

```
[INFO] Subscriber2 created successfully
[INFO] Connected to publisher: /tmp/pubsub_server
[INFO] Subscribed to topic: /weather
[INFO] Received message on /weather: Today's weather is sunny
[INFO] Subscriber2 destroyed
```

## Topics

- `/news` - News messages
- `/weather` - Weather information
- `/sports` - Sports updates (not used in this example)
