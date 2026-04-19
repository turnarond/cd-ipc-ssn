# Node Lifecycle Example

This example demonstrates the lifecycle management of nodes in the node abstraction layer.

## Overview

The example shows how to:
1. Create a node with different configurations
2. Start a node
3. Stop a node
4. Destroy a node
5. Check node state and capabilities

## Features Demonstrated

- Node creation with various configurations
- Node lifecycle management (create, start, stop, destroy)
- Node state monitoring
- Node capability checking
- Resource management

## Build and Run

### Build

```bash
cd examples/node/01_node_lifecycle
make
```

### Run

```bash
./node_lifecycle
```

## Expected Output

```
[INFO] Node lifecycle example started
[INFO] Test 1: Basic node lifecycle
[INFO] Node created successfully: id=test_node_12345, type=test, name=basic-node
[INFO] Node state: STOPPED
[INFO] Node capabilities: 0x000f (SERVER|CLIENT|RPC|PUBSUB)
[INFO] Node started successfully
[INFO] Node state: ACTIVE
[INFO] Node stopped successfully
[INFO] Node state: STOPPED
[INFO] Node destroyed successfully
[INFO] Test 2: Minimal configuration
[INFO] Node created with minimal config: id=minimal_node_67890, type=minimal, name=minimal
[INFO] Test 3: Server-only node
[INFO] Server-only node created: id=server_node_54321, type=server, name=server-only
[INFO] All lifecycle tests completed successfully!
```

## Node States

- **STOPPED**: Node is initialized but not running
- **ACTIVE**: Node is running and ready for communication
- **ERROR**: Node encountered an error

## Node Capabilities

- **SERVER**: Can act as a server
- **CLIENT**: Can act as a client
- **RPC**: Supports RPC functionality
- **PUBSUB**: Supports publish/subscribe functionality
- **DISCOVERY**: Supports node discovery (future feature)
- **QOS**: Supports quality of service (future feature)
