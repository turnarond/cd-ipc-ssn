# 02_pubsub_chat 示例（C++）—— 发布/订阅聊天室

本示例展示 SSN C++ 服务框架的「发布/订阅」通信模式：聊天服务端每 1 秒向 `/chat` 主题发布一条消息，订阅客户端实时收到并打印。同时演示类型安全 API（与 01_echo 的 JSON 层 API 互补）：

- `RegisterMethod<Req, Resp>()` / `Call<Req, Resp>()`：用 DTO 结构体描述请求/应答，编译期类型检查；
- `SsnService::publish()`：向主题发布消息（任意客户端可订阅）；
- `SsnClient::subscribe()`：订阅主题，收到消息时回调。

## 运行方式

### 一键体验

```bash
make run
```

脚本会自动：后台启动服务端 → 运行客户端（加入聊天室 + 收满 3 条消息后自动退出）→ 关闭服务端。

### 手动运行（两个终端，便于观察）

终端 1——启动聊天服务端：

```bash
make pub_server && ./pub_server
```

终端 2——运行订阅客户端：

```bash
make sub_client && ./sub_client
```

> 服务端是常驻进程，会一直运行直到按 Ctrl+C 优雅退出。

## 预期输出

客户端输出（先打印加入结果，再持续收到聊天消息）：

```
加入结果: 欢迎 小明 加入聊天室！（当前成员 1 人）
[/chat] 第 1 条消息
[/chat] 第 2 条消息
[/chat] 第 3 条消息
```

> 若客户端启动稍慢，可能错过第 1 条消息（消息每 1 秒发布一次），收到 2、3、4 条也是正常现象。

## 代码要点

### DTO：用结构体描述接口（pub_server.cpp / sub_client.cpp）

```cpp
struct JoinReq {              // 请求：加入聊天室
    std::string nickname;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(JoinReq, nickname)   // 侵入式宏：必须写在结构体内部
};
```

`NLOHMANN_DEFINE_TYPE_INTRUSIVE` 在结构体内部注入 `to_json` / `from_json` 实现，
自动生成结构体与 JSON 的互转代码，**成员名即 JSON 字段名**——这是客户端与服务端
之间的接口契约，两端定义必须一致。

### 类型安全方法注册（pub_server.cpp）

```cpp
RegisterMethod<JoinReq, JoinResp>("/chat/join", [this](const JoinReq& req) {
    return JoinResp{"欢迎 " + req.nickname + " 加入聊天室！", ++joined_};
});
```

handler 收到的是反序列化好的 `JoinReq` 对象，返回 `JoinResp` 对象自动序列化为
JSON 应答——字段名拼错、类型写错都在编译期暴露，而不是运行期才报错。请求字段
缺失/类型不符时，框架自动按错误码 1003 应答。

### 独立线程发布（pub_server.cpp）

```cpp
pub_thread_ = std::thread([this] { publishLoop(); });
```

- `publish()` 在 svc 线程之外调用**不持有节点锁**，与 RPC 分发无冲突，安全；
- 但不得在 RPC handler 内调用 `publish()`——handler 在持锁的 poll 线程中执行，会自锁死锁；
- 发布线程用**成员 `std::thread` + 停止标志**管理，而非 `detach`（detach 生命周期失控、退出时序不可控）；
- `OnShutdown` 中顺序固定：先置停止标志 → `join` 回收发布线程 → 再调基类清理（销毁节点）——保证发布线程不会在节点销毁期间访问节点。

### 订阅与回调线程约束（sub_client.cpp）

```cpp
cli.subscribe("/chat", [&](const std::string& topic, const nlohmann::json& data) {
    std::cout << "[" << topic << "] " << data.at("text").get<std::string>() << std::endl;
    ++received;
});
```

- 回调由框架在**内部线程**执行，期间持有节点锁：**不得**在回调中调用本客户端的 `callJson` / `subscribe` / `disconnect`（自锁死锁），只允许拷贝数据、打印、设置标志，并需快速返回；
- 跨线程共享的接收计数用 `std::atomic`；
- 主线程每 0.5 秒检查一次计数，收满 3 条后优雅退出（带超时保护，防无限等待）。

## 常见错误

| 现象 | 原因 | 解决 |
|---|---|---|
| 客户端打印「连接失败」 | 服务端未启动 | 先启动 `./pub_server`，再运行客户端 |
| 客户端打印「未收到足够的聊天消息」 | 服务端未启动或已退出 | 确认服务端在运行；检查两端端口一致（18881） |
| 客户端打印「调用 /chat/join 失败」 | DTO 字段与注册的不一致（如字段缺失） | 核对两端 DTO 定义（成员名即 JSON 字段名） |

## 相关 API

- `ssn::SsnService::RegisterMethod<Req, Resp>()` - 类型安全方法注册（DTO）
- `ssn::SsnService::publish()` - 发布 PubSub 主题消息
- `ssn::SsnClient::Call<Req, Resp>()` - 类型安全同步调用（DTO）
- `ssn::SsnClient::subscribe()` - 订阅 PubSub 主题
