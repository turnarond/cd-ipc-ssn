# 传输层架构设计文档

## 1. 架构概述

### 1.1 设计目标

设计一个统一的传输层架构，支持以下通信协议：
1. **Unix Socket**：本地进程间通信
2. **TCP**：可靠的面向连接传输
3. **UDP**：无连接的尽力而为传输

同时满足以下要求：
- 在侦听模式下能够同时启用Unix Socket功能
- 遵循模块化与分层设计原则
- 代码风格规范、代码质量优良、可读性高
- 参考nanomsgs库的优点，将协议适配放到transports文件夹中
- 修改当前工程文件中的前缀ipc_，使用新的前缀名

### 1.2 设计原则

1. **接口统一**：所有传输协议提供一致的API
2. **可扩展性**：易于添加新的传输协议
3. **性能优化**：针对不同协议特性优化性能
4. **向后兼容**：保持现有Unix Socket功能不变
5. **模块化**：将协议适配放到transports文件夹中，减轻核心文件代码量

## 2. 核心组件划分

### 2.1 目录结构

```
src/
├── comms/
│   └── cd-ipc-ssn/
│       ├── src/
│       │   ├── core/            # 核心功能
│       │   │   ├── ssn_client.c  # 客户端核心逻辑
│       │   │   ├── ssn_server.c  # 服务器核心逻辑
│       │   │   ├── ssn_protocol.c # 协议处理
│       │   │   └── ssn_global.c  # 全局变量
│       │   ├── transports/      # 传输适配器
│       │   │   ├── ssn_transport.h    # 统一传输接口
│       │   │   ├── ssn_transport_unix.c  # Unix Socket适配器
│       │   │   ├── ssn_transport_tcp.c   # TCP适配器
│       │   │   └── ssn_transport_udp.c   # UDP适配器
│       │   └── util/            # 工具函数
│       └── docs/                # 文档
│           └── protocol_adapter_design.md  # 传输层设计文档
```

### 2.2 核心组件

1. **传输接口层**：定义统一的传输操作接口
2. **协议适配器层**：实现不同协议的具体适配
3. **传输工厂**：负责创建和管理传输实例
4. **连接池**：管理连接资源，提高性能
5. **地址解析**：处理不同协议的地址格式

## 3. 统一传输接口设计

### 3.1 数据结构

```c
// 传输类型定义
typedef enum {
    SSN_TRANSPORT_UNIX,      // Unix域套接字
    SSN_TRANSPORT_TCP,       // TCP套接字
    SSN_TRANSPORT_TCP6,      // IPv6 TCP套接字
    SSN_TRANSPORT_UDP,       // UDP套接字
    SSN_TRANSPORT_UDP6,      // IPv6 UDP套接字
    SSN_TRANSPORT_TLS,       // TLS over TCP
    SSN_TRANSPORT_DTLS       // DTLS over UDP
} ssn_transport_type_t;

// 地址结构
typedef struct {
    ssn_transport_type_t type;           // 传输类型
    union {
        struct sockaddr_un unix_addr;    // Unix地址
        struct sockaddr_in inet_addr;    // IPv4地址
        struct sockaddr_in6 inet6_addr;  // IPv6地址
    } addr;                              // 具体地址
    char address_str[256];               // 字符串格式地址
} ssn_address_t;

// 传输配置
typedef struct {
    ssn_transport_type_t type;           // 传输类型
    bool non_blocking;                   // 是否非阻塞
    int send_timeout_ms;                 // 发送超时
    int recv_timeout_ms;                 // 接收超时
    int connect_timeout_ms;              // 连接超时
    bool enable_keepalive;               // 启用保活
    int keepalive_idle_sec;              // 保活空闲时间
    int keepalive_interval_sec;          // 保活间隔
    int keepalive_count;                 // 保活次数
    bool enable_nagle;                   // 启用Nagle算法
    int send_buffer_size;                // 发送缓冲区大小
    int recv_buffer_size;                // 接收缓冲区大小
    bool reuse_address;                  // 地址重用
} ssn_transport_config_t;

// 传输统计
typedef struct {
    uint64_t bytes_sent;                 // 发送字节数
    uint64_t bytes_received;             // 接收字节数
    uint32_t packets_sent;               // 发送数据包数
    uint32_t packets_received;           // 接收数据包数
    uint32_t send_errors;                // 发送错误数
    uint32_t recv_errors;                // 接收错误数
    uint32_t connection_count;           // 连接数
    uint32_t failed_connections;         // 失败连接数
    uint32_t avg_latency_ms;             // 平均延迟
    uint32_t max_latency_ms;             // 最大延迟
    float loss_rate;                     // 丢包率
} ssn_transport_stats_t;
```

