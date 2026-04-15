# cd-ipc-ssn 代码风格规范

## 1. 基本规范

### 1.1 缩进
- 使用 4 个空格进行缩进，不使用制表符
- 大括号 `{` 放在行尾，`}` 放在新行的开始
- 控制语句（if、for、while、switch）的大括号必须使用

### 1.2 命名规范
- **变量名**：使用小写字母，单词之间用下划线分隔（snake_case）
- **函数名**：使用小写字母，单词之间用下划线分隔（snake_case）
- **结构体名**：使用小写字母，单词之间用下划线分隔，后缀为 `_t`
- **常量**：使用全大写字母，单词之间用下划线分隔
- **宏**：使用全大写字母，单词之间用下划线分隔

### 1.3 注释
- **文件头部**：每个文件必须有文件头部注释，说明文件的功能
- **函数注释**：每个函数必须有函数注释，说明函数的功能、参数和返回值
- **行内注释**：使用 `//` 进行行内注释，注释内容与代码之间至少有一个空格
- **块注释**：使用 `/* */` 进行块注释，用于较长的注释内容

### 1.4 头文件包含
- 头文件包含顺序：
  1. 系统头文件（如 <stdio.h>、<stdlib.h> 等）
  2. 第三方库头文件
  3. 自定义头文件（使用相对路径）
- 头文件必须使用 `#ifndef`、`#define`、`#endif` 进行保护

### 1.5 错误处理
- 错误处理必须使用日志记录（`LOG_ERROR`、`LOG_WARNING` 等）
- 函数返回值必须明确表示成功或失败
- 错误码必须使用 `ipc_error.h` 中定义的标准错误码

### 1.6 代码长度
- 函数长度不应超过 200 行
- 每行代码长度不应超过 120 个字符

## 2. 具体实施

### 2.1 头文件
- 头文件命名：使用小写字母，单词之间用下划线分隔，后缀为 `.h`
- 头文件内容：只包含必要的声明，不包含实现
- 头文件保护：使用 `#ifndef`、`#define`、`#endif` 进行保护

### 2.2 源文件
- 源文件命名：使用小写字母，单词之间用下划线分隔，后缀为 `.c`
- 源文件内容：包含函数的实现
- 函数实现：每个函数应尽量保持简洁，功能单一

### 2.3 结构体
- 结构体定义：使用 `typedef struct` 定义结构体类型
- 结构体成员：按类型大小排序，较大的类型放在前面
- 结构体初始化：使用 `memset` 或指定初始化器进行初始化

### 2.4 宏定义
- 宏定义：使用 `#define` 定义常量和宏
- 宏参数：使用括号保护宏参数，避免优先级问题
- 多行宏：使用 `\` 进行换行

### 2.5 日志记录
- 日志级别：使用 `LOG_DEBUG`、`LOG_INFO`、`LOG_WARNING`、`LOG_ERROR` 等
- 日志格式：包含文件名、函数名、行号等信息
- 日志内容：清晰、简洁，包含必要的信息

## 3. 工具使用

### 3.1 代码格式化
- 使用 `clang-format` 进行代码格式化
- 配置文件：使用 `.clang-format` 文件指定格式化规则

### 3.2 静态分析
- 使用 `cppcheck` 进行静态代码分析
- 定期运行静态分析工具，发现潜在问题

### 3.3 代码审查
- 定期进行代码审查，确保代码符合规范
- 审查重点：代码风格、错误处理、性能优化等

## 4. 示例

### 4.1 函数定义示例
```c
/**
 * @brief 初始化 IPC 服务器
 * 
 * @param server 服务器实例
 * @param name 服务器名称
 * @return true 成功，false 失败
 */
bool ipc_server_init(ipc_server_t *server, const char *name)
{
    if (!server || !name) {
        LOG_ERROR("Invalid parameters");
        return false;
    }
    
    // 初始化服务器
    memset(server, 0, sizeof(*server));
    
    // 设置服务器名称
    strncpy(server->name, name, sizeof(server->name) - 1);
    server->name[sizeof(server->name) - 1] = '\0';
    
    return true;
}
```

### 4.2 结构体定义示例
```c
/**
 * @brief IPC 客户端结构体
 */
typedef struct ipc_client {
    bool valid;           // 客户端是否有效
    bool connected;       // 客户端是否已连接
    int sock;             // 套接字描述符
    ipc_stream_ctx_t recv; // 接收缓冲区
    // 其他成员...
} ipc_client_t;
```

### 4.3 错误处理示例
```c
if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    LOG_ERROR("Connect failed: %s", strerror(errno));
    return IPC_ERR_NET_CONNECT;
}
```