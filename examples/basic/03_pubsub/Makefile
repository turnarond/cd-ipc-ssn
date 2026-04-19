# Makefile for Publish/Subscribe example

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
all: publisher subscriber subscriber2

# Publisher
publisher: publisher.c
	$(CC) $(CFLAGS) -o publisher publisher.c $(LDFLAGS)

# Subscriber
subscriber: subscriber.c
	$(CC) $(CFLAGS) -o subscriber subscriber.c $(LDFLAGS)

# Subscriber2
subscriber2: subscriber2.c
	$(CC) $(CFLAGS) -o subscriber2 subscriber2.c $(LDFLAGS)

# Clean
clean:
	rm -f publisher subscriber subscriber2

# Run publisher
run-publisher:
	./publisher

# Run subscriber
run-subscriber:
	./subscriber

# Run subscriber2
run-subscriber2:
	./subscriber2

# Run all (in separate terminals)
run:
	@echo "Starting publisher in background..."
	@./publisher &
	@sleep 2
	@echo "Starting subscriber in background..."
	@./subscriber &
	@sleep 1
	@echo "Starting subscriber2..."
	@./subscriber2
	@echo "Waiting for all processes to finish..."
	@wait