### 3.2 传输接口

```c
// 传输操作接口
typedef struct ssn_transport_ops {
    // 连接管理
    bool (*connect)(ssn_transport_t* transport, const ssn_address_t* addr, int timeout_ms);
    bool (*disconnect)(ssn_transport_t* transport);
    bool (*is_connected)(const ssn_transport_t* transport);
    
    // 数据收发
    int (*send)(ssn_transport_t* transport, const void* data, size_t len);
    int (*recv)(ssn_transport_t* transport, void* buffer, size_t size, int timeout_ms);
    
    // 服务器功能
    bool (*listen)(ssn_transport_t* transport, int backlog);
    ssn_transport_t* (*accept)(ssn_transport_t* transport, ssn_address_t* client_addr, int timeout_ms);
    
    // 配置管理
    bool (*set_option)(ssn_transport_t* transport, int option, const void* value);
    bool (*get_option)(const ssn_transport_t* transport, int option, void* value);
    
    // 状态查询
    bool (*get_stats)(const ssn_transport_t* transport, ssn_transport_stats_t* stats);
    bool (*get_address)(const ssn_transport_t* transport, ssn_address_t* addr);
    
    // 资源管理
    void (*destroy)(ssn_transport_t* transport);
} ssn_transport_ops_t;

// 传输实例结构
typedef struct ssn_transport {
    ssn_transport_type_t type;           // 传输类型
    ssn_transport_ops_t ops;             // 操作接口
    void* impl_data;                     // 具体实现数据
    ssn_transport_config_t config;       // 配置信息
    ssn_transport_stats_t stats;         // 统计信息
    ssn_mutex_t* lock;                   // 同步锁
    bool valid;                          // 有效性标记
} ssn_transport_t;
```

## 4. 协议适配器实现

### 4.1 Unix Socket适配器

```c
// Unix Socket实现结构
typedef struct unix_transport_impl {
    int sock_fd;                         // 套接字描述符
    struct sockaddr_un addr;            // Unix地址
    bool is_server;                     // 是否为服务器
    bool non_blocking;                  // 是否非阻塞
    char socket_path[108];              // 套接字路径
    time_t last_activity;               // 最后活动时间
} unix_transport_impl_t;

// Unix Socket连接实现
static bool unix_transport_connect(ssn_transport_t* transport, const ssn_address_t* addr, int timeout_ms) {
    unix_transport_impl_t* impl = (unix_transport_impl_t*)transport->impl_data;
    
    // 创建Unix域套接字
    impl->sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (impl->sock_fd < 0) {
        LOG_ERROR("Failed to create Unix socket: %s", strerror(errno));
        return false;
    }
    
    // 设置非阻塞模式
    if (impl->non_blocking) {
        int flags = fcntl(impl->sock_fd, F_GETFL, 0);
        fcntl(impl->sock_fd, F_SETFL, flags | O_NONBLOCK);
    }
    
    // 连接服务器
    if (connect(impl->sock_fd, (struct sockaddr*)&impl->addr, sizeof(impl->addr)) < 0) {
        if (errno != EINPROGRESS) {
            LOG_ERROR("Failed to connect Unix socket: %s", strerror(errno));
            close(impl->sock_fd);
            return false;
        }
        
        // 非阻塞连接，等待连接完成
        if (timeout_ms > 0) {
            fd_set write_fds;
            struct timeval tv;
            
            FD_ZERO(&write_fds);
            FD_SET(impl->sock_fd, &write_fds);
            
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            
            int ret = select(impl->sock_fd + 1, NULL, &write_fds, NULL, &tv);
            if (ret <= 0) {
                LOG_ERROR("Unix socket connect timeout");
                close(impl->sock_fd);
                return false;
            }
        }
    }
    
    impl->is_server = false;
    impl->last_activity = time(NULL);
    LOG_DEBUG("Connected to Unix socket: %s", impl->socket_path);
    
    return true;
}

// Unix Socket监听实现
static bool unix_transport_listen(ssn_transport_t* transport, int backlog) {
    unix_transport_impl_t* impl = (unix_transport_impl_t*)transport->impl_data;
    
    // 检查Unix socket路径是否存在
    struct stat st;
    if (stat(impl->socket_path, &st) == 0) {
        if (S_ISSOCK(st.st_mode)) {
            unlink(impl->socket_path);
            LOG_INFO("Removed existing Unix socket: %s", impl->socket_path);
        } else {
            LOG_ERROR("Path %s is not a socket file", impl->socket_path);
            return false;
        }
    }
    
    // 绑定地址
    if (bind(impl->sock_fd, (struct sockaddr*)&impl->addr, sizeof(impl->addr)) < 0) {
        LOG_ERROR("Failed to bind Unix socket: %s", strerror(errno));
        return false;
    }
    
    // 开始监听
    if (listen(impl->sock_fd, backlog) < 0) {
        LOG_ERROR("Failed to listen on Unix socket: %s", strerror(errno));
        return false;
    }
    
    impl->is_server = true;
    LOG_DEBUG("Unix socket server listening on: %s", impl->socket_path);
    return true;
}
```

