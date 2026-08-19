#!/bin/bash

# run_examples.sh - Run examples interactively

set -e

# Colors
GREEN="\033[0;32m"
YELLOW="\033[1;33m"
RED="\033[0;31m"
NC="\033[0m" # No Color

echo -e "${GREEN}cd-ipc-ssn Examples Runner${NC}"
echo -e "${YELLOW}=========================${NC}"
echo

# Get the directory containing this script
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT_DIR="${SCRIPT_DIR}/../.."

# Example categories
CATEGORIES=(
    "Basic Examples"
    "Advanced Examples"
    "Protocol Examples"
    "Node Examples"
    "All Examples"
    "Exit"
)

# Example mapping
declare -A EXAMPLES
EXAMPLES["Basic Examples"]=(
    "01_hello_world:Hello World (Basic IPC)"
    "02_rpc_call:RPC Call Example"
    "03_pubsub:Publish/Subscribe Example"
    "04_node_basic:Node Basic Example"
)

EXAMPLES["Advanced Examples"]=(
    "01_multithread:Multithread Example"
    "02_error_handling:Error Handling Example"
    "03_timeout:Timeout Example"
    "04_transport_selection:Transport Selection Example"
)

EXAMPLES["Protocol Examples"]=(
    "01_unix_socket:Unix Socket Example"
    "02_tcp:TCP Example"
    "03_udp:UDP Example"
)

EXAMPLES["Node Examples"]=(
    "01_node_lifecycle:Node Lifecycle Example"
    "02_node_comm:Node Communication Example"
    "03_node_rpc:Node RPC Example"
    "04_node_pubsub:Node Publish/Subscribe Example"
)

# Function to run an example
run_example() {
    local category=$1
    local example=$2
    local name=$3
    
    local example_path=""
    case "$category" in
        "Basic Examples")
            example_path="${ROOT_DIR}/examples/basic/${example}"
            ;;
        "Advanced Examples")
            example_path="${ROOT_DIR}/examples/advanced/${example}"
            ;;
        "Protocol Examples")
            example_path="${ROOT_DIR}/examples/protocols/${example}"
            ;;
        "Node Examples")
            example_path="${ROOT_DIR}/examples/node/${example}"
            ;;
    esac
    
    if [ -d "${example_path}" ]; then
        echo -e "${YELLOW}Running ${name}...${NC}"
        echo -e "${GREEN}=================================${NC}"
        
        cd "${example_path}" || return 1
        
        # Run the example
        if [ -f "run.sh" ]; then
            ./run.sh
        elif [ -f "makefile" ] || [ -f "Makefile" ]; then
            make run
        else
            # Try to find executable
            for exec in *.c; do
                exec_name="${exec%.c}"
                if [ -f "${exec_name}" ]; then
                    ./"${exec_name}"
                    break
                fi
            done
        fi
        
        cd "${ROOT_DIR}"
        
        echo -e "${GREEN}=================================${NC}"
        echo -e "${YELLOW}Example completed${NC}"
        echo
    else
        echo -e "${RED}Error: Example directory not found: ${example_path}${NC}"
        echo
    fi
}

# Function to run all examples
run_all_examples() {
    echo -e "${YELLOW}Running all examples...${NC}"
    echo
    
    for category in "${!EXAMPLES[@]}"; do
        echo -e "${GREEN}=== ${category} ===${NC}"
        
        for example in "${EXAMPLES[$category][@]}"; do
            IFS=":" read -r example_id example_name <<< "$example"
            run_example "$category" "$example_id" "$example_name"
        done
        
        echo
    done
}

# Main menu
while true; do
    echo -e "${GREEN}Select a category:${NC}"
    echo
    
    for i in "${!CATEGORIES[@]}"; do
        echo -e "${YELLOW}$((i+1)). ${CATEGORIES[$i]}${NC}"
    done
    
    echo
    read -p "Enter your choice: " choice
    echo
    
    case "$choice" in
        1)
            category="${CATEGORIES[0]}"
            ;;
        2)
            category="${CATEGORIES[1]}"
            ;;
        3)
            category="${CATEGORIES[2]}"
            ;;
        4)
            category="${CATEGORIES[3]}"
            ;;
        5)
            run_all_examples
            continue
            ;;
        6)
            echo -e "${GREEN}Exiting...${NC}"
            exit 0
            ;;
        *)
            echo -e "${RED}Invalid choice. Please try again.${NC}"
            echo
            continue
            ;;
    esac
    
    # Submenu for examples in the selected category
    echo -e "${GREEN}Select an example from ${category}:${NC}"
    echo
    
    examples=(${EXAMPLES[$category][@]})
    for i in "${!examples[@]}"; do
        IFS=":" read -r example_id example_name <<< "${examples[$i]}"
        echo -e "${YELLOW}$((i+1)). ${example_name}${NC}"
    done
    
    echo -e "${YELLOW}$(( ${#examples[@]} + 1 )). Back to main menu${NC}"
    echo
    read -p "Enter your choice: " example_choice
    echo
    
    if [ "$example_choice" -eq $(( ${#examples[@]} + 1 )) ]; then
        continue
    elif [ "$example_choice" -ge 1 ] && [ "$example_choice" -le "${#examples[@]}" ]; then
        IFS=":" read -r example_id example_name <<< "${examples[$((example_choice-1))]}"
        run_example "$category" "$example_id" "$example_name"
    else
        echo -e "${RED}Invalid choice. Please try again.${NC}"
        echo
    fi
    
    # Wait for user input before continuing
    read -p "Press Enter to continue..." -n 1 -s
    echo
    echo

done
