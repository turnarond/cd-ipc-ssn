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
| 工程完备 | 16 套件 694 例测试全绿、`find_package(ssn)` 包配置、GitHub Actions CI、docsify 文档站 |

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

```c
#include <stdio.h>
#include <pthread.h>
#include "node/ssn_node.h"
#include "version/ssn_version.h"

static void on_msg(ssn_client_t *cli, ssn_url_ref_t *url,
                   ssn_data_ref_t *data, void *arg) {
    (void)cli; (void)url; (void)arg;
    printf("Received: %.*s\n", (int)data->length, (char*)data->data);
}

/* 服务端节点的事件循环必须在独立线程中驱动（poll 处理连接握手/订阅/消息分发） */
static volatile int g_srv_running = 1;
static void *srv_poll_thread(void *arg) {
    ssn_node_t *srv = (ssn_node_t *)arg;
    while (g_srv_running) ssn_node_poll(srv, 100);
    return NULL;
}

int main(void) {
    printf("ssn version: %s\n", ssn_version_get_string());

    // 创建并启动服务端节点
    ssn_node_config_t srv_cfg = {
        .node_type = "server", .node_name = "demo-server",
        .listen_address = "127.0.0.1", .listen_port = 8888,
        .capabilities = SSN_NODE_CAP_SERVER | SSN_NODE_CAP_PUBSUB
    };
    ssn_node_t *srv = ssn_node_create(&srv_cfg);
    ssn_node_start(srv);
    pthread_t srv_tid;
    pthread_create(&srv_tid, NULL, srv_poll_thread, srv);

    // 创建并启动客户端节点
    ssn_node_config_t cli_cfg = {
        .node_type = "client", .node_name = "demo-client",
        .capabilities = SSN_NODE_CAP_CLIENT | SSN_NODE_CAP_PUBSUB
    };
    ssn_node_t *cli = ssn_node_create(&cli_cfg);
    ssn_node_start(cli);

    // 订阅主题
    ssn_url_ref_t topic = { .url = "/demo", .url_len = 5 };
    ssn_node_subscribe(cli, "tcp://127.0.0.1:8888", &topic, on_msg, NULL, 5000);

    // 发布前先 poll 服务端：让订阅握手在服务端生效
    // （subscribe 只发出请求，服务端需 poll 处理后订阅才建立，否则首条消息会丢）
    ssn_node_poll(srv, 100);

    // 发布消息
    ssn_data_ref_t msg = { .data = "hello", .length = 5 };
    ssn_node_publish(srv, &topic, &msg);

    // 轮询接收（客户端节点的事件循环也由 poll 驱动）
    for (int i = 0; i < 10; i++) {
        ssn_node_poll(cli, 100);
    }

    ssn_node_stop(cli); ssn_node_destroy(cli);
    g_srv_running = 0; pthread_join(srv_tid, NULL);
    ssn_node_stop(srv); ssn_node_destroy(srv);
    return 0;
}
```

编译运行：

```bash
gcc -std=c99 -Wall -I src -o demo demo.c -L build -lssn_transport -lpthread \
    -Wl,-rpath,$PWD/build
./demo
```

输出：

```
ssn version: 2.5.1
Received: hello
```

> **关键认知**：SSN 是**事件循环驱动**的——必须周期性调用 `ssn_node_poll` /
> `ssn_client_poll` / `ssn_server_poll`（或在独立线程中轮询），连接握手、消息收发
> 与回调才会发生。这是新手最容易踩的坑，也是本框架与「自带后台线程」的框架最大的不同。

### 2.3 三种通信模式速览

| 模式 | 关键 API | 场景 |
|------|---------|------|
| RPC | `ssn_node_rpc_call` / `ssn_node_add_rpc_method` | 请求-应答，如查询设备状态 |
| PubSub | `ssn_node_publish` / `ssn_node_subscribe` | 一对多广播，如数据分发 |
| 消息 | `ssn_node_send_to_peer` | 定向发送，如指令下发 |

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