### 4.2 TCP适配器

```c
// TCP实现结构
typedef struct tcp_transport_impl {
    int sock_fd;                         // 套接字描述符
    struct sockaddr_in addr;            // IPv4地址
    struct sockaddr_in6 addr6;          // IPv6地址
    bool is_server;                     // 是否为服务器
    bool non_blocking;                  // 是否非阻塞
    bool ipv6_enabled;                  // 是否启用IPv6
    time_t last_activity;               // 最后活动时间
} tcp_transport_impl_t;

// TCP连接实现
static bool tcp_transport_connect(ssn_transport_t* transport, const ssn_address_t* addr, int timeout_ms) {
    tcp_transport_impl_t* impl = (tcp_transport_impl_t*)transport->impl_data;
    
    // 创建TCP套接字
    int family = impl->ipv6_enabled ? AF_INET6 : AF_INET;
    impl->sock_fd = socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (impl->sock_fd < 0) {
        LOG_ERROR("Failed to create TCP socket: %s", strerror(errno));
        return false;
    }
    
    // 设置套接字选项
    int optval = 1;
    setsockopt(impl->sock_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    
    // 禁用Nagle算法（如果需要）
    if (!transport->config.enable_nagle) {
        optval = 1;
        setsockopt(impl->sock_fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
    }
    
    // 设置非阻塞模式
    if (impl->non_blocking) {
        int flags = fcntl(impl->sock_fd, F_GETFL, 0);
        fcntl(impl->sock_fd, F_SETFL, flags | O_NONBLOCK);
    }
    
    // 连接服务器
    struct sockaddr* sockaddr_ptr;
    socklen_t sockaddr_len;
    
    if (family == AF_INET6) {
        sockaddr_ptr = (struct sockaddr*)&impl->addr6;
        sockaddr_len = sizeof(impl->addr6);
    } else {
        sockaddr_ptr = (struct sockaddr*)&impl->addr;
        sockaddr_len = sizeof(impl->addr);
    }
    
    if (connect(impl->sock_fd, sockaddr_ptr, sockaddr_len) < 0) {
        if (errno != EINPROGRESS) {
            LOG_ERROR("Failed to connect TCP socket: %s", strerror(errno));
            close(impl->sock_fd);
            return false;
        }
        
        // 非阻塞连接，等待连接完成
        if (timeout_ms > 0) {
            fd_set write_fds;
            struct timeval tv;
            
            FD_ZERO(&write_fds);
            FD_SET(impl->sock_fd, &write_fds);
            
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            
            int ret = select(impl->sock_fd + 1, NULL, &write_fds, NULL, &tv);
            if (ret <= 0) {
                LOG_ERROR("TCP socket connect timeout");
                close(impl->sock_fd);
                return false;
            }
        }
    }
    
    impl->is_server = false;
    impl->last_activity = time(NULL);
    transport->stats.connection_count++;
    
    char addr_str[INET6_ADDRSTRLEN];
    if (family == AF_INET6) {
        inet_ntop(AF_INET6, &impl->addr6.sin6_addr, addr_str, sizeof(addr_str));
        LOG_DEBUG("Connected to TCP server [%s]:%d", addr_str, ntohs(impl->addr6.sin6_port));
    } else {
        inet_ntop(AF_INET, &impl->addr.sin_addr, addr_str, sizeof(addr_str));
        LOG_DEBUG("Connected to TCP server %s:%d", addr_str, ntohs(impl->addr.sin_port));
    }
    
    return true;
}
```

### 4.3 UDP适配器

