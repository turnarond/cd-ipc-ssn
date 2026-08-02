# SSN API 使用指南

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

SSN 是一个支持多种传输协议的进程间通信框架，支持：
- Unix Socket（本地通信）
- TCP（网络通信，可靠传输）
- UDP（网络通信，无连接）

所有API都声明在以下头文件中：
- `ssn_client.h` - 客户端接口
- `ssn_server.h` - 服务器接口
- `ssn_frame.h` - 线协议定义
- `ssn_cliauto.h` - 自动重连客户端
- `node/ssn_node.h` - 节点抽象层接口

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
ssn_server_t *ssn_server_create(const char *server_info);

// 创建IPC服务器（带选项）
ssn_server_t *ssn_server_create_with_options(const char *name, const server_options_t *opts);

// 销毁服务器
void ssn_server_destroy(ssn_server_t *server);
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
bool ssn_server_start(ssn_server_t *server);

// 事件循环
int ssn_server_poll(ssn_server_t *server, int timeout_ms);
void ssn_server_run(ssn_server_t *server);

// 设置回调
void ssn_server_set_on_connect(ssn_server_t *server,
                                    ssn_on_connect_t oncli, void *arg);
void ssn_server_set_on_message(ssn_server_t *server,
                                     ssn_server_msg_handler_t callback, void *arg);
```

### RPC处理

```c
// 注册RPC方法
bool ssn_server_add_method(ssn_server_t *server,
                           const ssn_url_ref_t *url,
                           ssn_server_rpc_handler_t callback,
                           void *arg);

// 移除RPC方法
void ssn_server_remove_method(ssn_server_t *server, const ssn_url_ref_t *url);

// 发送RPC响应
int ssn_server_response(ssn_server_t *server, ssn_peer_id_t id,
                        uint32_t status, uint16_t seqno,
                        const ssn_data_ref_t *data);
```

### 发布订阅

```c
// 发布消息
int ssn_server_publish(ssn_server_t *server,
                       const ssn_url_ref_t *url,
                       const ssn_data_ref_t *data);

// 检查订阅状态
bool ssn_server_is_subscribed(ssn_server_t *server, const ssn_url_ref_t *url);

// 发送消息
int ssn_server_message(ssn_server_t *server, ssn_peer_id_t id,
                       const ssn_url_ref_t *url,
                       const ssn_data_ref_t *data);
```

### 连接管理

```c
// 获取客户端数量
int ssn_server_peer_count(ssn_server_t *server);

// 关闭客户端连接
bool ssn_server_peer_close(ssn_server_t *server, ssn_peer_id_t id);

// 获取客户端列表
int ssn_server_peer_list(ssn_server_t *server, ssn_peer_id_t ids[], int max_cnt);
```

### 地址获取

```c
// 获取服务器地址
int ssn_server_address(ssn_server_t *server,
                      struct sockaddr *addr,
                      socklen_t *namelen);

// 获取客户端地址
int ssn_server_peer_address(ssn_server_t *server, ssn_peer_id_t id,
                           struct sockaddr *addr,
                           socklen_t *namelen);
```

## 客户端API

### 创建和销毁

```c
// 创建IPC客户端
ssn_client_t *ssn_client_create(ssn_client_msg_handler_t onmsg, void *arg);

// 关闭客户端
void ssn_client_close(ssn_client_t *client);
```

### 连接管理

```c
// 连接到服务器
bool ssn_client_connect(ssn_client_t *client,
                        const char *address,
                        const struct timespec *timeout);

// 断开连接
bool ssn_client_disconnect(ssn_client_t *client);

// 检查连接状态
bool ssn_client_is_connect(ssn_client_t *client);

// 设置发送超时
bool ssn_client_send_timeout(ssn_client_t *client, const int timeout_ms);
```

### RPC调用

```c
// RPC调用
int ssn_client_call(ssn_client_t *client,
                    const ssn_url_ref_t *url,
                    const ssn_data_ref_t *data,
                    ssn_client_rpcreply_handler_t callback,
                    void *arg,
                    uint64_t timeout_ms);
