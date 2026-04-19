# Makefile for Node Communication example

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
all: node_comm

# Node communication example
node_comm: node_comm.c
	$(CC) $(CFLAGS) -o node_comm node_comm.c $(LDFLAGS)

# Clean
clean:
	rm -f node_comm

# Run example
run:
	./node_comm
