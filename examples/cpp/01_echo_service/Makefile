# Makefile for C++ echo example（echo 服务端 + 客户端）
# 用法：make           构建两个可执行文件
#       make run       一键体验：后台起服务端 → 运行客户端 → 自动关闭服务端
#       make clean     清理构建产物
TOP_DIR = ../../..
BUILD_DIR = $(TOP_DIR)/build
LIB_DIR = $(BUILD_DIR)
INCLUDE_DIR = $(TOP_DIR)/include
SRC_DIR = $(TOP_DIR)/src
THIRD_PARTY = $(TOP_DIR)/third_party

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -I$(INCLUDE_DIR) -I$(SRC_DIR) -I$(THIRD_PARTY)
LDFLAGS = -L$(LIB_DIR) -Wl,-rpath,$(LIB_DIR) -lssn_framework -lssn_transport -lpthread

all: echo_server echo_client

echo_server: echo_server.cpp
	$(CXX) $(CXXFLAGS) -o echo_server echo_server.cpp $(LDFLAGS)

echo_client: echo_client.cpp
	$(CXX) $(CXXFLAGS) -o echo_client echo_client.cpp $(LDFLAGS)

clean:
	rm -f echo_server echo_client

# 一键体验：服务端后台运行 → 客户端调用后退出 → 关闭服务端。
# 注意：服务端是常驻进程，客户端退出后仍存活，须显式结束（kill）；
# 手动场景下按 Ctrl+C 即可优雅退出。
run: echo_server echo_client
	@echo "启动服务端（后台）..."
	@./echo_server & pid=$$!; \
	sleep 2; \
	echo "启动客户端..."; \
	./echo_client; \
	kill $$pid 2>/dev/null; \
	wait $$pid 2>/dev/null || true
	@echo "服务端已退出，演示结束"
