# Timeout Example

This example demonstrates how to handle timeouts in cd-ipc-ssn library.

## Overview

The example shows how to:
1. Set connection timeouts
2. Set RPC call timeouts
3. Set message send timeouts
4. Handle timeout errors gracefully

## Features Demonstrated

- Connection timeout handling
- RPC call timeout handling
- Message send timeout handling
- Timeout configuration
- Graceful timeout recovery

## Build and Run

### Build

```bash
cd examples/advanced/03_timeout
make
```

### Run

```bash
./timeout
```

## Expected Output

```
[INFO] Timeout example started
[INFO] Test 1: Connection timeout
[ERROR] Connection timeout after 1 seconds
[INFO] Test 1 passed (expected timeout)
[INFO] Test 2: RPC call timeout
[ERROR] RPC call timeout after 2 seconds
[INFO] Test 2 passed (expected timeout)
[INFO] Test 3: Message send timeout
[ERROR] Message send timeout after 1 seconds
[INFO] Test 3 passed (expected timeout)
[INFO] All timeout tests completed
```

## Timeout Types Covered

- Connection timeouts
- RPC call timeouts
- Message send timeouts
- Idle timeouts
- Operation timeouts
