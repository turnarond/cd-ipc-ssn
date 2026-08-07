# SSN API 使用指南

## 文档信息

| 项目 | 内容 |
|------|------|
| 文档版本 | v1.0 |
| 状态 | 有效 |
| 更新日期 | 2026-08-07 |

## 目录

1. [概述](#概述)
2. [地址格式](#地址格式)
3. [服务器API](#服务器api)
4. [客户端API](#客户端api)
5. [节点API](#节点api)
6. [自动重连客户端](#自动重连客户端)
7. [消息类型](#消息类型)
8. [回调函数](#回调函数)
9. [错误处理](#错误处理)
10. [示例代码](#示例代码)
11. [注意事项](#注意事项)

## 概述

SSN 是一个支持多种传输协议的进程间通信框架，支持：
- Unix Socket（本地通信）
- TCP（网络通信，可靠传输）
- UDP（网络通信，无连接）

所有API都声明在以下头文件中：
- `ssn_client.h` - 客户端接口
- `ssn_server.h` - 服务器接口
- `ssn_frame.h` - 线协议定义
- `ssn_error.h` - 错误码定义与错误处理函数
- `ssn_cliauto.h` - 自动重连客户端
- `node/ssn_node.h` - 节点抽象层接口

## 地址格式

### 支持的地址格式

| 协议 | 地址格式 | 示例 |
|------|----------|------|
| Unix Socket | `unix:///path/to/socket` | `unix:///tmp/test.sock` |
| TCP | `tcp://host:port` | `tcp://127.0.0.1:8080` |
| UDP | `udp://host:port` | `udp://127.0.0.1:9090` |

### 地址格式说明

1. **Unix Socket**
   - 使用 `unix:///path/to/socket` 格式（`ssn_address_parse` 要求地址含 `://`）
   - 仅服务端创建时对不带前缀的裸路径自动补全 `unix://`；客户端连接请使用完整格式

2. **TCP/UDP**
   - 必须使用 `protocol://host:port` 格式
   - `host` 可以是 IP 地址或主机名
   - `port` 必须是有效的端口号 (1-65535)

## 服务器API

### 创建和销毁

```c
// 创建IPC服务器（简单方式，server_info 为地址，如 "unix:///tmp/test.sock"）
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
void ssn_server_set_connect_handler(ssn_server_t *server,
                                    ssn_on_connect_t oncli, void *arg);
void ssn_server_set_message_handler(ssn_server_t *server,
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

// 发送RPC响应（在方法回调内调用，seqno 取 ssn_get_seqno(ipc_hdr)）
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

// 向指定客户端发送消息
int ssn_server_message(ssn_server_t *server, ssn_peer_id_t id,
                       const ssn_url_ref_t *url,
                       const ssn_data_ref_t *data);
```

### 连接管理

```c
// 获取已连接客户端数量
int ssn_server_peer_count(ssn_server_t *server);

// 关闭指定客户端连接
bool ssn_server_peer_close(ssn_server_t *server, ssn_peer_id_t id);

// 获取客户端列表
int ssn_server_peer_list(ssn_server_t *server, ssn_peer_id_t ids[], int max_cnt);
```

### 地址获取

```c
// 获取服务器监听地址（需在 ssn_server_start 之后调用）
int ssn_server_address(ssn_server_t *server,
                      struct sockaddr *addr,
                      socklen_t *namelen);

// 获取指定客户端地址
int ssn_server_peer_address(ssn_server_t *server, ssn_peer_id_t id,
                           struct sockaddr *addr,
                           socklen_t *namelen);
```

## 客户端API

### 创建和销毁

```c
// 创建IPC客户端（无参数，消息回调通过 ssn_client_set_on_message 注册）
ssn_client_t *ssn_client_create(void);

// 关闭客户端
void ssn_client_close(ssn_client_t *client);
```

### 连接管理

```c
// 连接到服务器（同步，timeout 为 struct timespec 指针，NULL 表示使用默认值）
bool ssn_client_connect(ssn_client_t *client,
                        const char *ipc_path,
                        const struct timespec *timeout);

// 断开连接（断开后可再次调用 ssn_client_connect）
bool ssn_client_disconnect(ssn_client_t *client);

// 检查连接状态
bool ssn_client_is_connect(ssn_client_t *client);

// 设置发送超时（毫秒）
bool ssn_client_send_timeout(ssn_client_t *client, const int timeout_ms);
```

### RPC调用

```c
// RPC调用（返回 <0 表示发送失败；0 表示请求已发出）
int ssn_client_call(ssn_client_t *client,
                    const ssn_url_ref_t *url,
                    const ssn_data_ref_t *data,
                    ssn_client_rpcreply_handler_t callback,
                    void *arg,
                    uint64_t timeout_ms);
```

### 发布订阅

```c
// 订阅（返回 true 表示订阅成功）
bool ssn_client_subscribe(ssn_client_t *client,
                          const ssn_url_ref_t *url,
                          ssn_client_msg_handler_t callback,
                          void *arg,
                          uint64_t timeout_ms);

// 取消订阅（不携带回调参数）
bool ssn_client_unsubscribe(ssn_client_t *client,
                            const ssn_url_ref_t *url,
                            uint64_t timeout_ms);
```

### 消息处理

```c
// 发送消息
int ssn_client_message(ssn_client_t *client,
                       const ssn_url_ref_t *url,
                       const ssn_data_ref_t *data);

// 设置消息回调（MESSAGE 类型；v2.1.0 起同时作为订阅消息的 onsub 回调）
void ssn_client_set_on_message(ssn_client_t *client,
                               ssn_client_msg_handler_t callback,
                               void *arg);

// 设置 PUBLISH 类型回调（通常由 ssn_client_auto 内部设置，用户一般不直接调用）
void ssn_client_set_on_publish(ssn_client_t *client,
                               ssn_client_msg_handler_t callback,
                               void *arg);

// 事件循环
int ssn_client_poll(ssn_client_t *client, uint64_t timeout_ms);
void ssn_client_run(ssn_client_t *client);
```

## 节点API

节点抽象层同时提供客户端与服务器能力，接口见 `node/ssn_node.h`。

### 生命周期与查询

```c
// 创建节点
ssn_node_t *ssn_node_create(const ssn_node_config_t *config);

// 启动 / 停止 / 销毁
bool ssn_node_start(ssn_node_t *node);
bool ssn_node_stop(ssn_node_t *node);
void ssn_node_destroy(ssn_node_t *node);

// 查询状态与能力
ssn_node_state_t ssn_node_get_state(ssn_node_t *node);
uint32_t ssn_node_get_capabilities(ssn_node_t *node);

// 统计信息
bool ssn_node_get_stats(ssn_node_t *node, int *active_connections,
                        uint64_t *total_messages);
```

### 通信

```c
// 向指定对端发送消息（peer_address 为 "host:port"）
bool ssn_node_send_to_peer(ssn_node_t *node, const char *peer_address,
                           const ssn_url_ref_t *url, const ssn_data_ref_t *data);

// 发布消息
bool ssn_node_publish(ssn_node_t *node,
                      const ssn_url_ref_t *url, const ssn_data_ref_t *data);

// 订阅主题（peer_address 指定对端，v2.1.0 起为必填）
bool ssn_node_subscribe(ssn_node_t *node, const char *peer_address,
                        const ssn_url_ref_t *url,
                        ssn_client_msg_handler_t callback, void *arg,
                        uint64_t timeout_ms);

// 取消订阅
bool ssn_node_unsubscribe(ssn_node_t *node,
                          const ssn_url_ref_t *url, uint64_t timeout_ms);

// 节点间 RPC 调用（返回 0 成功，-1 失败）
int ssn_node_rpc_call(ssn_node_t *node, const char *peer_address,
                      const ssn_url_ref_t *url, const ssn_data_ref_t *data,
                      ssn_client_rpcreply_handler_t callback, void *arg,
                      uint64_t timeout_ms);

// 注册 / 移除 RPC 方法
bool ssn_node_add_rpc_method(ssn_node_t *node, const ssn_url_ref_t *url,
                             ssn_server_rpc_handler_t callback, void *arg);
void ssn_node_remove_rpc_method(ssn_node_t *node, const ssn_url_ref_t *url);
```

### 事件循环与回调设置

```c
int ssn_node_poll(ssn_node_t *node, uint64_t timeout_ms);
void ssn_node_run(ssn_node_t *node);

void ssn_node_set_connect_handler(ssn_node_t *node,
                                  ssn_on_connect_t callback, void *arg);
void ssn_node_set_message_handler(ssn_node_t *node,
                                  ssn_server_msg_handler_t callback, void *arg);
void ssn_node_set_client_message_handler(ssn_node_t *node,
                                         ssn_client_msg_handler_t callback,
                                         void *arg);
```

### 节点配置结构

```c
typedef struct {
    char node_id[64];                // 节点ID（留空自动生成）
    char node_type[32];              // 节点类型
    char node_name[64];              // 节点名称
    char listen_address[256];        // 监听地址
    uint16_t listen_port;            // 监听端口
    uint32_t capabilities;           // 能力位掩码（SSN_NODE_CAP_*）
    uint32_t max_connections;        // 最大连接数
    uint32_t send_buffer_size;       // 发送缓冲区大小
    uint32_t recv_buffer_size;       // 接收缓冲区大小
    uint32_t send_timeout_ms;        // 发送超时
    uint32_t conn_timeout_ms;        // 连接超时
    uint32_t idle_timeout_sec;       // 空闲超时
} ssn_node_config_t;
```

## 自动重连客户端

`ssn_client_auto` 模块（`ssn_cliauto.h`）提供自动连接、断线重连、自动订阅与事件循环处理。除 `ssn_client_auto_handle` 外的函数不允许在客户端事件循环线程上下文（如 RPC 回调、订阅消息回调）中调用。

```c
// 创建 / 删除
ssn_client_auto_t *ssn_client_auto_create(void);
void ssn_client_auto_delete(ssn_client_auto_t *cliauto);

// 设置连接回调（connect=true 表示连接并订阅成功）
bool ssn_client_auto_setup(ssn_client_auto_t *cliauto,
                           ssn_client_conn_func_t onconn, void *arg);

// 启动（server 为 ip:port 或服务主机名；urls/url_cnt 为建链后订阅的 URL 列表；
// keepalive 为 ping 间隔毫秒（最小 50ms）；conn_timeout 为连接超时毫秒（最小 20ms）；
// reconn_delay 为重连等待毫秒（最小 20ms））
bool ssn_client_auto_start(ssn_client_auto_t *cliauto, const char *server,
                           char * const urls[], int url_cnt,
                           unsigned int keepalive, unsigned int conn_timeout,
                           unsigned int reconn_delay);

// 停止（start 与 stop 必须成对顺序调用）
bool ssn_client_auto_stop(ssn_client_auto_t *cliauto);

// 获取通信用客户端句柄（仅用于通信，不能做关闭连接等状态操作）
ssn_client_t *ssn_client_auto_handle(ssn_client_auto_t *cliauto);
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
                                        ssn_header_t *ipc_hdr,
                                        ssn_url_ref_t *url,
                                        ssn_data_ref_t *data,
                                        void *arg);
```

### 客户端回调

```c
// RPC响应回调（ipc_hdr 为 NULL 表示服务器未响应；ipc_hdr/data 在回调返回后失效）
typedef void (*ssn_client_rpcreply_handler_t)(ssn_client_t *client,
                                              ssn_header_t *ipc_hdr,
                                              ssn_data_ref_t *data,
                                              void *arg);

// 操作结果回调（订阅/取消订阅）
typedef void (*ssn_client_result_handler_t)(ssn_client_t *client,
                                            bool success,
                                            void *arg);

// 消息回调（发布/普通消息；url/data 在回调返回后失效）
typedef void (*ssn_client_msg_handler_t)(ssn_client_t *client,
                                          ssn_url_ref_t *url,
                                          ssn_data_ref_t *data,
                                          void *arg);
```

## 错误处理

### 错误码

错误码类型为 `ssn_ecode_t`（uint32_t），由 `SSN_ECODE_MAKE(category, subcategory, code)` 宏组合生成（高 8 位类别、中间 8 位子类别、低 16 位错误码），完整定义见 `ssn_error.h`：

```c
// 通用错误（SSN_ECODE_CATEGORY_COMMON = 0x00）
#define SSN_ECODE_SUCCESS             0x00000000  // 成功
#define SSN_ECODE_INVALID_ARGS        0x00000001  // 无效参数
#define SSN_ECODE_NOT_FOUND           0x00000002  // 未找到
#define SSN_ECODE_TIMEOUT             0x00000003  // 超时
#define SSN_ECODE_INTERNAL            0x00000004  // 内部错误

// 网络错误（SSN_ECODE_CATEGORY_NETWORK = 0x01）
#define SSN_ECODE_NET_CONNECT         0x01000001  // 连接失败
#define SSN_ECODE_NET_DISCONNECT      0x01000002  // 连接断开
#define SSN_ECODE_NET_READ            0x01000003  // 读取失败
#define SSN_ECODE_NET_WRITE           0x01000004  // 写入失败

// 服务错误（SSN_ECODE_CATEGORY_SERVICE = 0x02）
#define SSN_ECODE_SERVICE_NOT_FOUND   0x02000001  // 服务未找到
#define SSN_ECODE_SERVICE_BUSY        0x02000002  // 服务繁忙
#define SSN_ECODE_SERVICE_ERROR       0x02000003  // 服务错误

// 资源错误（SSN_ECODE_CATEGORY_RESOURCE = 0x03）
#define SSN_ECODE_OUT_OF_MEMORY       0x03000001  // 内存不足
#define SSN_ECODE_RESOURCE_LIMIT      0x03000002  // 资源限制

// 安全错误（SSN_ECODE_CATEGORY_SECURITY = 0x04）
#define SSN_ECODE_AUTH_FAILED         0x04000001  // 认证失败
#define SSN_ECODE_ACCESS_DENIED       0x04000002  // 访问拒绝

// 序列化错误（SSN_ECODE_CATEGORY_SERIALIZE = 0x05）
#define SSN_ECODE_SERIALIZE_FAILED    0x05000001  // 序列化失败
#define SSN_ECODE_DESERIALIZE_FAILED  0x05000002  // 反序列化失败
```

### 错误处理函数

```c
// 错误码 → 错误描述
const char *ssn_ecode_message(ssn_ecode_t error);

// 获取错误类别 / 子类别 / 具体错误码
uint8_t  ssn_ecode_category(ssn_ecode_t error);
uint8_t  ssn_ecode_subcategory(ssn_ecode_t error);
uint16_t ssn_ecode_code(ssn_ecode_t error);

// 记录错误日志（带文件名/行号/函数名/格式化信息）
void ssn_handle_error(ssn_ecode_t error,
                      const char *file,
                      int line,
                      const char *func,
                      const char *format, ...);
```

## 示例代码

示例中的 `ssn_url_ref_t.url_len` 与 `ssn_data_ref_t.length` 均为 **strlen 语义（不含 NUL 终止符）**，与代码 `ssn_get_url`/`ssn_get_data` 的解析行为一致；字面量均取对应字符串的 strlen 值（如 `"/test/rpc"` 为 9、`"hello"` 为 5）。

### Unix Socket 服务器和客户端

```c
// 服务器
void server_example() {
    ssn_server_t *server = ssn_server_create("unix:///tmp/test.sock");
    ssn_server_set_message_handler(server, on_message, NULL);

    // 注册RPC方法
    ssn_url_ref_t url = {"/test/rpc", 9};
    ssn_server_add_method(server, &url, handle_rpc, NULL);

    ssn_server_start(server);
    ssn_server_run(server);
    ssn_server_destroy(server);
}

// 客户端
void client_example() {
    ssn_client_t *client = ssn_client_create();
    ssn_client_set_on_message(client, on_message, NULL);

    struct timespec timeout = {1, 0};
    if (ssn_client_connect(client, "unix:///tmp/test.sock", &timeout)) {
        ssn_data_ref_t data = {"hello", 5};
        ssn_url_ref_t url = {"/test/rpc", 9};

        ssn_client_call(client, &url, &data, on_reply, NULL, 1000);
        ssn_client_poll(client, 2000);
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
    ssn_server_set_message_handler(server, on_message, NULL);
    ssn_server_start(server);
    ssn_server_run(server);
    ssn_server_destroy(server);
}

// 客户端
void tcp_client_example() {
    ssn_client_t *client = ssn_client_create();
    ssn_client_set_on_message(client, on_message, NULL);

    struct timespec timeout = {1, 0};
    // 使用TCP协议连接
    if (ssn_client_connect(client, "tcp://127.0.0.1:8080", &timeout)) {
        ssn_data_ref_t data = {"hello", 5};
        ssn_url_ref_t url = {"/test/rpc", 9};

        ssn_client_call(client, &url, &data, on_reply, NULL, 1000);
        ssn_client_poll(client, 2000);
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
    ssn_server_set_message_handler(server, on_message, NULL);
    ssn_server_start(server);
    ssn_server_run(server);
    ssn_server_destroy(server);
}

// 客户端
void udp_client_example() {
    ssn_client_t *client = ssn_client_create();
    ssn_client_set_on_message(client, on_message, NULL);

    struct timespec timeout = {1, 0};
    // 使用UDP协议连接
    if (ssn_client_connect(client, "udp://127.0.0.1:9090", &timeout)) {
        ssn_data_ref_t data = {"hello", 5};
        ssn_url_ref_t url = {"/test/rpc", 9};

        ssn_client_call(client, &url, &data, on_reply, NULL, 1000);
        ssn_client_poll(client, 2000);
    }

    ssn_client_close(client);
}
```

### 发布订阅示例

```c
// 订阅者
void subscriber_example() {
    ssn_client_t *client = ssn_client_create();
    ssn_client_set_on_message(client, on_message, NULL);
    ssn_client_connect(client, "unix:///tmp/test.sock", &(struct timespec){1, 0});

    ssn_url_ref_t url = {"/topic/news", 11};
    // 订阅回调也可通过 set_on_message 兜底分发
    ssn_client_subscribe(client, &url, on_message, NULL, 1000);

    ssn_client_run(client);
    ssn_client_close(client);
}

// 发布者
void publisher_example() {
    ssn_server_t *server = ssn_server_create("unix:///tmp/test.sock");
    ssn_server_start(server);

    ssn_url_ref_t url = {"/topic/news", 11};
    ssn_data_ref_t data = {"Breaking news!", 14};

    // 发布消息给所有订阅者
    ssn_server_publish(server, &url, &data);
}
```

## 注意事项

1. **线程安全**
   - 库基于单线程事件循环模型设计，大多数函数不是线程安全的，跨线程访问同一对象时需要在调用时确保同步
   - 建议使用事件循环模型处理并发

2. **超时设置**
   - 超时值建议不小于20毫秒
   - RPC调用超时建议不小于100毫秒

3. **内存管理**
   - URL和数据引用在回调返回后可能无效
   - 需要在回调中复制需要长期使用的数据
   - 单包最大 128 KiB（`SSN_MAX_PACKET_SIZE`），超过需在应用层分片

4. **地址格式**
   - 优先使用带协议前缀的地址格式
   - Unix Socket路径应使用完整路径

5. **错误处理**
   - 建议检查所有API的返回值
   - 使用 `ssn_ecode_message` 转换错误码、`ssn_handle_error` 记录错误信息
