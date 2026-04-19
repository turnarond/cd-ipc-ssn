# Makefile for Node Publish/Subscribe example

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
all: node_pubsub

# Node publish/subscribe example
node_pubsub: node_pubsub.c
	$(CC) $(CFLAGS) -o node_pubsub node_pubsub.c $(LDFLAGS)

# Clean
clean:
	rm -f node_pubsub

# Run example
run:
	./node_pubsub
