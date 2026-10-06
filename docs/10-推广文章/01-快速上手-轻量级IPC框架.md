# SSN：一个轻量级 IPC/分布式通信框架的快速上手

> 面向：C/C++ 开发者，希望快速了解 SSN 是什么、能做什么、如何 5 分钟跑通第一个程序。
> 本文基于 SSN v2.5.1，所有代码示例均可直接编译运行。

## 1. SSN 是什么

**SSN（Scalable Socket Network，可扩展套接字网络）** 是一个轻量级的 IPC/分布式通信框架，
基于 C99 实现，无重量级外部依赖。它解决的是**进程间通信**这件事：

- 单机多进程之间（Unix Domain Socket）
- 跨设备、跨网络（TCP / UDP）
- 三种通信模式：**RPC**（请求/应答）、**发布/订阅**（PubSub）、**点对点消息**

一句话：如果你正在用裸 socket 手写协议、处理粘包、管理重连，SSN 把这些脏活都封装好了。

### 核心特性

| 特性 | 说明 |
|------|------|
| 三种通信模式 | RPC / PubSub / 点对点消息 |
| 三种传输 | Unix Socket / TCP / UDP（统一地址格式 `tcp://host:port`、`unix:///path`） |
| 分层架构 | 节点抽象 → 客户端/服务端 → 协议 → 传输 → 平台抽象（VSI） |
| 节点模型 | `ssn_node_t` 双角色，一个节点同时是生产者/消费者/服务提供者 |
| C++ 服务框架 | v2.4.0 起，`ServiceManager::Run<T>()` 一行启动服务 |
| 工程完备 | 22 套件 1471 例测试全绿、`find_package(ssn)` 包配置、GitHub Actions CI、docsify 文档站 |

### 适用场景

- **边缘计算**：边缘节点间的轻量数据分发（设备采集数据实时上报、指令下发）
- **进程间通信**：单机多进程通信，替代裸 socket 编程
- **设备互联**：跨设备、跨网络的分布式通信

## 2. 5 分钟跑通第一个程序

### 2.1 构建

```bash
git clone https://github.com/turnarond/cd-ipc-ssn.git
cd cd-ipc-ssn
mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

产物：`libssn_transport.so`（C 库）+ `libssn_framework.so`（C++ 服务框架，可选）。

### 2.2 第一个应用（发布/订阅）

v3.0.0 推荐入口是 C++ 服务框架（`libssn_framework.so`，C++17）：服务端
`SsnService` + 客户端 `SsnClient`，事件循环由框架内部驱动，无需手工 poll。

```cpp
// demo.cpp
#include <chrono>
#include <cstdio>
#include <thread>

#include <nlohmann/json.hpp>
#include <ssn/framework/SsnClient.hpp>
#include <ssn/framework/SsnService.hpp>

int main() {
    ssn::SsnService server;                 // 服务端：默认监听 127.0.0.1:18888
    server.initialize(0, nullptr);
    server.start();

    ssn::SsnClient client;                  // 客户端：连接 + 订阅
    client.connect("tcp://127.0.0.1:18888");
    client.subscribe("/news", [](const std::string& topic, const nlohmann::json& data) {
        std::printf("Received: %s\n", data.dump().c_str());
    });

    server.publish("/news", {{"msg", "hello"}});   // 发布（订阅者收到）

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    client.disconnect();
    server.stop();
    server.destroy();
    return 0;
}
```

编译运行：

```bash
g++ -std=c++17 -Wall -I include -o demo demo.cpp -L build \
    -lssn_framework -lssn_transport -lpthread -Wl,-rpath,$PWD/build
./demo
```

输出：

```
Received: {"msg":"hello"}
```

> **关键认知**：低层 C API（`ssn_client_*` / `ssn_server_*`）是**事件循环驱动**
> 的——必须周期性调用 `ssn_client_poll` / `ssn_server_poll`，连接握手、消息收发
> 与回调才会发生。而 C++ 框架（`SsnService`/`SsnClient`/`ssn::Node::startBackground`）
> 已把事件循环封装在内部线程中，开箱即用。

### 2.3 三种通信模式速览

| 模式 | 关键 API | 场景 |
|------|---------|------|
| RPC | `SsnService::RegisterMethod` / `SsnClient::Call`（低层：`ssn_rpc_*`） | 请求-应答，如查询设备状态 |
| PubSub | `SsnService::publish` / `SsnClient::subscribe`（低层：`ssn_pubsub_*`） | 一对多广播，如数据分发 |
| 消息 | `ssn::Node` 原始 MESSAGE 帧（`send`/`broadcast`） | 定向发送，如指令下发 |

### 2.4 用 CMake 集成（推荐）

安装后自带 CMake 包配置，`find_package(ssn)` 一键集成：

```cmake
find_package(ssn REQUIRED)
add_executable(app main.c)
target_link_libraries(app PRIVATE ssn::ssn_transport)
```

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/install
```

## 3. 更进一步

- **文档站**（全文搜索）：<https://turnarond.github.io/cd-ipc-ssn/>
- **完整教程**：`docs/06-使用手册/快速上手.md`、`使用指南.md`、`API使用指南.md`
- **19 个可运行示例**：`examples/`（`bash test/verify_examples.sh` 一键构建验证）
- **C++ 服务框架**：见《SSN C++ 服务框架：一行启动你的 IPC 服务》一文

---

*SSN 是学习型开源项目，对标 DDS 概念逐步演进（DCPS 概念模型计划 v2.6.0）。
欢迎 Star、提 Issue、参与讨论。*
