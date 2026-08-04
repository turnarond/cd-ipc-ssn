# Makefile for Node Lifecycle example

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
all: node_lifecycle

# Node lifecycle example
node_lifecycle: node_lifecycle.c
	$(CC) $(CFLAGS) -o node_lifecycle node_lifecycle.c $(LDFLAGS)

# Clean
clean:
	rm -f node_lifecycle

# Run example
run:
	./node_lifecycle
