# Makefile for Timeout example

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
all: timeout

# Timeout example
timeout: timeout.c
	$(CC) $(CFLAGS) -o timeout timeout.c $(LDFLAGS)

# Clean
clean:
	rm -f timeout

# Run example
run:
	./timeout
