# CD-IPC-SSN API 使用指南

## 目录

1. [概述](#概述)
2. [地址格式](#地址格式)
3. [服务器API](#服务器api)
4. [客户端API](#客户端api)
5. [消息类型](#消息类型)
6. [回调函数](#回调函数)
7. [错误处理](#错误处理)
8. [示例代码](#示例代码)

## 概述

CD-IPC-SSN 是一个支持多种传输协议的进程间通信库，支持：
- Unix Socket（本地通信）
- TCP（网络通信，可靠传输）
- UDP（网络通信，无连接）

所有API都声明在以下头文件中：
- `ipc_client.h` - 客户端接口
- `ipc_server.h` - 服务器接口
- `ipc_protocol.h` - 协议定义
- `ipc_cliauto.h` - 自动重连客户端

## 地址格式

### 支持的地址格式

| 协议 | 地址格式 | 示例 |
|------|----------|------|
| Unix Socket | `unix:///path/to/socket` 或 `/path/to/socket` | `unix:///tmp/test.sock` 或 `/tmp/test.sock` |
| TCP | `tcp://host:port` | `tcp://127.0.0.1:8080` |
| UDP | `udp://host:port` | `udp://127.0.0.1:9090` |

### 地址格式说明

1. **Unix Socket**
   - 推荐使用 `unix:///path/to/socket` 格式
   - 旧格式 `/path/to/socket` 仍然兼容（自动添加 `unix://` 前缀）

2. **TCP/UDP**
   - 必须使用 `protocol://host:port` 格式
   - `host` 可以是 IP 地址或主机名
   - `port` 必须是有效的端口号 (1-65535)

## 服务器API

### 创建和销毁

```c
// 创建IPC服务器（简单方式）
ipc_server_t *ipc_server_create(const char *server_info);

// 创建IPC服务器（带选项）
ipc_server_t *ipc_server_create_with_options(const char *name, const server_options_t *opts);

// 销毁服务器
void ipc_server_destroy(ipc_server_t *server);
```

### 服务器选项

```c
typedef struct {
    uint64_t send_timeout_ms;      // 发送超时（毫秒）
    uint64_t conn_timeout_ms;      // 连接超时（毫秒）
    uint64_t idle_timeout_sec;     // 空闲超时（秒）
    char ifname[IF_NAMESIZE];      // 网络接口名称（TCP/UDP用）
} server_options_t;
```

### 启动和管理

```c
// 启动服务器监听
bool ipc_server_start(ipc_server_t *server);

// 事件循环
int ipc_server_poll(ipc_server_t *server, int timeout_ms);
void ipc_server_run(ipc_server_t *server);

// 设置回调
void ipc_server_set_connect_handler(ipc_server_t *server,
                                    ipc_on_connect_t oncli, void *arg);
void ipc_server_set_message_handler(ipc_server_t *server,
                                     ipc_server_msg_handler_t callback, void *arg);
```

### RPC处理

```c
// 注册RPC方法
bool ipc_server_add_method(ipc_server_t *server,
                           const ipc_url_ref_t *url,
                           ipc_server_rpc_handler_t callback,
                           void *arg);

// 移除RPC方法
void ipc_server_remove_method(ipc_server_t *server, const ipc_url_ref_t *url);

// 发送RPC响应
int ipc_server_response(ipc_server_t *server, cli_id_t id,
                        uint32_t status, uint16_t seqno,
                        const ipc_data_ref_t *data);
```

### 发布订阅

```c
// 发布消息
int ipc_server_publish(ipc_server_t *server,
                       const ipc_url_ref_t *url,
                       const ipc_data_ref_t *data);

// 检查订阅状态
bool ipc_server_is_subscribed(ipc_server_t *server, const ipc_url_ref_t *url);

// 发送消息
int ipc_server_message(ipc_server_t *server, cli_id_t id,
                       const ipc_url_ref_t *url,
                       const ipc_data_ref_t *data);
```

### 连接管理

```c
// 获取客户端数量
int ipc_server_peer_count(ipc_server_t *server);

// 关闭客户端连接
bool ipc_server_peer_close(ipc_server_t *server, cli_id_t id);

// 获取客户端列表
int ipc_server_peer_list(ipc_server_t *server, cli_id_t ids[], int max_cnt);
```

### 地址获取

```c
// 获取服务器地址
int ipc_server_address(ipc_server_t *server,
                      struct sockaddr *addr,
                      socklen_t *namelen);

// 获取客户端地址
int ipc_server_peer_address(ipc_server_t *server, cli_id_t id,
                           struct sockaddr *addr,
                           socklen_t *namelen);
```

## 客户端API

### 创建和销毁

```c
// 创建IPC客户端
ipc_client_t *ipc_client_create(ipc_client_msg_handler_t onmsg, void *arg);

// 关闭客户端
void ipc_client_close(ipc_client_t *client);
```

### 连接管理

```c
// 连接到服务器
bool ipc_client_connect(ipc_client_t *client,
                        const char *ipc_path,
                        const struct timespec *timeout);

// 断开连接
bool ipc_client_disconnect(ipc_client_t *client);

// 检查连接状态
bool ipc_client_is_connect(ipc_client_t *client);

// 设置发送超时
bool ipc_client_send_timeout(ipc_client_t *client, const int timeout_ms);
```

### RPC调用

```c
// RPC调用
int ipc_client_call(ipc_client_t *client,
                    const ipc_url_ref_t *url,
                    const ipc_data_ref_t *data,
                    ipc_client_rpcreply_handler_t callback,
                    void *arg,
                    uint64_t timeout_ms);
```

### 发布订阅

```c
// 订阅
bool ipc_client_subscribe(ipc_client_t *client,
                          const ipc_url_ref_t *url,
                          ipc_client_result_handler_t callback,
                          void *arg,
                          uint64_t timeout_ms);

// 取消订阅
bool ipc_client_unsubscribe(ipc_client_t *client,
                            const ipc_url_ref_t *url,
                            ipc_client_result_handler_t callback,
                            void *arg,
                            uint64_t timeout_ms);
```

### 消息处理

```c
// 发送消息
int ipc_client_message(ipc_client_t *client,
                       const ipc_url_ref_t *url,
                       const ipc_data_ref_t *data);

// 设置消息回调
void ipc_client_set_on_message(ipc_client_t *client,
                               ipc_client_msg_handler_t callback,
                               void *arg);

// 事件循环
int ipc_client_poll(ipc_client_t *client, uint64_t timeout_ms);
void ipc_client_run(ipc_client_t *client);
```

## 消息类型

### IPC协议支持的消息类型

```c
#define IPC_MSG_TYPE_SERVICE_INFO     0x00  // 服务信息
#define IPC_MSG_TYPE_RPC_REQUEST     0x01  // RPC请求
#define IPC_MSG_TYPE_SUBSCRIBE       0x02  // 订阅
#define IPC_MSG_TYPE_UNSUBSCRIBE     0x03  // 取消订阅
#define IPC_MSG_TYPE_PUBLISH         0x04  // 发布
#define IPC_MSG_TYPE_MESSAGE         0x05  // 普通消息
#define IPC_MSG_TYPE_PING_ECHO       0xFF  // 心跳
```

## 回调函数

### 服务器回调

```c
// 客户端连接/断开回调
typedef void (*ipc_on_connect_t)(ipc_server_t *server,
                                 cli_id_t id,
                                 bool connect,
                                 void *arg);

// 服务器消息回调
typedef void (*ipc_server_msg_handler_t)(ipc_server_t *server,
                                        cli_id_t id,
                                        ipc_url_ref_t *url,
                                        ipc_data_ref_t *data,
                                        void *arg);

// RPC处理回调
typedef void (*ipc_server_rpc_handler_t)(ipc_server_t *server,
                                        cli_id_t id,
                                        ipc_header_t *ipc_hdr,
                                        ipc_url_ref_t *url,
                                        ipc_data_ref_t *data,
                                        void *arg);
```

### 客户端回调

```c
// RPC响应回调
typedef void (*ipc_client_rpcreply_handler_t)(ipc_client_t *client,
                                              ipc_header_t *ipc_hdr,
                                              ipc_data_ref_t *data,
                                              void *arg);

// 操作结果回调（订阅/取消订阅）
typedef void (*ipc_client_result_handler_t)(ipc_client_t *client,
                                            bool success,
                                            void *arg);

// 消息回调（发布/普通消息）
typedef void (*ipc_client_msg_handler_t)(ipc_client_t *client,
                                          ipc_url_ref_t *url,
                                          ipc_data_ref_t *data,
                                          void *arg);
```

## 错误处理

### 错误码

```c
#define IPC_ERR_SUCCESS              0   // 成功
#define IPC_ERR_INVALID_ARGS        1   // 无效参数
#define IPC_ERR_OUT_OF_MEMORY       2   // 内存不足
#define IPC_ERR_NET_CONNECT         3   // 网络连接错误
#define IPC_ERR_NET_READ            4   // 网络读取错误
#define IPC_ERR_NET_WRITE           5   // 网络写入错误
#define IPC_ERR_TIMEOUT             6   // 超时
#define IPC_ERR_NOT_FOUND           7   // 未找到
#define IPC_ERR_ALREADY_EXISTS      8   // 已存在
#define IPC_ERR_PERMISSION_DENIED   9   // 权限拒绝
#define IPC_ERR_PROTOCOL_ERROR      10   // 协议错误
```

### 错误处理函数

```c
void ipc_handle_error(int errcode,
                      const char *file,
                      int line,
                      const char *func,
                      const char *fmt, ...);
```

## 示例代码

### Unix Socket 服务器和客户端

```c
// 服务器
void server_example() {
    ipc_server_t *server = ipc_server_create("/tmp/test.sock");
    ipc_server_set_message_handler(server, on_message, NULL);

    // 注册RPC方法
    ipc_url_ref_t url = {"/test/rpc", 9};
    ipc_server_add_method(server, &url, handle_rpc, NULL);

    ipc_server_start(server);
    ipc_server_run(server);
    ipc_server_destroy(server);
}

// 客户端
void client_example() {
    ipc_client_t *client = ipc_client_create(on_message, NULL);

    struct timespec timeout = {1, 0};
    if (ipc_client_connect(client, "/tmp/test.sock", &timeout)) {
        ipc_data_ref_t data = {"hello", 5};
        ipc_url_ref_t url = {"/test/rpc", 9};

        ipc_client_call(client, &url, &data, on_reply, NULL, 1000);
    }

    ipc_client_close(client);
}
```

### TCP 服务器和客户端

```c
// 服务器
void tcp_server_example() {
    // 使用TCP协议
    ipc_server_t *server = ipc_server_create("tcp://0.0.0.0:8080");
    ipc_server_set_message_handler(server, on_message, NULL);
    ipc_server_start(server);
    ipc_server_run(server);
    ipc_server_destroy(server);
}

// 客户端
void tcp_client_example() {
    ipc_client_t *client = ipc_client_create(on_message, NULL);

    struct timespec timeout = {1, 0};
    // 使用TCP协议连接
    if (ipc_client_connect(client, "tcp://127.0.0.1:8080", &timeout)) {
        ipc_data_ref_t data = {"hello", 5};
        ipc_url_ref_t url = {"/test/rpc", 9};

        ipc_client_call(client, &url, &data, on_reply, NULL, 1000);
    }

    ipc_client_close(client);
}
```

### UDP 服务器和客户端

```c
// 服务器
void udp_server_example() {
    // 使用UDP协议
    ipc_server_t *server = ipc_server_create("udp://0.0.0.0:9090");
    ipc_server_set_message_handler(server, on_message, NULL);
    ipc_server_start(server);
    ipc_server_run(server);
    ipc_server_destroy(server);
}

// 客户端
void udp_client_example() {
    ipc_client_t *client = ipc_client_create(on_message, NULL);

    struct timespec timeout = {1, 0};
    // 使用UDP协议连接
    if (ipc_client_connect(client, "udp://127.0.0.1:9090", &timeout)) {
        ipc_data_ref_t data = {"hello", 5};
        ipc_url_ref_t url = {"/test/rpc", 9};

        ipc_client_call(client, &url, &data, on_reply, NULL, 1000);
    }

    ipc_client_close(client);
}
```

### 发布订阅示例

```c
// 订阅者
void subscriber_example() {
    ipc_client_t *client = ipc_client_create(on_message, NULL);
    ipc_client_connect(client, "/tmp/test.sock", &(struct timespec){1, 0});

    ipc_url_ref_t url = {"/topic/news", 11};
    ipc_client_subscribe(client, &url, on_subscribe, NULL, 1000);

    ipc_client_run(client);
    ipc_client_close(client);
}

// 发布者
void publisher_example() {
    ipc_server_t *server = ipc_server_create("/tmp/test.sock");
    ipc_server_start(server);

    ipc_url_ref_t url = {"/topic/news", 11};
    ipc_data_ref_t data = {"Breaking news!", 14};

    // 发布消息给所有订阅者
    ipc_server_publish(server, &url, &data);
}
```

## 注意事项

1. **线程安全**
   - 大多数函数不是线程安全的，需要在调用时确保同步
   - 建议使用事件循环模型处理并发

2. **超时设置**
   - 超时值建议不小于20毫秒
   - RPC调用超时建议不小于100毫秒

3. **内存管理**
   - URL和数据引用在回调返回后可能无效
   - 需要在回调中复制需要长期使用的数据

4. **地址格式**
   - 优先使用带协议前缀的地址格式
   - Unix Socket路径应使用完整路径

5. **错误处理**
   - 建议检查所有API的返回值
   - 使用 `ipc_handle_error` 记录错误信息
