# 01_echo_service 示例（C++）—— 第一个 SSN C++ 服务

本示例是 SSN C++ 服务框架的「Hello World」：一个 echo（回声）服务——客户端发什么，服务端原样返回什么。服务端约 40 行代码，展示框架最核心的两个 API：

- `ServiceManager::Run<T>()`：一行代码完成「初始化 → 启动 → 等待 Ctrl+C → 优雅停止」的完整生命周期；
- `SsnClient::callJson()`：同步 JSON-RPC 调用（发请求、等应答、得结果）。

## 运行方式

### 一键体验

```bash
make run
```

脚本会自动：后台启动服务端 → 等待 2 秒 → 运行客户端 → 客户端退出后关闭服务端。

### 手动运行（两个终端，便于观察）

终端 1——启动服务端：

```bash
make echo_server && ./echo_server
```

终端 2——运行客户端：

```bash
make echo_client && ./echo_client
```

> 服务端是常驻进程，会一直运行直到按 Ctrl+C 优雅退出。

## 预期输出

客户端输出：

```
应答: {"msg":"你好，SSN C++ 框架！","n":42}
```

服务端输出：

```
Echo 服务已启动，监听 tcp://127.0.0.1:18880（Ctrl+C 优雅退出）
```

## 代码要点

### 服务端：继承 + 配置 + 注册（echo_server.cpp）

1. **继承 `SsnService`**：服务端基类封装了节点创建、事件循环、方法分发等全部基础设施，业务代码只需关注「服务是什么」；
2. **构造函数中配置**：`listenTcp("127.0.0.1", 18880)` 设定监听地址；`registerJson("/echo", handler)` 注册方法——URL 即服务接口名；
3. **handler 签名**：`nlohmann::json -> nlohmann::json`，框架负责请求解析与应答回传；handler 抛异常时框架自动应答错误码 1003；
4. **`OnInit` 钩子**：基类 `OnInit` 成功后服务端已真实监听，此时打印启动信息最准确；
5. **一行启动**：`ServiceManager::Run<EchoService>()`——模板参数是服务类型，Run 内部编排 initialize → start → 等待信号 → stop → destroy 全流程。

### 客户端：连接 → 调用 → 断开（echo_client.cpp）

1. **connect**：地址格式与传输层一致（`tcp://host:port`），失败返回 false（最常见原因：服务端未启动）；
2. **callJson**：同步调用——阻塞至收到应答，超时（默认 3 秒）或服务端返回框架错误（如 1001 方法不存在）时返回 false；
3. **disconnect**：断开连接（析构函数也会兜底清理）。

### 内置端点（额外收获）

服务端自带三个管理接口，可直接用任意客户端调用体验：

| URL | 说明 | 示例应答 |
|---|---|---|
| `/urls` | 已注册方法列表 | `{"urls":["/echo","/health","/urls","/version"]}` |
| `/health` | 健康状态、连接数与累计消息数 | `{"status":"ok","connections":0,"messages":0}` |
| `/version` | 框架版本 | `{"version":"2.3.2"}` |

## 常见错误

| 现象 | 原因 | 解决 |
|---|---|---|
| 客户端打印「连接失败」 | 服务端未启动，或先退出了 | 先启动 `./echo_server`，再运行客户端 |
| 客户端打印「调用失败」 | 调用了未注册的 URL（框架应答 1001 错误） | 检查 URL 与 `registerJson` 注册的一致 |
| `make` 报找不到 `libssn_framework.so` | 框架库未构建 | 先构建库：`mkdir -p build && cd build && cmake .. && make` |

## 相关 API

- `ssn::ServiceManager::Run<T>()` - 一行启动服务（完整生命周期编排）
- `ssn::SsnService::listenTcp()` - 配置 TCP 监听地址
- `ssn::SsnService::registerJson()` - 注册 JSON 层 RPC 方法
- `ssn::SsnClient::connect()` - 连接服务端
- `ssn::SsnClient::callJson()` - 同步 JSON-RPC 调用
