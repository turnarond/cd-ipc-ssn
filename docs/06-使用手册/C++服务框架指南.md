# C++ 服务框架指南

> SSN C++ 服务框架（v2.5.4）是构建于 SSN C API 之上的服务开发框架：提供类型安全的 RPC
> 方法注册与调用、内置管理端点、发布/订阅与一行式生命周期编排，面向「快速开发 IPC 服务」
> 的使用场景。目标：**30 秒跑通第一个服务**。本文档内容以真实代码为准，示例代码摘录自
> `examples/cpp/`（01_echo_service、02_pubsub_chat 等 4 个示例），可直接复制运行。

## 1. 为什么用 C++ 服务框架

SSN 的 C API（`ssn_node_t` / `ssn_client_t` / `ssn_server_t`）功能完整，但使用成本高：
需要手工管理节点生命周期、自行驱动 poll 事件循环、手写 JSON 编解码与错误处理。C++
服务框架在这些能力之上做了一层「面向服务开发」的封装，两者对比如下：

| 维度 | C API（node 层） | C++ 服务框架 |
|------|------------------|--------------|
| 服务启动 | 手工 create/start + 自写 poll 循环 | `ServiceManager::Run<T>()` 一行启动，框架编排完整生命周期 |
| 调用方式 | 异步回调（callback 由事件循环驱动） | 同步调用（`callJson` / `Call<Req,Resp>` 阻塞至收到应答） |
| 序列化 | 手写 JSON / 字节流编解码 | 类型安全 DTO（`NLOHMANN_DEFINE_TYPE_INTRUSIVE`），编译期类型检查 |
| 方法注册 | `ssn_node_add_rpc_method` + 手工 URL 匹配 | `registerJson` / `RegisterMethod<Req,Resp>` 直接映射 URL → handler |
| 管理接口 | 无 | 内置端点 `/urls` `/health` `/version` |
| 错误处理 | 手工解析应答与错误码 | 统一错误应答体 `{"error":{"code","message"}}`（1001-1004） |
| 生命周期 | 手工状态机（create/start/stop/destroy） | `ServiceBase` 状态机 + 信号停止，stop/destroy 自动编排 |

框架分层（自底向上）：SSN C 库（`libssn_transport.so`）→ C++ 框架（`libssn_framework.so`，
构建于节点抽象层之上）→ 你的服务代码。框架组件职责：

| 组件 | 职责 |
|------|------|
| `ServiceBase` | 服务生命周期状态机（Created/Initialized/Started/Stopped），提供 `OnInit`/`OnShutdown` 钩子 |
| `ServiceTask` | 线程池服务基类：`svc()` 线程入口 + `activate`/`wait`/`requestShutdown` 线程管理 |
| `ServiceManager` | 服务管理器：`Run<T>()` 一行编排「初始化 → 启动 → 等待信号 → 优雅停止」 |
| `SsnService` | 服务端基类：TCP 监听、方法注册与分发、内置端点、`publish` 发布 |
| `SsnClient` | 客户端：连接管理、同步调用、PubSub 订阅 |

## 2. 5 分钟快速开始

### 2.1 构建框架库

框架库与 C 库在同一构建中产出（新增目标 `ssn_framework`）：

```bash
mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

产物：`libssn_transport.so`（C 库）与 `libssn_framework.so`（C++ 框架库）。

### 2.2 复制 echo 示例并运行

最快路径：直接跑仓库自带的 echo 示例（服务端 + 客户端，共约 80 行）。

```bash
cd examples/cpp/01_echo_service
make run
```

`make run` 自动完成：后台启动服务端 → 等待 2 秒 → 运行客户端 → 关闭服务端。

### 2.3 从零编写并编译自己的第一个服务

复制 `examples/cpp/01_echo_service/echo_server.cpp`（完整代码见 3.1 节），按示例
Makefile 的编译命令编译（需 C++17 与 pthread）：

```bash
g++ -std=c++17 -Wall -Wextra \
    -I<仓库>/include -I<仓库>/src -I<仓库>/third_party \
    echo_server.cpp -L<仓库>/build -Wl,-rpath,<仓库>/build \
    -lssn_framework -lssn_transport -lpthread -o echo_server
