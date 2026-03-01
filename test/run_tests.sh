#!/bin/bash

# Test script for cd-ipc-ssn library

echo "=== Building and running IPC tests ==="

# Create build directory if it doesn't exist
if [ ! -d "../build" ]; then
    echo "Creating build directory..."
    mkdir -p ../build
fi

# Change to build directory
cd ../build

# Run CMake
echo "Running CMake..."
cmake ..

# Build the library
echo "Building library..."
make -j4

# Build test files
echo "Building test files..."
make test_ipc_server test_ipc_client test_ipc_comprehensive

# Run tests
echo "\n=== Running tests ==="

# Test 1: Basic server-client test
echo "\n1. Running basic server-client test..."
# Start server in background
./test_ipc_server &
SERVER_PID=$!

# Wait for server to start
sleep 2

# Run client
./test_ipc_client &
CLIENT_PID=$!

# Let them run for a few seconds
sleep 5

# Kill server and client
kill $SERVER_PID $CLIENT_PID 2>/dev/null
wait $SERVER_PID $CLIENT_PID 2>/dev/null

# Test 2: Comprehensive test
echo "\n2. Running comprehensive test..."
./test_ipc_comprehensive

# Cleanup
echo "\n=== Test cleanup ==="

# Remove socket file if it exists
rm -f ipc-light_server ipc-test-server 2>/dev/null

echo "\n=== All tests completed ==="
