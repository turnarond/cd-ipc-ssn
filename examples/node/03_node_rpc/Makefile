# Makefile for Node RPC example

# Paths
TOP_DIR = ../../..
BUILD_DIR = $(TOP_DIR)/build
LIB_DIR = $(BUILD_DIR)
INCLUDE_DIR = $(TOP_DIR)/src

# Compiler settings
CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Werror -I$(INCLUDE_DIR)
LDFLAGS = -L$(LIB_DIR) -Wl,-rpath,$(LIB_DIR) -lssn_transport -lpthread

# Targets
all: node_rpc

# Node RPC example
node_rpc: node_rpc.c
	$(CC) $(CFLAGS) -o node_rpc node_rpc.c $(LDFLAGS)

# Clean
clean:
	rm -f node_rpc

# Run example
run:
	./node_rpc