```c
// UDP实现结构
typedef struct udp_transport_impl {
    int sock_fd;                         // 套接字描述符
    struct sockaddr_in addr;            // IPv4地址
    struct sockaddr_in6 addr6;          // IPv6地址
    bool is_server;                     // 是否为服务器
    bool non_blocking;                  // 是否非阻塞
    bool ipv6_enabled;                  // 是否启用IPv6
    bool multicast_enabled;             // 是否启用组播
    char multicast_group[INET_ADDRSTRLEN]; // 组播组地址
    time_t last_activity;               // 最后活动时间
    ssn_packet_buffer_t recv_buf;       // 接收缓冲区
} udp_transport_impl_t;

// UDP发送实现
static int udp_transport_send(ssn_transport_t* transport, const void* data, size_t len) {
    udp_transport_impl_t* impl = (udp_transport_impl_t*)transport->impl_data;
    
    if (!transport->valid || impl->sock_fd < 0) {
        LOG_ERROR("UDP socket not valid for sending");
        return -1;
    }
    
    // 检查数据包大小
    if (len > 65507) { // UDP最大有效载荷
        LOG_ERROR("UDP packet too large: %zu bytes (max 65507)", len);
        return -1;
    }
    
    // 发送数据
    struct sockaddr* sockaddr_ptr;
    socklen_t sockaddr_len;
    
    if (impl->ipv6_enabled) {
        sockaddr_ptr = (struct sockaddr*)&impl->addr6;
        sockaddr_len = sizeof(impl->addr6);
    } else {
        sockaddr_ptr = (struct sockaddr*)&impl->addr;
        sockaddr_len = sizeof(impl->addr);
    }
    
    ssize_t sent = sendto(impl->sock_fd, data, len, 0, sockaddr_ptr, sockaddr_len);
    
    if (sent < 0) {
        LOG_ERROR("Failed to send UDP packet: %s", strerror(errno));
        transport->stats.send_errors++;
        return -1;
    }
    
    transport->stats.bytes_sent += sent;
    transport->stats.packets_sent++;
    impl->last_activity = time(NULL);
    
    return (int)sent;
}

// UDP接收实现
static int udp_transport_recv(ssn_transport_t* transport, void* buffer, size_t size, int timeout_ms) {
    udp_transport_impl_t* impl = (udp_transport_impl_t*)transport->impl_data;
    
    if (!transport->valid || impl->sock_fd < 0) {
        LOG_ERROR("UDP socket not valid for receiving");
        return -1;
    }
    
    // 设置接收超时
    if (timeout_ms > 0) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(impl->sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    
    // 接收数据
    struct sockaddr_storage from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    ssize_t received = recvfrom(impl->sock_fd, buffer, size, 0,
                               (struct sockaddr*)&from_addr, &from_len);
    
    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("Failed to receive UDP packet: %s", strerror(errno));
            transport->stats.recv_errors++;
        }
        return -1;
    }
    
    transport->stats.bytes_received += received;
    transport->stats.packets_received++;
    impl->last_activity = time(NULL);
    
    // 记录发送方信息
    char from_str[INET6_ADDRSTRLEN];
    if (from_addr.ss_family == AF_INET6) {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&from_addr;
        inet_ntop(AF_INET6, &addr6->sin6_addr, from_str, sizeof(from_str));
        LOG_DEBUG("Received %zd bytes from [%s]:%d", received, from_str, ntohs(addr6->sin6_port));
    } else {
        struct sockaddr_in* addr = (struct sockaddr_in*)&from_addr;
        inet_ntop(AF_INET, &addr->sin_addr, from_str, sizeof(from_str));
        LOG_DEBUG("Received %zd bytes from %s:%d", received, from_str, ntohs(addr->sin_port));
    }
    
    return (int)received;
}
```

## 5. 传输工厂模式

### 5.1 传输工厂接口

```c
// 传输工厂接口
typedef struct ssn_transport_factory {
    // 创建传输实例
    ssn_transport_t* (*create)(ssn_transport_type_t type, const ssn_transport_config_t* config);
    
    // 销毁传输实例
    void (*destroy)(ssn_transport_t* transport);
    
    // 获取支持的传输类型
    int (*get_supported_types)(ssn_transport_type_t* types, int max_count);
    
    // 检查传输类型是否支持
    bool (*is_type_supported)(ssn_transport_type_t type);
} ssn_transport_factory_t;

// 传输创建器函数类型
typedef ssn_transport_t* (*ssn_transport_creator_t)(const ssn_transport_config_t* config);

// 传输工厂实现
static ssn_transport_t* transport_factory_create(ssn_transport_type_t type, const ssn_transport_config_t* config) {
    ssn_transport_factory_impl_t* factory_impl = (ssn_transport_factory_impl_t*)g_transport_factory;
    
    ssn_mutex_lock(factory_impl->lock);
    
    // 查找对应的创建器
    ssn_transport_creator_t creator = (ssn_transport_creator_t)ssn_hash_table_get(
        factory_impl->creators, (void*)(uintptr_t)type);
    
    if (!creator) {
        LOG_ERROR("Unsupported transport type: %d", type);
        ssn_mutex_unlock(factory_impl->lock);
        return NULL;
    }
    
    // 创建传输实例
    ssn_transport_t* transport = creator(config);
    
    ssn_mutex_unlock(factory_impl->lock);
    
    if (transport) {
        LOG_DEBUG("Created transport instance: type=%d", type);
    }
    
    return transport;
}
```

