# CD-IPC-SSN 迁移指南

## 从 v1.0 迁移到 v2.0

本指南帮助您将代码从仅支持 Unix Socket 的旧版本迁移到支持多协议（Unix Socket、TCP、UDP）的新版本。

## 主要变化

### 1. 地址格式变化

#### 旧版本（v1.0）
```c
// 服务器
ipc_server_create("/tmp/test.sock");

// 客户端
ipc_client_connect(client, "/tmp/test.sock", &timeout);
```

#### 新版本（v2.0）
```c
// Unix Socket - 方式1（推荐）
ipc_server_create("unix:///tmp/test.sock");
ipc_client_connect(client, "unix:///tmp/test.sock", &timeout);

// Unix Socket - 方式2（兼容旧版本）
ipc_server_create("/tmp/test.sock");  // 自动添加 unix:// 前缀
ipc_client_connect(client, "/tmp/test.sock", &timeout);

// TCP - 新增
ipc_server_create("tcp://0.0.0.0:8080");
ipc_client_connect(client, "tcp://127.0.0.1:8080", &timeout);

// UDP - 新增
ipc_server_create("udp://0.0.0.0:9090");
ipc_client_connect(client, "udp://127.0.0.1:9090", &timeout);
```

### 2. 传输层接口变化

#### 旧版本（v1.0）
直接使用 socket 描述符进行操作：
```c
int sock = socket(AF_UNIX, SOCK_STREAM, 0);
bind(sock, (struct sockaddr *)&addr, sizeof(addr));
listen(sock, 5);
```

#### 新版本（v2.0）
使用统一的传输层接口：
```c
ssn_transport_t *transport = ssn_transport_create(SSN_TRANSPORT_UNIX, &config);
ssn_transport_bind(transport, &addr);
ssn_transport_listen(transport, 5);
```

### 3. 发送消息接口变化

#### 旧版本（v1.0）
```c
bool ipc_send_message(int sock, ipc_header_t *ipc_hdr,
                     const ipc_url_ref_t *url,
                     const ipc_data_ref_t *data);
```

#### 新版本（v2.0）
```c
bool ipc_send_message(ssn_transport_t *transport,
                     ipc_header_t *ipc_hdr,
                     const ipc_url_ref_t *url,
                     const ipc_data_ref_t *data);
```

## 迁移步骤

### 步骤 1：更新头文件

确保包含正确的头文件：
```c
#include "cd_ipc_client.h"
#include "cd_ipc_server.h"
#include "cd_ipc_protocol.h"
#include "transports/ssn_transport.h"  // 新增
```

### 步骤 2：更新地址格式

**选项 A：使用带协议前缀的地址（推荐）**

服务器：
```c
// 旧代码
ipc_server_t *server = ipc_server_create("/tmp/test.sock");

// 新代码
ipc_server_t *server = ipc_server_create("unix:///tmp/test.sock");
```

客户端：
```c
// 旧代码
ipc_client_connect(client, "/tmp/test.sock", &timeout);

// 新代码
ipc_client_connect(client, "unix:///tmp/test.sock", &timeout);
```

**选项 B：保持旧地址格式（向后兼容）**

如果使用不带协议前缀的地址，系统会自动识别为 Unix Socket：
```c
// 仍然有效，但建议更新
ipc_server_create("/tmp/test.sock");
ipc_client_connect(client, "/tmp/test.sock", &timeout);
```

### 步骤 3：更新网络相关代码

如果使用了 TCP 或 UDP，需要更新连接代码：

```c
// TCP 示例
ipc_server_t *server = ipc_server_create("tcp://0.0.0.0:8080");
ipc_client_connect(client, "tcp://127.0.0.1:8080", &timeout);

// UDP 示例
ipc_server_t *server = ipc_server_create("udp://0.0.0.0:9090");
ipc_client_connect(client, "udp://127.0.0.1:9090", &timeout);
```

### 步骤 4：处理多协议共存

