# Unix Socket Example

This example demonstrates how to use Unix Socket transport protocol in cd-ipc-ssn library.

## Overview

Unix Socket is a communication protocol for inter-process communication (IPC) on the same machine.
It's fast, secure, and efficient for local communication.

## Features Demonstrated

- Unix Socket server creation
- Unix Socket client creation
- Connection management
- Message passing
- Error handling

## Build and Run

### Build

```bash
cd examples/protocols/01_unix_socket
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
[INFO] Unix Socket server started on /tmp/unix_socket_server
[INFO] Client connected: id=1
[INFO] Received message: Hello from Unix Socket client!
[INFO] Server stopped
```

### Client Output

```
[INFO] Unix Socket client connected to /tmp/unix_socket_server
[INFO] Message sent successfully
[INFO] Client disconnected
```

## Unix Socket Advantages

- **Fast**: No network overhead
- **Secure**: Local only, no network exposure
- **Efficient**: Low latency
- **Simple**: Easy to use

## Use Cases

- Local inter-process communication
- System services communication
- Desktop application components
- High-performance IPC