./echo_server
```

### 2.4 预期输出

服务端启动后打印：

```
Echo 服务已启动，监听 tcp://127.0.0.1:18880（Ctrl+C 优雅退出）
```

在另一个终端运行客户端（`echo_client`，或 `make echo_client && ./echo_client`），
客户端输出：

```
应答: {"msg":"你好，SSN C++ 框架！","n":42}
```

服务端是常驻进程，按 `Ctrl+C` 优雅退出。

## 3. 服务端开发

### 3.1 最小服务：继承 SsnService

服务端开发分三步：**继承 `SsnService` → 构造函数中配置监听与注册方法 → 一行启动**。
以下为 `examples/cpp/01_echo_service/echo_server.cpp` 的真实代码：

```cpp
#include "ssn/framework/ServiceManager.hpp"
#include "ssn/framework/SsnService.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

// 服务定义：继承 SsnService，构造函数中完成「监听 + 注册方法」配置
class EchoService : public ssn::SsnService {
public:
    EchoService() {
        // 监听配置：TCP 地址 127.0.0.1:18880（必须在服务初始化前调用）
        listenTcp("127.0.0.1", 18880);

        // 注册 RPC 方法：URL 即服务接口名；handler 收到请求 JSON，返回应答 JSON
        registerJson("/echo", [](const nlohmann::json& req) -> nlohmann::json {
            return req;   // echo 语义：请求是什么，应答就是什么
        });
    }

    // 初始化钩子：基类 OnInit 完成后服务端已真实监听，此时打印启动信息
    bool OnInit(int argc, char** argv) override {
        if (!ssn::SsnService::OnInit(argc, argv)) {
            return false;
        }
        std::cout << "Echo 服务已启动，监听 tcp://" << listenHost() << ":"
                  << listenPort() << "（Ctrl+C 优雅退出）" << std::endl;
        return true;
    }
};

int main(int argc, char** argv) {
    // 一行启动：完整生命周期由 ServiceManager 编排，收到信号后自动优雅退出
    return ssn::ServiceManager::Run<EchoService>(argc, argv);
}
```

要点：

- `listenTcp(host, port)`：设定监听地址，必须在服务初始化前调用；未调用时默认
  监听 `127.0.0.1:18888`；
- `registerJson(url, handler)`：handler 签名 `nlohmann::json(const nlohmann::json&)`，
  URL 以 `/` 开头；重复注册同一 URL 返回 `false`；内置端点（`/`、`/urls`、`/health`、
  `/version`）为保留路径，拒绝注册；
- `OnInit` 钩子：调用基类 `SsnService::OnInit` 成功后服务端已真实监听，此时打印
  启动信息最准确；返回 `false` 表示初始化失败，`Run` 将退出并返回 1。

### 3.2 类型安全方法：RegisterMethod<Req, Resp> 与 DTO

JSON 层 API（`registerJson`）灵活但无类型检查；类型安全层用 DTO 结构体描述请求/
应答，字段拼错、类型写错在编译期暴露。来自 `examples/cpp/02_pubsub_chat/pub_server.cpp`：

```cpp
#include <nlohmann/json.hpp>
#include <string>

// DTO：成员名即 JSON 字段名（两端一致即接口契约）。
// NLOHMANN_DEFINE_TYPE_INTRUSIVE 必须写在结构体内部（侵入式宏）
struct JoinReq {                        // 加入聊天室的请求
    std::string nickname;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(JoinReq, nickname)
};

struct JoinResp {                       // 加入聊天室的应答
    std::string welcome;
    int member_count;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(JoinResp, welcome, member_count)
};

// 服务端注册：URL + DTO 化 handler，返回 DTO 对象自动序列化为 JSON 应答
RegisterMethod<JoinReq, JoinResp>("/chat/join", [this](const JoinReq& req) {
    return JoinResp{"欢迎 " + req.nickname + " 加入聊天室！", ++joined_};
});
```

注意：**Req/Resp 需默认可构造**（nlohmann 的 `from_json` 语义要求），无默认构造的
DTO 会导致编译失败。`RegisterMethod` 的 handler 收到反序列化后的 `Req` 对象，
返回 `Resp` 对象自动序列化；请求字段缺失/类型不符时由框架按错误码 1003 应答。

### 3.3 发布：publish 与发布线程

`publish(topic, json)` 向 PubSub 主题发布消息，任意客户端可订阅。**publish 只能在
svc 线程之外调用**（如独立线程）——服务端 handler 在持节点锁的 poll 线程内执行，
在其中调用 `publish` 会自锁死锁。推荐模式（来自 pub_server.cpp）：

```cpp
// OnInit 中启动发布线程（独立线程：publish 不持有节点锁，安全）
bool OnInit(int argc, char** argv) override {
    if (!ssn::SsnService::OnInit(argc, argv)) {
        return false;
    }
    pub_thread_ = std::thread([this] { publishLoop(); });
    return true;
}