如果需要同时监听多个协议，可以使用多个服务器实例：

```c
// 同时监听 Unix Socket 和 TCP
ipc_server_t *unix_server = ipc_server_create("unix:///tmp/test.sock");
ipc_server_t *tcp_server = ipc_server_create("tcp://0.0.0.0:8080");

// 设置相同的回调
ipc_server_set_message_handler(unix_server, on_message, NULL);
ipc_server_set_message_handler(tcp_server, on_message, NULL);
```

## 兼容性说明

### 向后兼容

1. **地址格式兼容**
   - 不带协议前缀的地址自动识别为 Unix Socket
   - 现有代码无需修改即可运行

2. **API 兼容**
   - 大多数 API 保持不变
   - 新的 API 是可选的，用于增强功能

### 协议选择建议

| 场景 | 推荐协议 | 说明 |
|------|----------|------|
| 本地进程间通信 | Unix Socket | 最高性能，最低延迟 |
| 同一网络内通信 | TCP | 可靠传输，保证顺序 |
| 实时性要求高 | UDP | 低延迟，但不保证可靠性 |
| 跨网络通信 | TCP | 可靠传输 |

## 常见问题

### Q1：旧代码还能工作吗？
A：是的，只要地址格式不变，旧代码可以继续工作。但建议更新为带协议前缀的格式，以获得更好的可读性和未来兼容性。

### Q2：如何选择协议？
A：根据您的需求选择：
- 本地通信：使用 Unix Socket
- 需要可靠传输：使用 TCP
- 需要低延迟：使用 UDP

### Q3：TCP 和 UDP 能混用吗？
A：不能。客户端和服务器必须使用相同的协议。例如，TCP 客户端只能连接 TCP 服务器。

### Q4：如何调试协议相关问题？
A：检查以下几点：
1. 确认服务器和客户端使用相同的协议
2. 确认地址格式正确（带协议前缀）
3. 检查防火墙设置（对于 TCP/UDP）
4. 确认端口未被占用

## 示例代码对比

### Unix Socket 服务器

**旧版本：**
```c
ipc_server_t *server = ipc_server_create("/tmp/test.sock");
ipc_server_start(server);
```

**新版本：**
```c
ipc_server_t *server = ipc_server_create("unix:///tmp/test.sock");
// 或者
ipc_server_t *server = ipc_server_create("/tmp/test.sock");  // 仍然兼容
ipc_server_start(server);
```

### TCP 客户端

**旧版本：**
```c
// 不支持
```

**新版本：**
```c
struct timespec timeout = {1, 0};
if (ipc_client_connect(client, "tcp://127.0.0.1:8080", &timeout)) {
    // 连接成功
}
```

### UDP 发布订阅

**新版本：**
```c
// 服务器
ipc_server_t *server = ipc_server_create("udp://0.0.0.0:9090");
ipc_server_start(server);

ipc_url_ref_t url = {"/topic/news", 11};
ipc_data_ref_t data = {"news content", 12};
ipc_server_publish(server, &url, &data);

// 客户端
ipc_client_t *client = ipc_client_create(on_message, NULL);
ipc_client_connect(client, "udp://127.0.0.1:9090", &(struct timespec){1, 0});

ipc_url_ref_t sub_url = {"/topic/news", 11};
ipc_client_subscribe(client, &sub_url, NULL, NULL, 1000);
```

## 升级检查清单

迁移前请确认：

- [ ] 更新了地址格式为带协议前缀的格式
- [ ] 包含了必要的头文件
- [ ] 测试了所有连接场景
- [ ] 验证了消息发送和接收
- [ ] 测试了 RPC 功能（如果使用）
- [ ] 测试了发布订阅功能（如果使用）
- [ ] 检查了错误日志

## 技术支持

如果迁移过程中遇到问题，请检查：

1. 编译错误：确认头文件路径正确
2. 连接失败：确认地址格式和协议匹配
3. 消息丢失：检查超时设置和网络状态
