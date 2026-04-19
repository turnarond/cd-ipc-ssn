# Makefile for Error Handling example

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
all: error_handling

# Error handling example
error_handling: error_handling.c
	$(CC) $(CFLAGS) -o error_handling error_handling.c $(LDFLAGS)

# Clean
clean:
	rm -f error_handling

# Run example
run:
	./error_handling
