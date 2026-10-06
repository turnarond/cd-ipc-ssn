# Makefile for ssn::Node 外部事件循环示例
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

all: external_loop

external_loop: external_loop.cpp
	$(CXX) $(CXXFLAGS) -o external_loop external_loop.cpp $(LDFLAGS)

clean:
	rm -f external_loop

run: external_loop
	./external_loop
