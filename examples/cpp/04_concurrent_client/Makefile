# Makefile for C++ concurrent client example（并发对比：串行化 vs 真并行）
# 用法：make           构建两个可执行文件
#       make run       一键体验：后台起服务端 → 客户端实测耗时对比 → 关闭服务端
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

all: concurrent_server concurrent_client

concurrent_server: concurrent_server.cpp
	$(CXX) $(CXXFLAGS) -o concurrent_server concurrent_server.cpp $(LDFLAGS)

concurrent_client: concurrent_client.cpp
	$(CXX) $(CXXFLAGS) -o concurrent_client concurrent_client.cpp $(LDFLAGS)

clean:
	rm -f concurrent_server concurrent_client

# 一键体验：服务端后台运行 → 客户端输出三组耗时对比 → 关闭服务端
run: concurrent_server concurrent_client
	@echo "启动并发服务端（后台）..."
	@./concurrent_server & pid=$$!; \
	sleep 2; \
	echo "运行客户端（耗时对比）..."; \
	./concurrent_client; \
	kill $$pid 2>/dev/null; \
	wait $$pid 2>/dev/null || true
	@echo "服务端已退出，演示结束"