// 优雅停止：顺序不可颠倒——基类清理会销毁节点，发布线程必须先退出
void OnShutdown() override {
    stop_pub_ = true;                  // ① 置停止标志，发布循环据此退出
    if (pub_thread_.joinable()) {
        pub_thread_.join();            // ② 等待发布线程结束（至多一个发布周期）
    }
    ssn::SsnService::OnShutdown();     // ③ 基类清理：停 svc 线程并销毁节点
}

void publishLoop() {
    std::this_thread::sleep_for(std::chrono::seconds(1));   // 等客户端完成订阅
    int seq = 0;
    while (isRunning() && !stop_pub_) {
        publish("/chat", {{"text", "第 " + std::to_string(++seq) + " 条消息"}});
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
```

发布线程用成员 `std::thread` + 停止标志管理，不要 `detach`（生命周期失控、退出
时序不可控）；`OnShutdown` 中顺序固定：先置标志 → join 回收 → 再调基类清理。

### 3.4 内置端点

服务自带三个管理接口，可用任意客户端直接调用：

| URL | 说明 | 示例应答 |
|-----|------|----------|
| `/urls` | 已注册方法列表 | `{"urls":["/echo","/health","/urls","/version"]}` |
| `/health` | 健康状态、连接数与累计消息数 | `{"status":"ok","connections":0,"messages":0}` |
| `/version` | 框架版本（`ssn_version_get_string()`） | `{"version":"2.4.3"}` |

### 3.5 异常与错误码

应答体为 JSON 对象，失败时返回 `{"error": {"code": <int>, "message": "<中文描述>"}}`，
框架错误码如下：

| 错误码 | 含义 | 触发场景 |
|--------|------|----------|
| 1001 | 方法不存在 | 调用了未注册的 URL（含 `/` 兜底路径） |
| 1002 | 请求体 JSON 解析失败 | 请求体不是合法 JSON |
| 1003 | handler 执行期间任何异常 | Req 反序列化失败（字段缺失/类型不符），或用户 handler 自身抛出异常 |
| 1004 | 客户端超时（客户端侧分类号） | 不产生错误应答体：超时仅表现为 `callJson`/`Call` 返回 false，应答体中不会出现 code=1004 |

客户端侧表现：`callJson` / `Call` 收到框架错误应答（1001-1003）或超时（1004）均
返回 `false`，调用方无需区分（需要区分时可自行解析应答体中的 error 字段；
注意超时不产生应答体，只能靠返回 false 识别）。

### 3.6 死锁约束（重要）

服务端 handler 在节点 poll 线程（svc）内执行，期间持有 C 层 `node->lock`——
**handler 内不得调用任何会加 node 锁的 API**（`publish` / `rpc_call` /
`subscribe` / `send_to_peer` / `get_stats` / `stop` 等），否则自锁死锁。需要
发布消息时，应在独立线程中调用 `publish`（见 3.3 节）。

## 4. 客户端开发

### 4.1 连接：connect

`SsnClient` 封装了客户端节点与内部驱动线程（自动 poll 事件循环），用法
（来自 `examples/cpp/01_echo_service/echo_client.cpp`）：

```cpp
#include "ssn/framework/SsnClient.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

int main() {
    ssn::SsnClient cli;
    if (!cli.connect("tcp://127.0.0.1:18880")) {   // 地址格式与传输层一致
        std::cerr << "连接失败：请确认 echo_server 已启动" << std::endl;
        return 1;
    }

    nlohmann::json req = {{"msg", "你好，SSN C++ 框架！"}, {"n", 42}};
    nlohmann::json resp;
    if (cli.callJson("/echo", req, resp)) {        // 同步调用：阻塞至收到应答
        std::cout << "应答: " << resp.dump() << std::endl;
    } else {
        std::cerr << "调用失败" << std::endl;
        cli.disconnect();
        return 1;
    }

    cli.disconnect();   // 析构函数也会兜底清理，显式调用更清晰
    return 0;
}
```

- `connect(peer_address, timeout_ms = 5000)`：地址格式 `tcp://host:port`；仅创建并
  启动客户端节点，**不接触服务器**（服务端未启动时 connect 仍返回 `true`），实际
  连接发生在首次 `callJson` / `subscribe`，其失败以返回 `false` 呈现（内部 C 层
  节点连接同步超时固定 3 秒）；已连接时重复调用返回 `false`；
- `disconnect()`：停止驱动线程并销毁节点；未连接时是幂等空操作。

### 4.2 同步调用：callJson 与 Call<Req, Resp>

| API | 请求/应答形态 | 用途 |
|-----|--------------|------|
| `callJson(url, req, resp, timeout_ms = 3000)` | `nlohmann::json` | 通用 JSON 调用 |
| `Call<Req, Resp>(url, req, resp, timeout_ms = 3000)` | DTO 对象 | 类型安全调用，编译期类型检查 |

两者语义一致：

- **同步阻塞**：阻塞至收到应答，超时（默认 3 秒）或服务端返回框架错误（1001-1003）
  时返回 `false`；
- **单 in-flight 限制**：同一 `SsnClient` 的并发调用被内部互斥串行化，后到者排队
  等待——不要在同一客户端上发起需要并行的调用，多并发请使用多个 `SsnClient`
  实例；
- **超时竞态（Issue #5-7）**：同一客户端超时失败后立即发起下一次调用，理论上
  有极窄窗口被迟到应答覆盖（C API 无请求序号回调，不可修）；实际超时路径的
  应用应避免紧接重试（如先断开重连、或改用新客户端实例）；
- `Call` 的 Resp 反序列化失败会向调用方抛异常（DTO 与应答不匹配属编程错误）。

DTO 定义与服务端一致（`NLOHMANN_DEFINE_TYPE_INTRUSIVE`，成员名即 JSON 字段名），
示例见 3.2 节与 `examples/cpp/02_pubsub_chat/` 两端代码。

### 4.3 订阅：subscribe

```cpp
std::atomic<int> received{0};   // 回调在别的线程，共享计数须用原子类型
bool subscribed = cli.subscribe("/chat", [&](const std::string& topic,
                                             const nlohmann::json& data) {
    std::cout << "[" << topic << "] " << data.at("text").get<std::string>() << std::endl;
    ++received;
});
if (!subscribed) { /* 订阅失败：服务端未启动等 */ }
```

- `subscribe(topic, handler, timeout_ms = 5000)`：**异步订阅**——返回 `true` 仅表示
  SUBSCRIBE 请求已发送，服务端确认在后台进行，订阅生效以收到该主题消息为准
  （C 层为 fire-and-forget 发送，非同步握手）；`unsubscribe(topic)` 取消订阅；
  建议订阅后先轮询服务端 `/health` 或等待首条消息作为就绪确认；
- **回调线程约束**：回调在内部驱动线程执行，期间持有节点锁——回调内**不得**调用
  本客户端的 `callJson` / `subscribe` / `unsubscribe` / `disconnect`（会自锁死锁或
  必然超时），只允许拷贝数据、打印、设置标志，并需快速返回；
- 跨线程共享的计数/标志用 `std::atomic` 或加锁保护。

### 4.4 常见错误排查

| 现象 | 原因 | 解决 |
|------|------|------|
| 客户端 `connect` 返回 false | 参数非法（地址格式错误）、重复连接 | `connect` 不接触服务器，服务端未启动不会在此失败 |
| `callJson` / `subscribe` 返回 false | 服务端未启动、连接失败、调用超时、或方法未注册（框架应答 1001） | 先启动服务端，再运行客户端；检查 URL 与注册的一致 |
| 调用超时返回 false | 服务端未应答（网络异常/服务端退出）或同一客户端并发调用排队 | 检查服务端运行状态；并发场景拆分为多个客户端 |
| `Call` 抛异常 | Resp DTO 与服务端应答不匹配 | 核对两端 DTO 定义 |

## 5. 生命周期与部署

### 5.1 一行启动：ServiceManager::Run<T>

```cpp
int main(int argc, char** argv) {
    return ssn::ServiceManager::Run<EchoService>(argc, argv);
}
```

`Run<T>` 编排完整生命周期：安装信号处理（SIGINT/SIGTERM）→ `initialize` → `start`
→ 等待停止信号 → `stop` → `destroy` → 返回 0。服务是常驻进程，按 `Ctrl+C` 优雅退出。

注意：**Run 返回时会恢复调用前的信号掩码与信号处理器**（v2.4.1 起，Issue #5-2）
——不向调用方泄漏信号状态，并复位停止标志，因此 `Run` 支持在测试/嵌入场景下
重复调用；定位仍建议作为 `main` 里的最后一步。

### 5.2 手动模式：initialize / start / stop / destroy

需要自定义生命周期时（如嵌入其他主循环），直接使用 `ServiceBase` 提供的四个
final 方法：

```cpp
EchoService svc;
if (!svc.initialize(argc, argv)) { return 1; }   // Created → Initialized（OnInit）
if (!svc.start())               { return 1; }    // Initialized → Started
// ... 运行期间 ...
svc.stop();                                      // Started → Stopped（OnShutdown）
svc.destroy();                                   // 任意状态 → Created
```

- `initialize` / `start` 失败时自动回退上一状态，返回 `false`；
- **销毁前必须 `stop`**：`destroy` 在 Started 状态会先 `stop` 再回收，但显式
  `stop` 是所有权约定，保证 `OnShutdown` 钩子按你预期的时机执行；
- `initialize` / `start` 为串行调用（重复调用返回失败），不要在多个线程中同时
  驱动同一服务的生命周期。

### 5.3 钩子与优雅退出

- `OnInit(argc, argv)`：初始化钩子，基类实现创建节点并注册方法（含内置端点）；
  返回 `false` 中止启动；
- `OnShutdown()`：停止钩子，先回收你自己的资源（如 join 发布线程），再调基类
  `OnShutdown`（停 svc 线程并销毁节点）——顺序不可颠倒；
- `ServiceTask` 的 `svc()` 是线程入口：以 `while (isRunning())` 驱动事件循环，
  `requestShutdown` 置停止标志，`wait` 回收线程。

### 5.4 部署要点

- **依赖库**：运行时需 `libssn_framework.so` 与 `libssn_transport.so` 同时可用
  （示例用 `-Wl,-rpath` 指向构建目录，部署时用 `LD_LIBRARY_PATH` 或安装到系统
  库目录）；
- **端口**：`listenTcp` 设定监听地址，未调用时默认 `127.0.0.1:18888`；示例使用
  18880（echo）与 18881（chat）两个端口；
- **幂等性**：`disconnect` 未连接时为空操作、`destroy` 任意状态安全、`Run` 支持
  重复调用——停止路径刻意设计为幂等，便于脚本与编排系统反复启停；
- 更完整的安装（`cmake --install`）说明见 `docs/05-部署手册/部署手册.md`。

## 6. 示例索引与限制

### 6.1 示例索引（examples/cpp/）

| 目录 | 内容 | 演示要点 | 端口 |
|------|------|----------|------|
| `01_echo_service` | echo_server.cpp + echo_client.cpp | 最小服务、JSON 层 API、一行启动、内置端点 | 18880 |
| `02_pubsub_chat` | pub_server.cpp + sub_client.cpp | 类型安全层（RegisterMethod/Call + DTO）、publish/subscribe | 18881 |
| `03_robust_client` | robust_client.cpp | 三态错误处理（连接/调用/订阅）、重连退避、RAII | 18882 |
| `04_concurrent_client` | concurrent_client.cpp | 两层串行化并发客户端（单客户端排队 + 多客户端并行） | 18883 |

每个目录自带 Makefile：`make` 构建、`make run` 一键体验（后台起服务端 → 运行
客户端 → 关闭服务端）、`make clean` 清理。

### 6.2 首批限制（如实标注）

- **仅 TCP 监听**：`SsnService::listenTcp` 只支持 TCP 监听（框架层节点配置未暴露
  Unix Domain Socket 字段）；客户端 `connect` 的 `peer_address` 使用传输层地址
  格式（`tcp://host:port`、`unix:///path` 等）；
- **单 in-flight**：同一 `SsnClient` 的并发调用串行化（见 4.2 节）；服务端 handler
  与订阅回调的持锁约束见 3.6 / 4.3 节；
- **同步超时固定**：客户端 connect 的 C 层连接超时固定 3 秒（参数暂留扩展）；
- **错误码归并**：客户端 `callJson` / `Call` 对超时与框架错误统一返回 `false`，
  不区分 1004 与 1001-1003（需要区分时解析应答体 error 字段）。

### 6.3 相关文档

- [README](../../README.md) —— 仓库总览与 C API 快速开始
- [快速上手](快速上手.md) —— C API 5 分钟入门
- [API使用指南](API使用指南.md) —— C API 权威参考
- [架构白皮书](../01-白皮书/架构白皮书.md) —— 总体架构与 C++ 服务框架定位
- [部署手册](../05-部署手册/部署手册.md) —— 构建、安装与集成
