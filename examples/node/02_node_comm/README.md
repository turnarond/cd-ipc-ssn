# Node Communication Example

This example demonstrates how nodes can communicate with each other using the node abstraction layer.

## Overview

The example shows how to:
1. Create multiple nodes
2. Establish communication between nodes
3. Send messages between nodes
4. Handle incoming messages

## Features Demonstrated

- Node-to-node communication
- Message sending and receiving
- Event handling
- Error handling
- Connection management

## Build and Run

### Build

```bash
cd examples/node/02_node_comm
make
```

### Run

```bash
./node_comm
```

## Expected Output

```
[INFO] Node communication example started
[INFO] Node A created: id=node_A_12345, type=server, name=NodeA
[INFO] Node B created: id=node_B_67890, type=client, name=NodeB
[INFO] Node A started
[INFO] Node B started
[INFO] Node B connected to Node A
[INFO] Node A received message: Hello from Node B!
[INFO] Node B received reply: Hello from Node A!
[INFO] All communication tests completed successfully!
```

## Communication Flow

1. **Node A** (server node) starts and listens for connections
2. **Node B** (client node) connects to Node A
3. **Node B** sends a message to Node A
4. **Node A** receives the message and sends a reply
5. **Node B** receives the reply
6. Both nodes clean up resources

## Node Configuration

- **Node A**: Server node listening on 127.0.0.1:8890
- **Node B**: Client node connecting to Node A
