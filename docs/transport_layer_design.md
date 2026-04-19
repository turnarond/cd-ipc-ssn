# CD-IPC-SSN 传输层设计文档

## 概述

CD-IPC-SSN 是一个支持多种传输协议的进程间通信库，采用分层架构设计，将传输层抽象为统一的接口，支持 Unix Socket、TCP 和 UDP 等多种协议。

## 架构设计

### 分层架构

```
+------------------------+
|     Application Layer   |  应用层（RPC、发布订阅、消息）
+------------------------+
           |
+------------------------+
|     Protocol Layer      |  协议层（IPC协议）
+------------------------+
           |
+------------------------+
|   Transport Layer       |  传输层（统一接口）
+------------------------+
           |
    +-----+-----+-----+
    |     |     |     |
+----+ +----+ +----+ +----+
|Unix| |TCP | |UDP | |TLS |
|Sock| |    | |    | |    |
+----+ +----+ +----+ +----+
    传输适配层实现
```

### 核心组件

#### 1. 传输层接口 (ssn_transport.h)

统一的传输接口定义：

```c
typedef struct ssn_transport_ops {
    bool (*bind)(ssn_transport_t* transport, const ssn_address_t* addr);
    bool (*connect)(ssn_transport_t* transport,
                    const ssn_address_t* addr,
                    int timeout_ms);
    bool (*disconnect)(ssn_transport_t* transport);
    bool (*is_connected)(const ssn_transport_t* transport);
    int (*send)(ssn_transport_t* transport,
                const void* data,
                size_t len);
    int (*recv)(ssn_transport_t* transport,
                void* buffer,
                size_t size,
                int timeout_ms);
    bool (*listen)(ssn_transport_t* transport, int backlog);
    ssn_transport_t* (*accept)(ssn_transport_t* transport,
                               ssn_address_t* client_addr,
                               int timeout_ms);
    bool (*set_option)(ssn_transport_t* transport,
                       int option,
                       const void* value);
    bool (*get_option)(const ssn_transport_t* transport,
                        int option,
                        void* value);
    bool (*get_stats)(const ssn_transport_t* transport,
                       ssn_transport_stats_t* stats);
    bool (*get_address)(const ssn_transport_t* transport,
                        ssn_address_t* addr);
    void (*destroy)(ssn_transport_t* transport);
} ssn_transport_ops_t;
```

#### 2. 传输配置 (ssn_transport_config_t)

```c
typedef struct {
    ssn_transport_type_t type;     // 传输类型
    bool non_blocking;              // 非阻塞模式
    int send_timeout_ms;            // 发送超时
    int recv_timeout_ms;            // 接收超时
    int connect_timeout_ms;         // 连接超时
    bool enable_keepalive;          // 保活
    int keepalive_idle_sec;        // 保活空闲时间
    int keepalive_interval_sec;    // 保活间隔
    int keepalive_count;           // 保活次数
    bool enable_nagle;             // Nagle算法
    int send_buffer_size;          // 发送缓冲区
    int recv_buffer_size;          // 接收缓冲区
    bool reuse_address;            // 地址复用
} ssn_transport_config_t;
```

#### 3. 地址结构 (ssn_address_t)

```c
typedef struct {
    ssn_transport_type_t type;
    union {
        struct sockaddr_un unix_addr;    // Unix Socket
        struct sockaddr_in inet_addr;    // IPv4
        struct sockaddr_in6 inet6_addr; // IPv6
    } addr;
    char address_str[256];
} ssn_address_t;
```

## 协议类型

### 支持的传输类型

| 类型 | 枚举值 | 说明 |
|------|--------|------|
| Unix Socket | SSN_TRANSPORT_UNIX | 本地进程间通信 |
| TCP | SSN_TRANSPORT_TCP | TCP/IPv4 可靠传输 |
| TCP6 | SSN_TRANSPORT_TCP6 | TCP/IPv6 可靠传输 |
| UDP | SSN_TRANSPORT_UDP | UDP/IPv4 无连接传输 |
| UDP6 | SSN_TRANSPORT_UDP6 | UDP/IPv6 无连接传输 |
| TLS | SSN_TRANSPORT_TLS | TLS 安全传输（待实现） |
| DTLS | SSN_TRANSPORT_DTLS | DTLS 安全传输（待实现） |

### 地址格式

| 协议 | 地址格式 | 示例 |
|------|----------|------|
| Unix Socket | `unix:///path/to/socket` | `unix:///tmp/test.sock` |
| TCP | `tcp://host:port` | `tcp://127.0.0.1:8080` |
| UDP | `udp://host:port` | `udp://127.0.0.1:9090` |
| TCP6 | `tcp6://[host]:port` | `tcp6://[::1]:8080` |
| UDP6 | `udp6://[host]:port` | `udp6://[::1]:9090` |

## 实现设计

### 工厂模式

使用工厂模式创建不同类型的传输实例：

```c
ssn_transport_t* ssn_transport_create(ssn_transport_type_t type,
                                       const ssn_transport_config_t* config);
```