```

### 发布订阅

```c
// 订阅
bool ssn_client_subscribe(ssn_client_t *client,
                          const ssn_url_ref_t *url,
                          ssn_client_result_handler_t callback,
                          void *arg,
                          uint64_t timeout_ms);

// 取消订阅
bool ssn_client_unsubscribe(ssn_client_t *client,
                            const ssn_url_ref_t *url,
                            ssn_client_result_handler_t callback,
                            void *arg,
                            uint64_t timeout_ms);
```

### 消息处理

```c
// 发送消息
int ssn_client_message(ssn_client_t *client,
                       const ssn_url_ref_t *url,
                       const ssn_data_ref_t *data);

// 设置消息回调
void ssn_client_set_on_message(ssn_client_t *client,
                               ssn_client_msg_handler_t callback,
                               void *arg);

// 事件循环
int ssn_client_poll(ssn_client_t *client, uint64_t timeout_ms);
void ssn_client_run(ssn_client_t *client);
```

## 消息类型

### IPC协议支持的消息类型

```c
#define SSN_MSG_TYPE_SERVICE_INFO     0x00  // 服务信息
#define SSN_MSG_TYPE_RPC_REQUEST     0x01  // RPC请求
#define SSN_MSG_TYPE_SUBSCRIBE       0x02  // 订阅
#define SSN_MSG_TYPE_UNSUBSCRIBE     0x03  // 取消订阅
#define SSN_MSG_TYPE_PUBLISH         0x04  // 发布
#define SSN_MSG_TYPE_MESSAGE         0x05  // 普通消息
#define SSN_MSG_TYPE_PING_ECHO       0xFF  // 心跳
```

## 回调函数

### 服务器回调

```c
// 客户端连接/断开回调
typedef void (*ssn_on_connect_t)(ssn_server_t *server,
                                 ssn_peer_id_t id,
                                 bool connect,
                                 void *arg);

// 服务器消息回调
typedef void (*ssn_server_msg_handler_t)(ssn_server_t *server,
                                        ssn_peer_id_t id,
                                        ssn_url_ref_t *url,
                                        ssn_data_ref_t *data,
                                        void *arg);

// RPC处理回调
typedef void (*ssn_server_rpc_handler_t)(ssn_server_t *server,
                                        ssn_peer_id_t id,
                                        ssn_header_t *ssn_hdr,
                                        ssn_url_ref_t *url,
                                        ssn_data_ref_t *data,
                                        void *arg);
```

### 客户端回调

```c
// RPC响应回调
typedef void (*ssn_client_rpcreply_handler_t)(ssn_client_t *client,
                                              ssn_header_t *ssn_hdr,
                                              ssn_data_ref_t *data,
                                              void *arg);

// 操作结果回调（订阅/取消订阅）
typedef void (*ssn_client_result_handler_t)(ssn_client_t *client,
                                            bool success,
                                            void *arg);

// 消息回调（发布/普通消息）
typedef void (*ssn_client_msg_handler_t)(ssn_client_t *client,
                                          ssn_url_ref_t *url,
                                          ssn_data_ref_t *data,
                                          void *arg);
