# Hello World Example

This example demonstrates the basic IPC communication between a server and client using cd-ipc-ssn library.

## Overview

The example consists of two programs:
1. `server.c` - Creates an IPC server that listens for connections and handles messages
2. `client.c` - Creates an IPC client that connects to the server and sends a message

## Features Demonstrated

- Basic server creation and startup
- Basic client creation and connection
- Message sending from client to server
- Message handling on server side
- Clean shutdown of server and client

## Build and Run

### Build

```bash
cd examples/basic/01_hello_world
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
[INFO] IPC server created successfully
[INFO] IPC server started on /tmp/hello_server
[INFO] Client connected: id=1
[INFO] Message received from client 1: Hello from client!
[INFO] IPC server stopped
[INFO] IPC server destroyed
```

### Client Output

```
[INFO] IPC client created successfully
[INFO] Connected to server: /tmp/hello_server
[INFO] Message sent successfully
[INFO] IPC client destroyed
```
