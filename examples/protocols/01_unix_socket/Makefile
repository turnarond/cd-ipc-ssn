# Makefile for Unix Socket example

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
all: server client

# Server
server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

# Client
client: client.c
	$(CC) $(CFLAGS) -o client client.c $(LDFLAGS)

# Clean
clean:
	rm -f server client

# Run server
run-server:
	./server

# Run client
run-client:
	./client

# Run both (in separate terminals)
run:
	@echo "Starting server in background..."
	@./server &
	@sleep 2
	@echo "Starting client..."
	@./client
	@echo "Waiting for server to finish..."
	@wait
