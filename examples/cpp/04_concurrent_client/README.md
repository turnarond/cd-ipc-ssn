# 04_concurrent_client 示例（C++）—— 并发调用：两层串行化与正确性

本示例用实测耗时回答一个问题：**多线程并发调用时，共用一个 `SsnClient` 还是各用各的？**

实测结论（两次 `/slow`，各 500ms）：

| 方式 | 排队位置 | 总耗时实测 |
|---|---|---|
| 单 client + 双线程 | client 端：单 in-flight 互斥锁，后到者等待 | ≈ 2×500ms |
| 双 client + 双线程 | 服务端：单线程 poll 串行执行 handler | ≈ 2×500ms |

即：本框架调用链路存在**两层串行化**——client 端单 in-flight（`callJson` 互斥）与服务端单线程分发（handler 在 poll 线程内执行）。两条并发路径的排队位置不同，但都避免不了其中一层。需要真正并行吞吐时，应横向扩展（多服务进程/节点）。

## 运行方式

### 一键体验

```bash
make run
```

预期输出（耗时随负载浮动）：

```
[串行化] 单 client × 双线程两次 /slow：1104ms（≈2×500ms，第二次调用在队列中等待第一次完成），成功=是
[双 client] 双 client × 双线程两次 /slow：1102ms（请求同时到达，服务端串行处理 handler，仍 ≈2×500ms），成功=是
[正确性] 并发 /echo_id 应答配对：id 匹配=是，序号互不重复=是（seq=1/2）
```

### 手动运行（两个终端）

终端 1：`make concurrent_server && ./concurrent_server`
终端 2：`make concurrent_client && ./concurrent_client`

## 代码要点

### 演示 1：单 client 串行化（client 端排队）

`SsnClient::callJson` 是单 in-flight 同步调用：同一 client 的并发调用被内部互斥锁串行化，后到者排队。两个线程各自发起 `/slow` → 第二个调用等待第一个完成后才真正发出 → 总耗时 ≈ 2×500ms。

### 演示 2：双 client（服务端排队）

两个独立 client 同时发出请求 → 请求都真实到达服务端，但服务端 `SsnService` 的 handler 在单个 poll 线程内执行（且持有 node->lock）——两次 `/slow` 仍被服务端逐个执行 → 总耗时同样 ≈ 2×500ms。与演示 1 的差别仅在**排队位置**：client 端队列 vs 服务端队列。

### 演示 3：并发正确性（为什么串行化是特性而非缺陷）

`/echo_id` 回显请求 id 并附服务端自增序号。并发调用下断言：

- 每个线程收到**自己的**应答（`id` 与请求一致）；
- 两个序号互不重复（无迟到应答覆盖、无错配）。

单 in-flight 串行化保证了应答与请求严格一一配对——这是共享同一 client 会话（同一连接、同一订阅上下文）时的正确性基石。

### 需要真并行怎么办？

- **吞吐优先**：多服务进程/节点横向扩展（每个节点独立 poll 线程，互不排队）；
- **单进程内**：框架当前不提供并行 handler 分发（poll 线程模型 + node->lock 串行化），避免在 handler 内做长阻塞操作，长任务可拆分为异步步骤或投递给业务线程池。

## 相关 API

- `ssn::SsnClient::callJson()` - 同步调用（单 in-flight 串行化）
- `ssn::SsnClient::connect()` - 每实例独立连接
