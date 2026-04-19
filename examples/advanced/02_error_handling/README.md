# Error Handling Example

This example demonstrates how to handle errors in cd-ipc-ssn library.

## Overview

The example shows how to:
1. Detect and handle connection errors
2. Handle RPC call failures
3. Handle message sending errors
4. Implement robust error recovery

## Features Demonstrated

- Connection error handling
- RPC call error handling
- Timeout error handling
- Error recovery strategies
- Error logging and reporting

## Build and Run

### Build

```bash
cd examples/advanced/02_error_handling
make
```

### Run

```bash
./error_handling
```

## Expected Output

```
[INFO] Error handling example started
[INFO] Test 1: Connection to non-existent server
[ERROR] Failed to connect to server: /tmp/non_existent_server
[INFO] Test 1 passed (expected error)
[INFO] Test 2: RPC call to non-existent method
[ERROR] RPC call failed: No such method
[INFO] Test 2 passed (expected error)
[INFO] Test 3: Message send with timeout
[ERROR] Failed to send message: Timeout
[INFO] Test 3 passed (expected error)
[INFO] All error handling tests completed
```

## Error Types Covered

- Connection errors (server not running)
- RPC method not found
- Timeout errors
- Resource errors
- Protocol errors
