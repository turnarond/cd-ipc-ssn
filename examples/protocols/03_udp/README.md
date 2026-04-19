# UDP Example

This example demonstrates how to use UDP transport protocol in cd-ipc-ssn library.

## Overview

UDP (User Datagram Protocol) is a connectionless, lightweight protocol for network communication.
It's suitable for applications that require low latency and can tolerate some packet loss.

## Features Demonstrated

- UDP server creation
- UDP client creation
- Connectionless communication
- Low-latency data transfer
- Error handling

## Build and Run

### Build

```bash
cd examples/protocols/03_udp
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
[INFO] UDP server started on 127.0.0.1:9999
[INFO] Received message: Hello from UDP client!
[INFO] Server stopped
```

### Client Output

```
[INFO] UDP client connected to 127.0.0.1:9999
[INFO] Message sent successfully
[INFO] Client disconnected
```

## UDP Advantages

- **Fast**: Low overhead
- **Low latency**: No connection setup
- **Simple**: Less complex
- **Multicast support**: Can send to multiple recipients

## Use Cases

- Real-time applications (voice, video)
- Gaming
- Streaming media
- DNS
- SNMP
- Any application where speed is more important than reliability
