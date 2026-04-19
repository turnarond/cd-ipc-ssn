# Multithread Example

This example demonstrates how to use cd-ipc-ssn library in a multithreaded environment.

## Overview

The example shows how to:
1. Create multiple threads that communicate using IPC
2. Handle concurrent operations safely
3. Use mutexes for thread synchronization
4. Manage resources across multiple threads

## Features Demonstrated

- Multi-threaded IPC server
- Multi-threaded IPC client
- Thread-safe message handling
- Concurrent RPC calls
- Proper resource cleanup in multi-threaded environment

## Build and Run

### Build

```bash
cd examples/advanced/01_multithread
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
[INFO] Multithreaded server started
[INFO] Thread 1: Handling connection from client 1
[INFO] Thread 2: Handling RPC call from client 1
[INFO] Thread 2: RPC method /echo called with: Hello from thread 1
[INFO] Thread 3: Handling RPC call from client 1
[INFO] Thread 3: RPC method /echo called with: Hello from thread 2
[INFO] Multithreaded server stopped
```

### Client Output

```
[INFO] Multithreaded client started
[INFO] Thread 1: Connected to server
[INFO] Thread 1: RPC call successful: Hello from thread 1
[INFO] Thread 2: RPC call successful: Hello from thread 2
[INFO] Multithreaded client stopped
```
