# Makefile for C++ pubsub chat example（聊天服务端 + 订阅客户端）
# 用法：make           构建两个可执行文件
#       make run       一键体验：后台起服务端 → 收满 3 条消息后客户端退出 → 关闭服务端
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

all: pub_server sub_client

pub_server: pub_server.cpp
	$(CXX) $(CXXFLAGS) -o pub_server pub_server.cpp $(LDFLAGS)

sub_client: sub_client.cpp
	$(CXX) $(CXXFLAGS) -o sub_client sub_client.cpp $(LDFLAGS)

clean:
	rm -f pub_server sub_client

# 一键体验：服务端后台运行 → 客户端收满 3 条消息后自动退出 → 关闭服务端。
# 服务端是常驻进程，须显式结束（kill）；手动场景下按 Ctrl+C 即可优雅退出。
run: pub_server sub_client
	@echo "启动聊天服务端（后台）..."
	@./pub_server & pid=$$!; \
	sleep 2; \
	echo "启动订阅客户端..."; \
	./sub_client; \
	kill $$pid 2>/dev/null; \
	wait $$pid 2>/dev/null || true
	@echo "服务端已退出，演示结束"
