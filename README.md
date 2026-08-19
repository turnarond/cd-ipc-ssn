# ssn (cd-ipc-ssn)

**版本: 2.4.4**

SSN (Scalable Socket Network) is a lightweight inter-process communication (IPC) framework supporting RPC, publish/subscribe, and message passing over Unix domain sockets, TCP, and UDP. Features a layered architecture with node abstraction, protocol modularization, and platform abstraction (VSI).

> 📖 **在线文档**：<https://turnarond.github.io/cd-ipc-ssn/>（docsify 文档站，含全文搜索；源码见 `docs/`）

## 目录

- [快速开始](#快速开始)
- [架构概述](#架构概述)
- [API 参考](#api-参考)
- [构建说明](#构建说明)
- [测试说明](#测试说明)
- [版本历史](#版本历史)
- [文档链接](#文档链接)

## 快速开始

### 构建

```bash
mkdir build && cd build
cmake .. && make -j$(nproc)
```

### 运行测试

```bash
bash test/run_tests.sh        # 一键：构建 + 全部 14 个自动化套件
# 或构建后逐个运行：
./test_transport                # 传输层测试 (67 断言)
./test_node_basic               # 节点基础测试 (3 用例)
./test_node                     # 节点完整测试 (6 用例)
./test_protocol                 # 协议层测试 (25 断言)
./test_protocol_integration     # 协议集成测试 (19 用例)
```

### 第一个应用

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

    // 发布消息（订阅已由 subscribe 同步建立）
    ssn_data_ref_t msg = { .data = "hello", .length = 5 };
    ssn_node_publish(srv, &topic, &msg);

    // 轮询接收（客户端节点的事件循环也由 poll 驱动；无事件时每次阻塞至多 100ms）
    for (int i = 0; i < 10; i++) {
        ssn_node_poll(cli, 100);
    }

    ssn_node_stop(cli); ssn_node_destroy(cli);
    g_srv_running = 0; pthread_join(srv_tid, NULL);
    ssn_node_stop(srv); ssn_node_destroy(srv);
    return 0;
}
```

编译运行（产物为 `libssn_transport.so`，位于 `build/`）：

```bash
gcc -std=c99 -Wall -I src -o demo demo.c -L build -lssn_transport -lpthread \
    -Wl,-rpath,$PWD/build
./demo
```

> 提示：节点（`ssn_node_*`）与客户端/服务端（`ssn_client_*` / `ssn_server_*`）均为
> **事件循环驱动**——必须周期性调用 `ssn_node_poll` / `ssn_client_poll` /
> `ssn_server_poll`（或在独立线程中轮询），连接握手、消息收发与回调才会发生。
> 完整可运行示例见 `examples/`（`bash test/verify_examples.sh` 可构建全部 19 个）。

### C++ 服务框架（v2.4.0）

面向「快速开发 IPC 服务」的 C++ 封装（`libssn_framework.so`，C++17）：类型安全的
RPC 方法注册与调用（`RegisterMethod<Req,Resp>` / `Call<Req,Resp>`）、内置管理端点
（`/urls` `/health` `/version`）、PubSub 发布/订阅，`ServiceManager::Run<T>()` 一行
启动服务（信号优雅停止）。示例：`examples/cpp/01_echo_service`（echo 服务）、
`examples/cpp/02_pubsub_chat`（聊天室）。完整教学文档见
[C++ 服务框架指南](docs/06-使用手册/C++服务框架指南.md)。

## 架构概述

```
┌──────────────────────────────────────────────────────────────┐
│                    Application Layer                          │
├──────────────────────────────────────────────────────────────┤
│  ┌──────────────────────────────────────────────────────────┐│
│  │               Node Abstraction Layer                     ││
│  │   ssn_node_t (server + client dual-role capability)      ││
│  └──────────────────────────────────────────────────────────┘│
├──────────────────────────────────────────────────────────────┤
│  ┌─────────────────┐  ┌────────────────────────────────────┐ │
│  │   ssn_client    │  │          ssn_server                │ │
│  └─────────────────┘  └────────────────────────────────────┘ │
├──────────────────────────────────────────────────────────────┤
│                     Protocol Layer (Modular)                  │
│  ┌───────────┐  ┌────────────┐  ┌─────────────┐             │
│  │ ssn_rpc   │  │ ssn_pubsub │  │  ssn_msg    │             │
│  │ (req/rep) │  │ (pub/sub)  │  │ (send/recv) │             │
│  └───────────┘  └────────────┘  └─────────────┘             │
│  ┌──────────────────────────────────────────────────────────┐│
│  │              ssn_frame (wire protocol)                   ││
│  └──────────────────────────────────────────────────────────┘│
├──────────────────────────────────────────────────────────────┤
│                    Transport Layer                            │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐                │
│  │ ssn_trans │  │ ssn_trans │  │ ssn_trans │                │
│  │ port_unix │  │ port_tcp  │  │ port_udp  │                │
│  └───────────┘  └───────────┘  └───────────┘                │
│  ┌──────────────────────────────────────────────────────────┐│
│  │              ssn_transport_factory                       ││
│  └──────────────────────────────────────────────────────────┘│
├──────────────────────────────────────────────────────────────┤
│                     VSI Layer (internal)                      │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐                │
│  │ ipc_socket│  │ ipc_event │  │ ipc_thread │                │
│  └───────────┘  └───────────┘  └───────────┘                │
└──────────────────────────────────────────────────────────────┘
```

### 核心组件

| 组件 | 类型 | 文件 |
|------|------|------|
| `ssn_node_t` | 节点抽象（服务端+客户端双角色） | `src/node/ssn_node.c` |
| `ssn_client_t` | 客户端 | `src/ssn_client.c` |
| `ssn_server_t` | 服务端 | `src/ssn_server.c` |
| `ssn_transport_t` | 传输层统一接口 | `src/transports/` |
| `ssn_rpc_req_t` / `ssn_rpc_rep_t` | RPC 协议模块 | `src/protocol/rpc/` |
| `ssn_pubsub_pub_t` / `ssn_pubsub_sub_t` | 发布/订阅协议模块 | `src/protocol/pubsub/` |
| `ssn_msg_send_t` / `ssn_msg_recv_t` | 消息协议模块 | `src/protocol/msg/` |
| `ssn_header_t` | 线协议编解码 | `src/ssn_frame.c` |

### 命名约定

- 所有公开符号使用 `ssn_` 前缀
- VSI 平台抽象层（内部）保留 `ipc_` 前缀
- 类型名: `ssn_<module>_t` (如 `ssn_client_t`, `ssn_transport_t`)
- 函数名: `ssn_<module>_<action>` (如 `ssn_node_create`, `ssn_client_connect`)
- 宏: `SSN_<UPPER_CASE>` (如 `SSN_MAX_PACKET_SIZE`, `SSN_MSG_TYPE_PUBLISH`)

## API 参考

### 节点层 API

```c
// 生命周期
ssn_node_t *ssn_node_create(const ssn_node_config_t *config);
bool ssn_node_start(ssn_node_t *node);
bool ssn_node_stop(ssn_node_t *node);
void ssn_node_destroy(ssn_node_t *node);

// 通信
int ssn_node_rpc_call(ssn_node_t *node, const char *peer_address,
                      const ssn_url_ref_t *url, const ssn_data_ref_t *data,
                      ssn_client_rpcreply_handler_t callback, void *arg,
                      uint64_t timeout_ms);
bool ssn_node_publish(ssn_node_t *node, const ssn_url_ref_t *url,
                      const ssn_data_ref_t *data);
bool ssn_node_subscribe(ssn_node_t *node, const char *peer_address,
                        const ssn_url_ref_t *url,
                        ssn_client_msg_handler_t callback, void *arg,
                        uint64_t timeout_ms);
bool ssn_node_send_to_peer(ssn_node_t *node, const char *peer_address,
                           const ssn_url_ref_t *url, const ssn_data_ref_t *data);
int ssn_node_poll(ssn_node_t *node, uint64_t timeout_ms);

// RPC 方法注册（服务端）
bool ssn_node_add_rpc_method(ssn_node_t *node, const ssn_url_ref_t *url,
                             ssn_server_rpc_handler_t callback, void *arg);

// 统计
bool ssn_node_get_stats(ssn_node_t *node, int *active_connections,
                        uint64_t *total_messages);
```

### 传输层 API

```c
ssn_transport_t *ssn_transport_create(ssn_transport_type_t type,
                                      const ssn_transport_config_t *config);
void ssn_transport_destroy(ssn_transport_t *transport);
int ssn_transport_send(ssn_transport_t *transport, const void *data, size_t len);
int ssn_transport_recv(ssn_transport_t *transport, void *buf, size_t len, int timeout_ms);
int ssn_transport_get_fd(const ssn_transport_t *transport);
bool ssn_transport_connect(ssn_transport_t *transport, const ssn_address_t *addr, int timeout_ms);
bool ssn_transport_bind(ssn_transport_t *transport, const ssn_address_t *addr);
bool ssn_transport_listen(ssn_transport_t *transport, int backlog);
ssn_transport_t *ssn_transport_accept(ssn_transport_t *transport, ssn_address_t *client_addr, int timeout_ms);
```

### 地址格式

```
tcp://127.0.0.1:8888        # TCP
unix:///tmp/my_server       # Unix Domain Socket
udp://127.0.0.1:9999        # UDP
```

## 构建说明

### 环境要求

- CMake >= 3.12
- C 库：GCC >= 4.8 或 Clang >= 3.0（C99）
- C++ 服务框架（`libssn_framework`）：GCC >= 7 或 Clang >= 6（C++17）
- POSIX 兼容系统 (Linux)

### 构建步骤

```bash
mkdir build && cd build
cmake .. && make -j$(nproc)
```

产物: `libssn_transport.so`（C 库）+ `libssn_framework.so`（C++ 服务框架）

## 测试说明

### 测试套件

| 测试 | 描述 | 用例数 |
|------|------|--------|
| `test_transport` | 传输层完整测试（创建、连接、收发、工厂、IPv6） | 67 |
| `test_node_basic` | 节点基础生命周期 | 3 |
| `test_node` | 节点完整功能（创建、启停、PubSub、RPC、统计） | 6 |
| `test_protocol` | 协议层单元测试（创建、类型、角色、绑定） | 25 |
| `test_protocol_integration` | 协议层集成测试（RPC、PubSub、Msg 全链路） | 19 |
| `example_server` | 服务端 API 功能测试（创建、启停、RPC、idle 超时） | 8 |
| `example_client` | 客户端 API 功能测试（连接、RPC、订阅、消息、慢握手） | 12 |
| `test_cpp_*` | C++ 服务框架 7 套件（生命周期、线程池、Run 编排、服务/客户端、DTO、稳定性） | 485 |

**合计：自动化 14 套件 625 例**，另有 3 个手工套件（需自行启动服务端）与
19 个示例构建验证（`bash test/verify_examples.sh`，含 hello_world 运行冒烟）。

### 运行

```bash
# 一键：构建 + 全部 14 个自动化套件（位置无关）
bash test/run_tests.sh

# 或构建后逐个运行
cd build
./test_transport && ./test_node_basic && ./test_node \
  && ./test_protocol && ./test_protocol_integration \
  && ./example_server && ./example_client && ./test_cpp_*
```

## 版本历史

详见 [CHANGELOG.md](CHANGELOG.md)

| 版本 | 日期 | 主要变更 |
|------|------|----------|
| 2.4.4 | 2026-08-19 | 用户旅程/线程安全/传输层/协议层/C++ 框架 P0 修复 |
| 2.4.3 | 2026-08-19 | transport 构造 fd 泄漏修复（Issue #10） |
| 2.4.2 | 2026-08-18 | 稳定性加固：回调异常保护、并发互斥、svc 失败可观测、稳定性测试套件 |
| 2.4.1 | 2026-08-16 | 技术债 12 项集中修复（Issue #5 闭环） |
| 2.4.0 | 2026-08-16 | C++ 服务框架（libssn_framework）、Issue #4 空连接挂死修复 |
| 2.3.2 | 2026-08-08 | 使用手册/示例全面修正、定时器线程与分片帧接收修复 |
| 2.3.1 | 2026-08-06 | 稳定化：node 自锁、IPv6 截断、poll 毫秒换算等修复；需求分析与部署手册补写 |
| 2.3.0 | 2026-05-07 | driver-sdk 稳定性与完备性升级（状态机、数据管道、诊断收集） |
| 2.2.0 | 2026-05-06 | client/server API 自动化测试、ssn_cliauto 适配、EAGAIN 修复、文档全面清理 |
| 2.1.0 | 2026-04-29 | 统一 ssn_ 命名、client/server 重构集成协议层、修复多项 bug、补全测试 |
| 2.0.0 | 2026-04-21 | 节点抽象、多协议支持 |
| 1.0.0 | 2026-04-19 | 传输层抽象、节点抽象 Phase 1、通信 API |

## 文档链接

- [架构白皮书](docs/01-白皮书/架构白皮书.md)
- [文档中心索引](docs/README.md)
- [架构设计总览](docs/03-设计/架构设计/架构设计总览.md)
- [传输层设计](docs/03-设计/架构设计/传输层设计.md)
- [协议层模块化设计](docs/03-设计/核心模块/协议层模块化设计.md)
- [C++ 服务框架指南](docs/06-使用手册/C++服务框架指南.md)
- [API 使用指南](docs/06-使用手册/API使用指南.md)
- [使用指南](docs/06-使用手册/使用指南.md)
- [迁移指南](docs/04-实施规划/迁移指南.md)
- [测试方案](docs/07-测试方案/协议层集成测试方案.md)

## 许可证

MIT License
