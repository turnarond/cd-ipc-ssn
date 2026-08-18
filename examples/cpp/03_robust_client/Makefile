# Makefile for C++ robust client example（稳健服务端 + 稳健客户端）
# 用法：make           构建两个可执行文件
#       make run       一键体验：三态分类 → 停服务端 → 客户端退避重试 → 重启服务端恢复
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

all: robust_server robust_client

robust_server: robust_server.cpp
	$(CXX) $(CXXFLAGS) -o robust_server robust_server.cpp $(LDFLAGS)

robust_client: robust_client.cpp
	$(CXX) $(CXXFLAGS) -o robust_client robust_client.cpp $(LDFLAGS)

clean:
	rm -f robust_server robust_client

# 一键体验三段式演示：
#   1) 服务端运行中：客户端完成三态分类（成功/超时/1001/1003）；
#   2) 停掉服务端：客户端进入 disconnect → 指数退避重连循环；
#   3) 重启服务端：客户端在退避窗口内探测到恢复，重连成功并退出。
run: robust_server robust_client
	@echo "== 阶段 1：三态分类（服务端运行中）=="
	@./robust_server & pid=$$!; \
	sleep 2; \
	./robust_client; \
	echo "== 阶段 2：断线重连（停服务端，客户端退避重试）=="; \
	kill $$pid 2>/dev/null; \
	wait $$pid 2>/dev/null || true; \
	./robust_client reconnect & cpid=$$!; \
	sleep 4; \
	echo "== 阶段 3：重启服务端（客户端应自动恢复）=="; \
	./robust_server & pid2=$$!; \
	wait $$cpid; \
	rc=$$?; \
	kill $$pid2 2>/dev/null; \
	wait $$pid2 2>/dev/null || true; \
	echo "== 演示结束 =="; \
	exit $$rc
