# cd-ipc-ssn

**版本: 1.0.0**

A high-performance inter-process communication (IPC) library supporting RPC, publish/subscribe, and message passing over Unix domain sockets, TCP, and UDP.

## 目录

- [特性](#特性)
- [版本历史](#版本历史)
- [快速开始](#快速开始)
- [架构概述](#架构概述)
- [API 参考](#api-参考)
- [示例代码](#示例代码)
- [构建说明](#构建说明)
- [测试说明](#测试说明)
- [文档链接](#文档链接)
- [许可证](#许可证)

## 特性

### 核心特性

- **多协议支持**: 支持 Unix Socket、TCP、UDP 三种传输协议
- **统一接口**: 提供标准化的传输层接口 (`ssn_transport_t`)
- **节点抽象**: 提供高级节点抽象层，支持客户端/服务端双重角色
- **RPC 支持**: 支持同步和异步 RPC 调用
- **发布/订阅**: 支持主题订阅和消息发布
- **自动重连**: 支持客户端自动重连机制
- **线程安全**: 完整的线程安全设计
- **事件驱动**: 基于事件驱动的异步 I/O 模型

### 技术规格

- 引用计数和内存管理
- Pending 请求管理 (Bitmap + 数组)
- 线程间同步机制 (Mutex, Event)
- 平台抽象层 (VSI)
- 统一的错误处理体系

## 版本历史

### v1.0.0 (当前版本)

**发布日期**: 2026-04-19

**主要功能**:

- ✅ 传输层抽象 (Unix Socket / TCP / UDP)
- ✅ 节点抽象层 Phase 1 (创建、启动、停止、销毁)
- ✅ 通信接口 (发送消息、发布订阅、RPC调用)
- ✅ 版本管理功能
- ✅ 完整的单元测试

**已知限制**:

- 节点发现功能 (Phase 2 待实现)
- QoS 服务质量支持 (Phase 3 待实现)
- 安全传输 (TLS/DTLS) (后续版本)

### 未来版本计划

- **v1.1.0**: 服务注册与发现
- **v1.2.0**: 节点发现机制
- **v2.0.0**: QoS 支持与安全传输

## 快速开始

### 构建项目

```bash
mkdir build
cd build
cmake ..
make
```

### 运行测试

```bash
# 运行所有测试
ctest

# 运行特定测试
./test_node_basic
./test_transport
```

### 第一个应用

```c
#include "cd_ipc_client.h"
#include "cd_ipc_server.h"
#include "version/ssn_version.h"
#include "transports/ssn_transport.h"

int main(void) {
    // 打印版本信息
    printf("cd-ipc-ssn version: %s\n", ssn_version_get_string());

    // 创建服务器
    ipc_server_t *server = ipc_server_create("/tmp/my_server", NULL);
    ipc_server_start(server);

    // 创建客户端
    ipc_client_t *client = ipc_client_create(message_handler, NULL);
    ipc_client_connect(client, "/tmp/my_server", NULL);

    // 业务逻辑...

    // 清理
    ipc_client_destroy(client);
    ipc_server_destroy(server);

    return 0;
}
```

## 架构概述

```
┌─────────────────────────────────────────────────────────────┐
│                     Application Layer                        │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐│
│  │ IPC Client  │  │ IPC Server  │  │   Node Abstraction  ││
│  │   API       │  │   API       │  │      Layer          ││
│  └─────────────┘  └─────────────┘  └─────────────────────┘│
├─────────────────────────────────────────────────────────────┤
│                     Protocol Layer                          │
│  ┌─────────────────────────────────────────────────────────┐│
│  │                  cd_ipc_protocol                        ││
│  │  (Message encoding/decoding, RPC, PubSub)              ││
│  └─────────────────────────────────────────────────────────┘│
├─────────────────────────────────────────────────────────────┤
│                   Transport Layer                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐│
│  │  ssn_trans  │  │  ssn_trans  │  │    ssn_trans        ││
│  │  port_unix  │  │  port_tcp   │  │    port_udp         ││
│  └─────────────┘  └─────────────┘  └─────────────────────┘│
│  ┌─────────────────────────────────────────────────────────┐│
│  │              ssn_transport_factory                     ││
│  └─────────────────────────────────────────────────────────┘│
├─────────────────────────────────────────────────────────────┤
│                     VSI Layer                               │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐│
│  │ ipc_socket  │  │ ipc_event   │  │    ipc_thread       ││
│  └─────────────┘  └─────────────┘  └─────────────────────┘│
└─────────────────────────────────────────────────────────────┘
```

### 核心组件

| 组件 | 描述 | 位置 |
|------|------|------|
| `ssn_transport_t` | 传输层统一接口 | transports/ |
| `ipc_client_t` | IPC 客户端 | cd_ipc_client.c |
| `ipc_server_t` | IPC 服务端 | cd_ipc_server.c |
| `ipc_node_t` | 节点抽象 | node/ |
| `ipc_protocol_t` | 协议编解码 | cd_ipc_protocol.c |

## API 参考

### 版本 API

```c
// 获取版本信息
const char *ssn_version_get_string(void);  // 返回 "1.0.0"
int ssn_version_get_major(void);           // 返回 1
int ssn_version_get_minor(void);           // 返回 0
int ssn_version_get_patch(void);           // 返回 0

// 版本兼容性检查
bool ssn_version_is_compatible(int major, int minor);
```

### 传输层 API

```c
// 创建传输实例
ssn_transport_t *ssn_transport_create(ssn_transport_type_t type,
                                       const ssn_transport_config_t *config);

// 销毁传输实例
void ssn_transport_destroy(ssn_transport_t *transport);

// 发送数据
int ssn_transport_send(ssn_transport_t *transport, const void *data, size_t len);

// 接收数据
int ssn_transport_recv(ssn_transport_t *transport, void *buf, size_t len);

// 获取文件描述符
int ssn_transport_get_fd(ssn_transport_t *transport);
```

### 节点 API

```c
// 创建节点
ipc_node_t *ipc_node_create(const ipc_node_config_t *config);

// 启动节点
bool ipc_node_start(ipc_node_t *node);

// 停止节点
bool ipc_node_stop(ipc_node_t *node);

// 销毁节点
void ipc_node_destroy(ipc_node_t *node);

// 获取节点状态
ipc_node_state_t ipc_node_get_state(ipc_node_t *node);

// RPC 调用
int ipc_node_rpc_call(ipc_node_t *node, const char *peer_address,
                      const ipc_url_ref_t *url, const ipc_data_ref_t *data,
                      ipc_client_rpcreply_handler_t callback, void *arg,
                      uint64_t timeout_ms);

// 发布消息
bool ipc_node_publish(ipc_node_t *node, const ipc_url_ref_t *url,
                     const ipc_data_ref_t *data);

// 订阅主题
bool ipc_node_subscribe(ipc_node_t *node, const ipc_url_ref_t *url,
                       ipc_client_msg_handler_t callback, void *arg,
                       uint64_t timeout_ms);
```

## 示例代码

我们提供了丰富的示例代码，展示了库的各种功能和使用场景。所有示例都位于 `examples` 目录中，按照功能和复杂度进行了分类：

### 基础示例

- **01_hello_world** - 基础的客户端-服务器通信
- **02_rpc_call** - RPC 功能演示
- **03_pubsub** - 发布/订阅模式
- **04_node_basic** - 节点抽象层基础

### 高级示例

- **01_multithread** - 多线程 IPC
- **02_error_handling** - 错误处理
- **03_timeout** - 超时管理
- **04_transport_selection** - 传输协议选择

### 协议示例

- **01_unix_socket** - Unix Socket 使用
- **02_tcp** - TCP 使用
- **03_udp** - UDP 使用

### 节点示例

- **01_node_lifecycle** - 节点生命周期管理
- **02_node_comm** - 节点间通信
- **03_node_rpc** - 节点 RPC 功能
- **04_node_pubsub** - 节点发布/订阅

### 运行示例

```bash
# 构建所有示例
./examples/utils/build_examples.sh

# 运行示例
./examples/utils/run_examples.sh

# 运行特定示例
cd examples/basic/01_hello_world
make run
```

## 构建说明

### 环境要求

- CMake >= 3.12
- GCC >= 4.8 或 CLang >= 3.0
- POSIX 兼容系统 (Linux, macOS)

### 构建选项

```bash
# 默认构建
cmake ..
make

# 详细构建输出
cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON

# 安装到指定目录
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
make install
```

### 交叉编译

```bash
# ARM 架构
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchains/arm-linux-gnueabihf.cmake
```

## 测试说明

### 运行测试

```bash
# 进入构建目录
cd build

# 运行所有测试
ctest -V

# 运行特定测试
./test_node_basic
./test_transport
./test_transport_interface
```

### 测试列表

| 测试名称 | 描述 | 状态 |
|----------|------|------|
| test_node_basic | 节点基本功能测试 | ✅ 通过 |
| test_node | 节点完整功能测试 | ⏳ 待完善 |
| test_transport | 传输层测试 | ✅ 通过 |
| test_transport_interface | 传输接口测试 | ✅ 通过 |

### 编写新测试

```c
#include <stdio.h>
#include "cd_ipc_client.h"
#include "util/ssn_log.h"

int main(void) {
    ssn_log_set_level(SSN_LOG_LEVEL_INFO);

    // 测试代码
    LOG_INFO("Test passed!");

    return 0;
}
```

## 文档链接

- [架构设计文档](docs/architecture_overview.md)
- [协议适配层设计](docs/protocol_adapter_design.md)
- [节点抽象设计](docs/node_abstraction_design.md)
- [节点发现设计](docs/node_discovery_design.md)
- [迁移指南](docs/migration_guide.md)

## 许可证

本项目采用 MIT 许可证。详见 [LICENSE](LICENSE) 文件。

## 联系方式

- 项目主页: https://github.com/acoinfo/edge-framework
- 问题反馈: https://github.com/acoinfo/edge-framework/issues

## 致谢

感谢所有为该项目做出贡献的开发者。
