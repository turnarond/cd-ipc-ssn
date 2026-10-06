# Makefile for ssn::Node 回调内双向回复示例
# 用法：make           构建
#       make run       一键体验（单进程自演示，无需外部服务端）
#       make clean     清理构建产物
TOP_DIR = ../../../..
BUILD_DIR = $(TOP_DIR)/build
LIB_DIR = $(BUILD_DIR)
INCLUDE_DIR = $(TOP_DIR)/include
SRC_DIR = $(TOP_DIR)/src
THIRD_PARTY = $(TOP_DIR)/third_party

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -I$(INCLUDE_DIR) -I$(SRC_DIR) -I$(THIRD_PARTY)
LDFLAGS = -L$(LIB_DIR) -Wl,-rpath,$(LIB_DIR) -lssn_framework -lssn_transport -lpthread

all: duplex_message

duplex_message: duplex_message.cpp
	$(CXX) $(CXXFLAGS) -o duplex_message duplex_message.cpp $(LDFLAGS)

clean:
	rm -f duplex_message

run: duplex_message
	./duplex_message
