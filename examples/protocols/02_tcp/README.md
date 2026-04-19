# TCP Example

This example demonstrates how to use TCP transport protocol in cd-ipc-ssn library.

## Overview

TCP (Transmission Control Protocol) is a reliable, connection-oriented protocol for network communication.
It's suitable for applications that require reliable data delivery.

## Features Demonstrated

- TCP server creation
- TCP client creation
- Network communication
- Reliable data transfer
- Error handling

## Build and Run

### Build

```bash
cd examples/protocols/02_tcp
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
[INFO] TCP server started on 127.0.0.1:8888
[INFO] Client connected: id=1
[INFO] Received message: Hello from TCP client!
[INFO] Server stopped
```

### Client Output

```
[INFO] TCP client connected to 127.0.0.1:8888
[INFO] Message sent successfully
[INFO] Client disconnected
```

## TCP Advantages

- **Reliable**: Guaranteed delivery
- **Ordered**: Data arrives in order
- **Connection-oriented**: Stateful communication
- **Error checking**: Automatic retransmission

## Use Cases

- Network communication
- Client-server applications
- Web servers
- Database connections
- Any application requiring reliable data transfer
