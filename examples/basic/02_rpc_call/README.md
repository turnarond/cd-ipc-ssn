# RPC Call Example

This example demonstrates how to use the RPC (Remote Procedure Call) functionality of cd-ipc-ssn library.

## Overview

The example consists of two programs:
1. `server.c` - Creates an IPC server that provides RPC methods
2. `client.c` - Creates an IPC client that calls RPC methods on the server

## Features Demonstrated

- RPC method registration on server
- RPC method invocation from client
- Synchronous RPC calls
- RPC response handling
- Error handling in RPC

## Build and Run

### Build

```bash
cd examples/basic/02_rpc_call
make
```

### Run

1. Start the server:
   ```bash
   ./server
   ```

2. In another terminal, start the client:
   ```bash
   ./client
   ```

## Expected Output

### Server Output

```
[INFO] RPC server created successfully
[INFO] RPC server started on /tmp/rpc_server
[INFO] Client connected: id=1
[INFO] RPC method called: /math/add with parameters: {"a": 5, "b": 3}
[INFO] RPC method /math/add returned: 8
[INFO] RPC server stopped
[INFO] RPC server destroyed
```

### Client Output

```
[INFO] RPC client created successfully
[INFO] Connected to server: /tmp/rpc_server
[INFO] RPC call /math/add successful, result: 8
[INFO] RPC client destroyed
```

## RPC Methods

- `/math/add` - Adds two numbers
- `/math/subtract` - Subtracts two numbers
- `/math/multiply` - Multiplies two numbers
- `/math/divide` - Divides two numbers
