# Makefile for Transport Selection example

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
all: transport_selection

# Transport selection example
transport_selection: transport_selection.c
	$(CC) $(CFLAGS) -o transport_selection transport_selection.c $(LDFLAGS)

# Clean
clean:
	rm -f transport_selection

# Run example
run:
	./transport_selection
