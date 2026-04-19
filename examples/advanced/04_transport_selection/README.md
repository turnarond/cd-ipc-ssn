# Transport Selection Example

This example demonstrates how to select different transport protocols in cd-ipc-ssn library.

## Overview

The example shows how to:
1. Use Unix Socket transport
2. Use TCP transport
3. Use UDP transport
4. Switch between different transport protocols

## Features Demonstrated

- Unix Socket transport
- TCP transport
- UDP transport
- Transport configuration
- Protocol-specific options

## Build and Run

### Build

```bash
cd examples/advanced/04_transport_selection
make
```

### Run

```bash
./transport_selection
```

## Expected Output

```
[INFO] Transport selection example started
[INFO] Test 1: Unix Socket transport
[INFO] Unix Socket server started on /tmp/unix_socket_server
[INFO] Unix Socket client connected
[INFO] Message sent and received successfully
[INFO] Test 1 passed
[INFO] Test 2: TCP transport
[INFO] TCP server started on 127.0.0.1:8888
[INFO] TCP client connected
[INFO] Message sent and received successfully
[INFO] Test 2 passed
[INFO] Test 3: UDP transport
[INFO] UDP server started on 127.0.0.1:9999
[INFO] UDP client connected
[INFO] Message sent and received successfully
[INFO] Test 3 passed
[INFO] All transport tests completed
```

## Transport Protocols

- **Unix Socket**: For local inter-process communication
- **TCP**: For reliable network communication
- **UDP**: For fast, connectionless communication