### 5.2 传输工厂初始化

```c
// 传输工厂初始化
bool ssn_transport_factory_init(void) {
    // 创建工厂实例
    ssn_transport_factory_impl_t* factory_impl = calloc(1, sizeof(ssn_transport_factory_impl_t));
    if (!factory_impl) {
        LOG_ERROR("Failed to allocate memory for transport factory");
        return false;
    }
    
    // 初始化工厂接口
    factory_impl->factory.create = transport_factory_create;
    factory_impl->factory.destroy = transport_factory_destroy;
    factory_impl->factory.get_supported_types = transport_factory_get_supported_types;
    factory_impl->factory.is_type_supported = transport_factory_is_type_supported;
    
    // 初始化同步机制
    factory_impl->lock = ssn_mutex_create();
    if (!factory_impl->lock) {
        LOG_ERROR("Failed to create mutex for transport factory");
        free(factory_impl);
        return false;
    }
    
    // 初始化创建器映射表
    factory_impl->creators = ssn_hash_table_create(16);
    if (!factory_impl->creators) {
        LOG_ERROR("Failed to create hash table for transport factory");
        ssn_mutex_destroy(factory_impl->lock);
        free(factory_impl);
        return false;
    }
    
    // 注册默认传输类型
    ssn_transport_factory_register(SSN_TRANSPORT_UNIX, unix_transport_create);
    ssn_transport_factory_register(SSN_TRANSPORT_TCP, tcp_transport_create);
    ssn_transport_factory_register(SSN_TRANSPORT_UDP, udp_transport_create);
    
    // 设置全局工厂实例
    g_transport_factory = (ssn_transport_factory_t*)factory_impl;
    
    LOG_INFO("Transport factory initialized successfully");
    return true;
}
```

## 6. 地址解析与转换

### 6.1 地址解析接口

