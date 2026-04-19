#!/bin/bash

# build_examples.sh - Build all examples

set -e

# Colors
GREEN="\033[0;32m"
YELLOW="\033[1;33m"
RED="\033[0;31m"
NC="\033[0m" # No Color

echo -e "${GREEN}Building all examples...${NC}"
echo

# Get the directory containing this script
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT_DIR="${SCRIPT_DIR}/../.."

# Build order
EXAMPLE_DIRS=(
    "examples/basic/01_hello_world"
    "examples/basic/02_rpc_call"
    "examples/basic/03_pubsub"
    "examples/basic/04_node_basic"
    "examples/advanced/01_multithread"
    "examples/advanced/02_error_handling"
    "examples/advanced/03_timeout"
    "examples/advanced/04_transport_selection"
    "examples/protocols/01_unix_socket"
    "examples/protocols/02_tcp"
    "examples/protocols/03_udp"
    "examples/node/01_node_lifecycle"
    "examples/node/02_node_comm"
    "examples/node/03_node_rpc"
    "examples/node/04_node_pubsub"
)

total=${#EXAMPLE_DIRS[@]}
current=0

for dir in "${EXAMPLE_DIRS[@]}"; do
    current=$((current + 1))
    example_path="${ROOT_DIR}/${dir}"
    
    if [ -d "${example_path}" ]; then
        echo -e "${YELLOW}[${current}/${total}] Building ${dir}...${NC}"
        
        cd "${example_path}" || continue
        
        # Build the example
        if make > build.log 2>&1; then
            echo -e "  ${GREEN}✓ Built successfully${NC}"
        else
            echo -e "  ${RED}✗ Build failed${NC}"
            echo -e "  ${YELLOW}See build.log for details${NC}"
        fi
        
        cd "${ROOT_DIR}"
    else
        echo -e "${YELLOW}[${current}/${total}] Skipping ${dir} (directory not found)${NC}"
    fi
    
    echo
done

echo -e "${GREEN}Build process completed!${NC}"
echo -e "${YELLOW}You can run examples using run_examples.sh${NC}"
