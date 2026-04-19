# Makefile for Node Basic example

# Paths
TOP_DIR = ../../..
BUILD_DIR = $(TOP_DIR)/build
LIB_DIR = $(BUILD_DIR)
INCLUDE_DIR = $(TOP_DIR)/src

# Compiler settings
CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Werror -I$(INCLUDE_DIR)
LDFLAGS = -L$(LIB_DIR) -lssn_transport -lpthread

# Targets
all: node_basic

# Node basic example
node_basic: node_basic.c
	$(CC) $(CFLAGS) -o node_basic node_basic.c $(LDFLAGS)

# Clean
clean:
	rm -f node_basic

# Run example
run:
	./node_basic
