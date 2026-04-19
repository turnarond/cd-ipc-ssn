# Node Basic Example

This example demonstrates the basic functionality of the node abstraction layer in cd-ipc-ssn library.

## Overview

The example shows how to create, start, stop, and destroy a node using the node abstraction layer.

## Features Demonstrated

- Node creation with configuration
- Node lifecycle management (create, start, stop, destroy)
- Node state checking
- Node capability checking
- Node statistics

## Build and Run

### Build

```bash
cd examples/basic/04_node_basic
make
```

### Run

```bash
./node_basic
```

## Expected Output

```
[INFO] Node creation and destruction test
[INFO] Node created successfully: id=test_node_12345, type=test, name=basic-node
[INFO] Node state: STOPPED
[INFO] Node capabilities: 0x000f (SERVER|CLIENT|RPC|PUBSUB)
[INFO] Node started successfully
[INFO] Node state: ACTIVE
[INFO] Node statistics: active_connections=0, total_messages=0
[INFO] Node stopped successfully
[INFO] Node state: STOPPED
[INFO] Node destroyed successfully
[INFO] All tests passed!
```

## Node Configuration

The example creates a node with the following configuration:

- Node type: "test"
- Node name: "basic-node"
- Listen address: "127.0.0.1"
- Listen port: 8888
- Capabilities: SERVER, CLIENT, RPC, PUBSUB