```c
// 地址解析函数
bool ssn_address_parse(const char* address_str, ssn_address_t* addr) {
    if (!address_str || !addr) {
        return false;
    }
    
    // 解析地址格式
    // 格式: protocol://host:port 或 protocol://path (Unix Socket)
    
    char protocol[32];
    char host[256];
    int port = 0;
    char path[256];
    
    // 解析协议部分
    const char* protocol_end = strstr(address_str, "://");
    if (!protocol_end) {
        LOG_ERROR("Invalid address format: missing '://'");
        return false;
    }
    
    size_t protocol_len = protocol_end - address_str;
    if (protocol_len >= sizeof(protocol)) {
        LOG_ERROR("Protocol name too long");
        return false;
    }
    
    strncpy(protocol, address_str, protocol_len);
    protocol[protocol_len] = '\0';
    
    const char* rest = protocol_end + 3;
    
    // 根据协议类型解析地址
    if (strcmp(protocol, "unix") == 0) {
        // Unix Socket: unix:///path/to/socket
        addr->type = SSN_TRANSPORT_UNIX;
        
        strncpy(path, rest, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        
        memset(&addr->addr.unix_addr, 0, sizeof(addr->addr.unix_addr));
        addr->addr.unix_addr.sun_family = AF_UNIX;
        strncpy(addr->addr.unix_addr.sun_path, path, sizeof(addr->addr.unix_addr.sun_path) - 1);
        
        snprintf(addr->address_str, sizeof(addr->address_str), "unix://%s", path);
        
    } else if (strcmp(protocol, "tcp") == 0) {
        // TCP: tcp://host:port
        addr->type = SSN_TRANSPORT_TCP;
        
        char* colon = strchr(rest, ':');
        if (!colon) {
            LOG_ERROR("Invalid TCP address format: missing port");
            return false;
        }
        
        size_t host_len = colon - rest;
        if (host_len >= sizeof(host)) {
            LOG_ERROR("Host name too long");
            return false;
        }
        
        strncpy(host, rest, host_len);
        host[host_len] = '\0';
        
        port = atoi(colon + 1);
        if (port <= 0 || port > 65535) {
            LOG_ERROR("Invalid TCP port: %d", port);
            return false;
        }
        
        memset(&addr->addr.inet_addr, 0, sizeof(addr->addr.inet_addr));
        addr->addr.inet_addr.sin_family = AF_INET;
        addr->addr.inet_addr.sin_port = htons((uint16_t)port);
        
        if (inet_pton(AF_INET, host, &addr->addr.inet_addr.sin_addr) <= 0) {
            // 尝试域名解析
            struct hostent* he = gethostbyname(host);
            if (!he) {
                LOG_ERROR("Failed to resolve host: %s", host);
                return false;
            }
            memcpy(&addr->addr.inet_addr.sin_addr, he->h_addr_list[0], he->h_length);
        }
        
        snprintf(addr->address_str, sizeof(addr->address_str), "tcp://%s:%d", host, port);
        
    } else if (strcmp(protocol, "udp") == 0) {
        // UDP: udp://host:port
        addr->type = SSN_TRANSPORT_UDP;
        
        char* colon = strchr(rest, ':');
        if (!colon) {
            LOG_ERROR("Invalid UDP address format: missing port");
            return false;
        }
        
        size_t host_len = colon - rest;
        if (host_len >= sizeof(host)) {
            LOG_ERROR("Host name too long");
            return false;
        }
        
        strncpy(host, rest, host_len);
        host[host_len] = '\0';
        
        port = atoi(colon + 1);
        if (port <= 0 || port > 65535) {
            LOG_ERROR("Invalid UDP port: %d", port);
            return false;
        }
        
        memset(&addr->addr.inet_addr, 0, sizeof(addr->addr.inet_addr));
        addr->addr.inet_addr.sin_family = AF_INET;
        addr->addr.inet_addr.sin_port = htons((uint16_t)port);
        
        if (inet_pton(AF_INET, host, &addr->addr.inet_addr.sin_addr) <= 0) {
            // 尝试域名解析
            struct hostent* he = gethostbyname(host);
            if (!he) {
                LOG_ERROR("Failed to resolve host: %s", host);
                return false;
            }
            memcpy(&addr->addr.inet_addr.sin_addr, he->h_addr_list[0], he->h_length);
        }
        
        snprintf(addr->address_str, sizeof(addr->address_str), "udp://%s:%d", host, port);
        
    } else {
        LOG_ERROR("Unsupported protocol: %s", protocol);
        return false;
    }
    
    return true;
}

// 地址转换函数
bool ssn_address_to_string(const ssn_address_t* addr, char* buffer, size_t size) {
    if (!addr || !buffer || size == 0) {
        return false;
    }
    
    switch (addr->type) {
        case SSN_TRANSPORT_UNIX:
            snprintf(buffer, size, "unix://%s", addr->addr.unix_addr.sun_path);
            break;
            
        case SSN_TRANSPORT_TCP: {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr->addr.inet_addr.sin_addr, ip_str, sizeof(ip_str));
            snprintf(buffer, size, "tcp://%s:%d", ip_str, ntohs(addr->addr.inet_addr.sin_port));
            break;
        }
            
        case SSN_TRANSPORT_TCP6: {
            char ip_str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &addr->addr.inet6_addr.sin6_addr, ip_str, sizeof(ip_str));
            snprintf(buffer, size, "tcp6://[%s]:%d", ip_str, ntohs(addr->addr.inet6_addr.sin6_port));
            break;
        }
            
        case SSN_TRANSPORT_UDP: {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr->addr.inet_addr.sin_addr, ip_str, sizeof(ip_str));
            snprintf(buffer, size, "udp://%s:%d", ip_str, ntohs(addr->addr.inet_addr.sin_port));
            break;
        }
            
        case SSN_TRANSPORT_UDP6: {
            char ip_str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &addr->addr.inet6_addr.sin6_addr, ip_str, sizeof(ip_str));
            snprintf(buffer, size, "udp6://[%s]:%d", ip_str, ntohs(addr->addr.inet6_addr.sin6_port));
            break;
        }
            
        default:
            LOG_ERROR("Unsupported transport type: %d", addr->type);
            return false;
    }
    
    return true;
}
```

## 7. 连接池管理

### 7.1 连接池接口

