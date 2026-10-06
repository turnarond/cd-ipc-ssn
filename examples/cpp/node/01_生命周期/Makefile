# Makefile for ssn::Node 生命周期示例（RAII 创建/监听/连接/停止）
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

all: node_lifecycle

node_lifecycle: node_lifecycle.cpp
	$(CXX) $(CXXFLAGS) -o node_lifecycle node_lifecycle.cpp $(LDFLAGS)

clean:
	rm -f node_lifecycle

run: node_lifecycle
	./node_lifecycle