```

## 错误处理

### 错误码

```c
#define SSN_ECODE_SUCCESS              0   // 成功
#define SSN_ECODE_INVALID_ARGS        1   // 无效参数
#define SSN_ECODE_OUT_OF_MEMORY       2   // 内存不足
#define SSN_ECODE_NET_CONNECT         3   // 网络连接错误
#define SSN_ECODE_NET_READ            4   // 网络读取错误
#define SSN_ECODE_NET_WRITE           5   // 网络写入错误
#define SSN_ECODE_TIMEOUT             6   // 超时
#define SSN_ECODE_NOT_FOUND           7   // 未找到
#define SSN_ECODE_ALREADY_EXISTS      8   // 已存在
#define SSN_ECODE_PERMISSION_DENIED   9   // 权限拒绝
#define SSN_ECODE_PROTOCOL_ERROR      10   // 协议错误
```

### 错误处理函数

```c
void ssn_handle_error(int errcode,
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
    ssn_server_t *server = ssn_server_create("/tmp/test.sock");
    ssn_server_set_on_message(server, on_message, NULL);

    // 注册RPC方法
    ssn_url_ref_t url = {"/test/rpc", 9};
    ssn_server_add_method(server, &url, handle_rpc, NULL);

    ssn_server_start(server);
    ssn_server_run(server);
    ssn_server_destroy(server);
}

// 客户端
void client_example() {
    ssn_client_t *client = ssn_client_create(on_message, NULL);

    struct timespec timeout = {1, 0};
    if (ssn_client_connect(client, "/tmp/test.sock", &timeout)) {
        ssn_data_ref_t data = {"hello", 5};
        ssn_url_ref_t url = {"/test/rpc", 9};

        ssn_client_call(client, &url, &data, on_reply, NULL, 1000);
    }

    ssn_client_close(client);
}
```

### TCP 服务器和客户端

```c
// 服务器
void tcp_server_example() {
    // 使用TCP协议
    ssn_server_t *server = ssn_server_create("tcp://0.0.0.0:8080");
    ssn_server_set_on_message(server, on_message, NULL);
    ssn_server_start(server);
    ssn_server_run(server);
    ssn_server_destroy(server);
}

// 客户端
void tcp_client_example() {
    ssn_client_t *client = ssn_client_create(on_message, NULL);

    struct timespec timeout = {1, 0};
    // 使用TCP协议连接
    if (ssn_client_connect(client, "tcp://127.0.0.1:8080", &timeout)) {
        ssn_data_ref_t data = {"hello", 5};
        ssn_url_ref_t url = {"/test/rpc", 9};

        ssn_client_call(client, &url, &data, on_reply, NULL, 1000);
    }

    ssn_client_close(client);
}
```

### UDP 服务器和客户端

```c
// 服务器
void udp_server_example() {
    // 使用UDP协议
    ssn_server_t *server = ssn_server_create("udp://0.0.0.0:9090");
    ssn_server_set_on_message(server, on_message, NULL);
    ssn_server_start(server);
    ssn_server_run(server);
    ssn_server_destroy(server);
}

// 客户端
void udp_client_example() {
    ssn_client_t *client = ssn_client_create(on_message, NULL);

    struct timespec timeout = {1, 0};
    // 使用UDP协议连接
    if (ssn_client_connect(client, "udp://127.0.0.1:9090", &timeout)) {
        ssn_data_ref_t data = {"hello", 5};
        ssn_url_ref_t url = {"/test/rpc", 9};

        ssn_client_call(client, &url, &data, on_reply, NULL, 1000);
    }

    ssn_client_close(client);
}
```

### 发布订阅示例

```c
// 订阅者
void subscriber_example() {
    ssn_client_t *client = ssn_client_create(on_message, NULL);
    ssn_client_connect(client, "/tmp/test.sock", &(struct timespec){1, 0});

    ssn_url_ref_t url = {"/topic/news", 11};
    ssn_client_subscribe(client, &url, on_subscribe, NULL, 1000);

    ssn_client_run(client);
    ssn_client_close(client);
}

// 发布者
void publisher_example() {
    ssn_server_t *server = ssn_server_create("/tmp/test.sock");
    ssn_server_start(server);

    ssn_url_ref_t url = {"/topic/news", 11};
    ssn_data_ref_t data = {"Breaking news!", 14};

    // 发布消息给所有订阅者
    ssn_server_publish(server, &url, &data);
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
   - 使用 `ssn_handle_error` 记录错误信息