```c
// 连接池配置
typedef struct {
    uint32_t max_connections;           // 最大连接数
    uint32_t min_connections;           // 最小连接数
    uint32_t idle_timeout_sec;          // 空闲超时时间
    uint32_t connection_timeout_ms;     // 连接超时时间
    uint32_t max_idle_connections;      // 最大空闲连接数
    bool enable_health_check;           // 启用健康检查
    uint32_t health_check_interval_sec; // 健康检查间隔
} ssn_connection_pool_config_t;

// 连接池统计
typedef struct {
    uint32_t total_connections;         // 总连接数
    uint32_t active_connections;        // 活跃连接数
    uint32_t idle_connections;          // 空闲连接数
    uint32_t failed_connections;        // 失败连接数
    uint32_t connection_requests;       // 连接请求数
    uint32_t connection_timeouts;       // 连接超时数
    uint32_t connection_errors;         // 连接错误数
} ssn_connection_pool_stats_t;

// 连接池接口
typedef struct ssn_connection_pool {
    // 获取连接
    ssn_transport_t* (*acquire)(ssn_connection_pool_t* pool, const ssn_address_t* addr);
    
    // 释放连接
    bool (*release)(ssn_connection_pool_t* pool, ssn_transport_t* transport);
    
    // 关闭连接
    bool (*close)(ssn_connection_pool_t* pool, ssn_transport_t* transport);
    
    // 清理空闲连接
    void (*cleanup_idle)(ssn_connection_pool_t* pool);
    
    // 获取统计信息
    bool (*get_stats)(ssn_connection_pool_t* pool, ssn_connection_pool_stats_t* stats);
    
    // 重置统计信息
    void (*reset_stats)(ssn_connection_pool_t* pool);
    
    // 销毁连接池
    void (*destroy)(ssn_connection_pool_t* pool);
} ssn_connection_pool_t;
```

## 8. 数据流转流程

### 8.1 客户端数据流转

1. **连接建立**：
   - 客户端调用 `ssn_client_connect`，传入地址字符串
   - 地址解析函数 `ssn_address_parse` 解析地址，确定传输类型
   - 传输工厂创建对应类型的传输实例
   - 调用传输实例的 `connect` 方法建立连接

2. **数据发送**：
   - 客户端调用 `ssn_client_call` 或 `ssn_client_message`
   - 消息被封装成协议格式
   - 调用传输实例的 `send` 方法发送数据
   - 数据通过底层协议发送到服务器

3. **数据接收**：
   - 客户端通过 `ssn_client_poll` 或 `ssn_client_run` 等待事件
   - 当有数据到达时，调用传输实例的 `recv` 方法接收数据
   - 数据被解析成协议格式
   - 调用对应的回调函数处理数据

### 8.2 服务器数据流转

1. **服务器启动**：
   - 服务器调用 `ssn_server_start`，传入地址字符串
   - 地址解析函数 `ssn_address_parse` 解析地址，确定传输类型
   - 传输工厂创建对应类型的传输实例
   - 调用传输实例的 `listen` 方法开始监听

2. **连接处理**：
   - 服务器通过 `ssn_server_poll` 或 `ssn_server_run` 等待事件
   - 当有新连接时，调用传输实例的 `accept` 方法接受连接
   - 创建新的客户端实例，处理连接

3. **数据处理**：
   - 当客户端发送数据时，调用传输实例的 `recv` 方法接收数据
   - 数据被解析成协议格式
   - 根据消息类型调用对应的处理函数
   - 处理结果通过传输实例的 `send` 方法发送回客户端

## 9. 错误处理机制

### 9.1 错误类型

```c
// 错误类型定义
typedef enum {
    SSN_ERR_SUCCESS = 0,             // 成功
    SSN_ERR_INVALID_ARGS,            // 无效参数
    SSN_ERR_OUT_OF_MEMORY,           // 内存不足
    SSN_ERR_NET_CONNECT,             // 网络连接错误
    SSN_ERR_NET_READ,                // 网络读取错误
    SSN_ERR_NET_WRITE,               // 网络写入错误
    SSN_ERR_TIMEOUT,                 // 超时
    SSN_ERR_NOT_FOUND,               // 未找到
    SSN_ERR_ALREADY_EXISTS,          // 已存在
    SSN_ERR_PERMISSION_DENIED,       // 权限拒绝
    SSN_ERR_PROTOCOL_ERROR,          // 协议错误
    SSN_ERR_TRANSPORT_ERROR,         // 传输错误
    SSN_ERR_INTERNAL_ERROR           // 内部错误
} ssn_error_t;
```

### 9.2 错误处理策略

