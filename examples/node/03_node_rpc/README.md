# Node RPC Example

This example demonstrates how to use RPC (Remote Procedure Call) between nodes in the node abstraction layer.

## Overview

The example shows how to:
1. Create server and client nodes
2. Register RPC methods on server node
3. Call RPC methods from client node
4. Handle RPC responses

## Features Demonstrated

- RPC method registration
- RPC method invocation
- RPC response handling
- Error handling in RPC
- Node-to-node RPC communication

## Build and Run

### Build

```bash
cd examples/node/03_node_rpc
make
```

### Run

```bash
./node_rpc
```

## Expected Output

```
[INFO] Node RPC example started
[INFO] Server node created: id=server_node_12345, type=server, name=ServerNode
[INFO] Client node created: id=client_node_67890, type=client, name=ClientNode
[INFO] Server node started
[INFO] Client node started
[INFO] RPC method /math/add registered
[INFO] RPC method /math/subtract registered
[INFO] Client calling /math/add with {"a": 5, "b": 3}
[INFO] Server received RPC call: /math/add with {"a": 5, "b": 3}
[INFO] Client received RPC response: 8
[INFO] Client calling /math/subtract with {"a": 10, "b": 4}
[INFO] Server received RPC call: /math/subtract with {"a": 10, "b": 4}
[INFO] Client received RPC response: 6
[INFO] All RPC tests completed successfully!
```

## RPC Methods

- **/math/add** - Adds two numbers
- **/math/subtract** - Subtracts two numbers

## Node Configuration

- **Server Node**: Listening on 127.0.0.1:8891
- **Client Node**: Connecting to Server Node
