# 03_robust_client 示例（C++）—— 稳健客户端：三态分类 + 断线重连

本示例演示真实服务中客户端必须具备的两种稳健性能力：

1. **调用结果三态分类**：`callJson` 返回 `false` 时，业务必须区分「超时」与「服务端框架错误」（如 1001 方法不存在、1003 handler 异常）——分类依据是应答体里是否有 `error.code`；
2. **断线重连**：服务端停机后，客户端按「disconnect → 指数退避 → connect」循环重试，服务端恢复后自动重连成功。

## 运行方式

### 一键体验（推荐）

```bash
make run
```

脚本自动编排三段式演示：

1. 启动服务端 → 客户端完成三态分类（成功 / 超时 / 1001 / 1003）；
2. 停掉服务端 → 客户端进入指数退避重连循环（1s/2s/4s/8s 封顶）；
3. 重启服务端 → 客户端在退避窗口内探测到恢复，重连成功并退出。

预期输出（节选）：

```
[1] /echo       → 成功，应答={"msg":"你好"}
[2] /slow       → 超时（约 300ms 返回，未等待完整 500ms）
[3] /no_such    → 服务端错误（error.code=1001）
[4] /boom       → 服务端错误（error.code=1003）
== 阶段 2：断线重连（停服务端，客户端退避重试）==
尝试 1 ...
尝试 2 ...
尝试 3 ...
重连成功（第 4 次尝试）：{"ping":4}
```

### 手动运行（两个终端）

终端 1：

```bash
make robust_server && ./robust_server
```

终端 2（先正常分类，再体验重连——重连时先停掉服务端，过几秒再启动）：

```bash
make robust_client && ./robust_client          # 三态分类
./robust_client reconnect                       # 断线重连演示
```

## 代码要点

### 三态分类（robust_client.cpp）

`callJson` 的返回值只说明「调用是否成功」；失败时用应答体区分原因：

| 现象 | 判定 | 典型原因 |
|---|---|---|
| 返回 true | 成功 | 正常应答 |
| 返回 false + 应答含 `error.code` | 服务端错误 | 1001 方法不存在 / 1003 handler 异常 |
| 返回 false + 应答无 `error` | 超时 | 服务端处理过慢或已停机 |

示例中的 `classify_call` 把这一逻辑封装为单一入口，业务代码只需按三态分支处理。

### 断线重连（robust_client.cpp）

```cpp
// 核心循环：失败 → disconnect（失活会话）→ 退避 → connect 重试
for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
    if (!cli.connected() && !cli.connect(SERVER_ADDR)) {
        std::this_thread::sleep_for(backoff(attempt));   // 1s/2s/4s/8s 封顶
        continue;
    }
    if (classify_call(cli, "/echo", ..., 2000) == CallResult::Success) {
        std::cout << "重连成功（第 " << attempt << " 次尝试）" << std::endl;
        break;
    }
    cli.disconnect();   // 断开失活会话，下一次 connect 重新建立
    std::this_thread::sleep_for(backoff(attempt));
}
```

要点：

- **指数退避**：`backoff(attempt)` 为 1s/2s/4s/8s 封顶——快速失败、缓慢重试，避免在服务端恢复前高频空转（与 C 层 3 秒连接超时配合，重试间隔小于 3s 无意义）；
- **disconnect 必须先于 connect**：`callJson` 失败后框架侧的连接态可能仍为「已连接」，直接 `connect` 会被拒绝（重复 connect 返回 false），必须先 `disconnect`；
- **RAII 清理**：`SsnClient` 析构自动 `disconnect`（兜底），示例仍显式调用以表达意图；
- **死锁约束**：重连循环在业务线程执行，不触及订阅/消息回调，符合框架锁约束（回调内不调用任何会加 node 锁的 API）。

### 服务端（robust_server.cpp）

- `/slow` 睡眠 500ms——制造「超时」场景的慢处理；
- `/boom` 抛异常——框架捕获后应答错误码 1003，服务端进程不受影响（这正是 C1 加固在服务端的对应行为）；
- 内置 `/health` 可做重连判活探测（示例直接用调用结果判活，未额外探测）。

## 相关 API

- `ssn::SsnClient::callJson()` - 同步调用（超时/框架错误返回 false）
- `ssn::SsnClient::connect()` / `disconnect()` - 连接管理（disconnect 幂等）
- `ssn::SsnClient::connected()` - 连接态查询