1. **传输层错误**：
   - 网络连接错误：自动重连机制
   - 数据传输错误：错误计数和统计
   - 超时错误：超时重传或失败处理

2. **应用层错误**：
   - 无效参数：参数验证和错误返回
   - 资源不足：内存分配失败处理
   - 逻辑错误：错误码返回和日志记录

3. **错误传播**：
   - 错误从底层传输层向上传播
   - 每个层次处理自己能处理的错误
   - 无法处理的错误传递给上层

## 10. 扩展性考虑

### 10.1 新协议支持

- **插件式架构**：通过传输工厂注册新的传输类型
- **统一接口**：所有传输协议实现相同的接口
- **配置灵活性**：通过配置文件或运行时参数选择传输协议

### 10.2 性能扩展

- **零拷贝传输**：减少数据拷贝，提高性能
- **批量传输**：合并小数据包，减少网络开销
- **连接池**：复用连接，减少连接建立开销
- **异步操作**：非阻塞IO，提高并发处理能力

### 10.3 功能扩展

- **安全传输**：支持TLS/DTLS加密传输
- **多播支持**：UDP多播功能
- **流量控制**：拥塞控制和流量整形
- **QoS支持**：服务质量保证

## 11. 关键技术选型

### 11.1 传输协议

| 协议 | 特点 | 适用场景 |
|------|------|----------|
| Unix Socket | 本地通信，高性能，低延迟 | 同一主机内进程间通信 |
| TCP | 可靠传输，面向连接 | 跨网络可靠通信 |
| UDP | 无连接，低延迟 | 实时数据传输，广播/多播 |
| TLS/DTLS | 加密传输 | 安全通信场景 |

### 11.2 设计模式

- **工厂模式**：创建传输实例
- **适配器模式**：适配不同传输协议
- **池化模式**：连接池管理
- **策略模式**：不同传输策略

### 11.3 性能优化

- **非阻塞IO**：提高并发处理能力
- **零拷贝**：减少数据拷贝开销
- **批量传输**：减少网络往返时间
- **连接复用**：减少连接建立开销

## 12. 实现建议

### 12.1 分阶段实施

1. **第一阶段**：实现核心传输接口和Unix Socket适配器
2. **第二阶段**：实现TCP适配器
3. **第三阶段**：实现UDP适配器
4. **第四阶段**：实现连接池和性能优化
5. **第五阶段**：实现TLS/DTLS安全传输

### 12.2 测试策略

1. **单元测试**：测试每个适配器的基本功能
2. **集成测试**：测试适配器与上层模块的集成
3. **性能测试**：测试不同协议的性能表现
4. **兼容性测试**：测试与现有系统的兼容性
5. **压力测试**：测试高并发下的稳定性

### 12.3 部署建议

1. **渐进式部署**：先在小范围部署，逐步扩大
2. **监控和告警**：部署监控系统，及时发现和解决问题
3. **回滚计划**：制定详细回滚计划，确保系统安全
4. **文档和培训**：提供详细文档和培训，确保团队掌握新技术

## 13. 代码风格规范

### 13.1 命名规范

- **前缀**：所有公共函数和数据结构使用 `ssn_` 前缀
- **类型**：使用 `ssn_xxx_t` 命名类型
- **函数**：使用 `ssn_xxx_yyy` 命名函数
- **常量**：使用 `SSN_XXX_YYY` 命名常量

### 13.2 代码格式

- **缩进**：使用4个空格缩进
- **括号**：左括号与语句在同一行，右括号单独一行
- **命名**：使用驼峰命名法或下划线分隔
- **注释**：每个函数和重要数据结构都有详细注释

### 13.3 错误处理

- **返回值**：使用错误码返回错误状态
- **日志**：使用统一的日志接口记录错误
- **异常**：避免使用异常，使用错误码和错误处理函数

## 14. 总结

本设计文档提供了一个完整的传输层架构设计，支持Unix Socket、TCP和UDP三种通信协议，遵循模块化与分层设计原则，确保代码风格规范、代码质量优良、可读性高。通过将协议适配放到transports文件夹中，减轻了核心文件的代码量，提高了代码的可维护性和可扩展性。

该设计参考了nanomsgs库的优点，采用了统一的传输接口和工厂模式，实现了不同协议的适配。同时，通过连接池管理和性能优化策略，提高了系统的性能和可靠性。

新的前缀名 `ssn_` 替代了原有的 `ipc_`，使代码更加清晰和具有辨识度。该设计可以满足边缘计算场景的复杂需求，为cd-ipc-ssn库提供了更加灵活和强大的传输能力。