工厂实现：
- 根据类型创建对应的传输实例
- 初始化公共字段（配置、统计信息、锁等）
- 调用具体传输类型的创建函数

### 具体传输实现

#### Unix Socket 传输

- 文件系统路径作为地址
- 可靠的面向连接通信
- 适用于本地进程间通信
- 高性能、低延迟

#### TCP 传输

- IP 地址 + 端口作为地址
- 可靠的面向连接通信
- 适用于网络通信
- 支持保活、Nagle 算法等选项

#### UDP 传输

- IP 地址 + 端口作为地址
- 无连接的不可靠通信
- 适用于实时性要求高的场景
- 支持广播、多播（待实现）

### 核心流程

#### 服务器启动流程

```
1. 解析地址字符串
   |
2. 根据地址类型选择传输实现
   |
3. 创建传输实例
   |
4. 绑定地址（bind）
   |
5. 开始监听（listen）
   |
6. 接受连接（accept）
   |
7. 处理客户端请求
```

#### 客户端连接流程

```
1. 解析地址字符串
   |
2. 根据地址类型选择传输实现
   |
3. 创建传输实例
   |
4. 连接到服务器（connect）
   |
5. 发送服务信息
   |
6. 进行 RPC/订阅/消息通信
```

## 协议层集成

### 消息发送

```c
bool ipc_send_message(ssn_transport_t *transport,
                     ipc_header_t *ipc_hdr,
                     const ipc_url_ref_t *url,
                     const ipc_data_ref_t *data)
```

通过传输层的 `send` 接口发送序列化后的消息。

### 地址自动识别

服务器和客户端支持自动识别地址类型：

```c
// 服务器
if (strstr(name, "://") != NULL) {
    // 使用提供的地址
} else {
    // 默认为 Unix Socket
}

// 客户端
ssn_address_parse(ipc_path, &addr);
config.type = addr.type;  // 根据解析结果设置类型
```

## 错误处理

### 错误码定义

```c
typedef enum {
    SSN_ERR_SUCCESS = 0,
    SSN_ERR_INVALID_ARGS,
    SSN_ERR_OUT_OF_MEMORY,
    SSN_ERR_NET_CONNECT,
    SSN_ERR_NET_READ,
    SSN_ERR_NET_WRITE,
    SSN_ERR_TIMEOUT,
    SSN_ERR_NOT_FOUND,
    SSN_ERR_ALREADY_EXISTS,
    SSN_ERR_PERMISSION_DENIED,
    SSN_ERR_PROTOCOL_ERROR,
    SSN_ERR_TRANSPORT_ERROR,
    SSN_ERR_INTERNAL_ERROR
} ssn_error_t;
```

## 统计信息

```c
typedef struct {
    uint64_t bytes_sent;           // 发送字节数
    uint64_t bytes_received;       // 接收字节数
    uint32_t packets_sent;         // 发送包数
    uint32_t packets_received;     // 接收包数
    uint32_t send_errors;          // 发送错误数
    uint32_t recv_errors;          // 接收错误数
    uint32_t connection_count;     // 连接数
    uint32_t failed_connections;   // 失败连接数
    uint32_t avg_latency_ms;       // 平均延迟
    uint32_t max_latency_ms;       // 最大延迟
    float loss_rate;               // 丢包率
} ssn_transport_stats_t;
```

## 扩展性设计

### 添加新传输类型

1. 在 `ssn_transport_type_t` 中添加新类型
2. 实现新传输的创建函数（如 `xxx_transport_create`）
3. 在工厂函数中注册新类型
4. 在地址解析中添加新协议支持

### 未来扩展方向

1. **TLS/DTLS 支持**
   - 添加 SSL/TLS 加密传输
   - 支持证书认证

2. **IPv6 支持**
   - 完善 IPv6 地址解析
   - 支持 TCP6、UDP6

3. **多播支持**
   - UDP 多播组播
   - 服务发现

4. **连接池**
   - 复用连接
   - 负载均衡

## 性能优化

### 缓冲区设置

```c
ssn_transport_config_t config = {
    .send_buffer_size = 65536,   // 64KB 发送缓冲
    .recv_buffer_size = 65536,   // 64KB 接收缓冲
};
```

### 非阻塞模式

```c
ssn_transport_config_t config = {
    .non_blocking = true,  // 非阻塞模式
};
```

### 超时设置

根据场景调整超时：
- 本地通信：较短超时（100-500ms）
- 网络通信：较长超时（1-5s）

## 线程安全性

传输层本身**不是线程安全**的，需要调用方保证：
- 同一传输实例不应被多线程同时访问
- 建议每个线程使用独立的传输实例
- 或使用外部同步机制保护共享传输

## 总结

传输层设计采用接口抽象 + 工厂模式的方案，实现了：
- 统一的接口定义
- 灵活的协议扩展
- 简洁的使用方式
- 良好的可维护性

通过这种设计，协议层可以完全不关心底层传输细节，专注于业务逻辑的实现。
