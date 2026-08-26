# 事件循环归属收敛设计（Spec）——Issue #31

## 文档信息

| 项目 | 内容 |
|------|------|
| 文档版本 | v1.0 |
| 创建日期 | 2026-08-24 |
| 状态 | 已批准 |
| 关联 Issue | [#31 技术债：client/server 层与协议层事件循环双轨并存](https://github.com/turnarond/cd-ipc-ssn/issues/31) |
| 关联分支 | `feature/event-loop-unify`（实施时创建） |
| 目标版本 | 与 DDS 阶段 1 同车（v2.6.0，见第 7 节） |

## 1. 背景

### 1.1 问题（Issue #31）

代码存在两套并行的事件循环实现。经读码精确化，实际重复度如下：

**已单份复用（无需改动）——发送路径与协议状态**：

- server 持有协议对象并 bind：`ssn_rpc_rep_t` / `ssn_pubsub_pub_t` / `ssn_msg_recv_t`
  （src/ssn_server.c:114-116、567-569）
- client 持有协议对象并 connect：`ssn_rpc_req_t` / `ssn_pubsub_sub_t` / `ssn_msg_send_t`
  （src/ssn_client.c:94-96、836-838）

**双轨并存（本规格收敛范围）——接收分发与请求关联**：

| 职责 | client/server 自建路径（生产主路径） | 协议层独立路径（孤立） |
|---|---|---|
| 就绪检测 | `pselect` + fds（承载多连接/握手/evtfd/定时器/引用计数生命周期）：src/ssn_client.c:1718 起、src/ssn_server.c:1667 起 | 协议对象自持 transport 盲 recv：`ssn_protocol_poll`（src/protocol/ssn_protocol.c:138）→ `ssn_rpc_poll` / `ssn_pubsub_poll` / `ssn_msg_poll` |
| 解帧后分发 | 按 header type 直发：client `handle_response`（ssn_client.c:1084）/ `handle_publish`（:1011）；server `ssn_server_input` | 各自 recv+parse+dispatch（如 ssn_rpc.c:299 起） |
| RPC 请求关联 | client 层自有 pending 池 + seqno 映射（ssn_client.c:70/72，2.5.3 加锁修复） | 协议层另有一份 `rpc_pending_entry_t` 池（定义于 src/protocol/rpc/ssn_rpc.h:36/45，服务裸 `ssn_rpc_call`） |

协议层 poll 在生产代码中**零调用**：`ssn_protocol_poll` 唯一调用点是内部 run 循环（ssn_protocol.c:168），而 `ssn_protocol_run` 全仓库零调用；实际消费者仅测试代码——`test_protocol.c` 与 `test/test_protocol_integration` 直接调用 `ssn_rpc_poll` 自转收发。

### 1.2 危害

1. **语义漂移实例已发生**：v2.5.1 修 server poll 负数超时、v2.3.1 修 client poll 毫秒换算、#19 修「RPC 应答消息类型两套体系」——同一职责两处实现的必然后果
2. **认知负担**：公开导出的 `ssn_protocol_poll` 无生产消费者，演进者无法判断权威实现
3. **DDS 阶段 1 前置风险**：DCPS 概念层若建立在错误一轨上，返工代价更大

### 1.3 为什么不是「client/server 复用协议层 poll」（反向方案否决）

协议层 poll 的「单 transport 单对象自转循环」模型结构上无法承载：

- server 的 listen + 多客户端 + evtfd 多 fd 管理（fd_set 归属 VSI/上层，塞进协议层即破坏分层）
- 握手状态机、pending 定时器线程、引用计数延迟释放等生命周期机制

这些正是 v2.4.4~2.5.8 八个稳定化版本加固出的资产。让 client/server 反向收敛等于推倒重做 P0 高危区，风险收益完全不成比例。

## 2. 设计原则与目标形态

> **事件循环唯一权威 = client/server 层；协议层退化为「纯状态机 + 编解码」，
> 不拥有循环；接收分发逻辑单份化到协议层；锁全部留在上层。**

参照 nanomsg/ZeroMQ 形态：transport 只提供 fd 与非阻塞读写原语；core loop 唯一；
协议是有状态机但无自有线程/循环，由 core 分发事件驱动。

```
现状：                                目标：
client/server 循环                    client/server 循环（唯一权威，不变）
  └─ stream_feed 解帧                  └─ stream_feed 解帧
      └─ 自己解析分发（双轨之一）          └─ 调协议层 handle 原语（唯一分发实现）
                                            └─ 协议状态机更新 + 触发回调
协议层 poll（双轨之二，孤立）
```

## 3. 详细设计

### 3.1 上行收敛：协议层 handle 原语（纯新增 API）

新增「无 I/O、无锁假设、纯函数式」的接收处理入口，收编现有分散的分发逻辑：

```c
/* ssn_rpc.h */
int ssn_rpc_handle_reply(ssn_rpc_req_t *req, const ssn_header_t *hdr);
    /* 应答匹配 pending → 触发 on_reply（回调由调用方上下文执行） */

int ssn_rpc_handle_request(ssn_rpc_rep_t *rep, const ssn_header_t *hdr);
    /* 方法表查找 → 触发 on_request；应答仍由上层显式调 ssn_rpc_response */

/* ssn_pubsub.h */
int ssn_pubsub_handle_message(ssn_pubsub_sub_t *sub, const ssn_header_t *hdr);

/* ssn_msg.h */
int ssn_msg_handle_data(ssn_msg_recv_t *recv, const ssn_header_t *hdr);
```

约束：

- **红线：锁留在上层，协议层无锁**——handle 原语假定调用方已串行化（poll 线程内），
  协议对象内部的注册表访问沿用现状加锁策略，不得引入新锁序
- 回调一律在 handle 返回前同步触发（与现状一致），不改变「回调内禁止 close」契约

### 3.2 client/server 接收路径瘦身

- `ssn_client_process_events` 解帧后的 `handle_response` / `handle_publish` 改为薄壳：
  判断消息类型 → 调对应 handle 原语；类型判断只留一处
- server `ssn_server_input` 同样瘦身为「类型路由 → handle 原语」
- 行为等价性由既有回归安全网保障（见第 6 节）

### 3.3 pending 表归属裁决（双池问题）

以 **client/server 层池为唯一权威**（超时定时器、锁序已在 2.5.3 加固）：

- client 层 `pending_pool + seqno_to_index`：保持不变
- 协议层 `rpc_pending_entry_t` 池（仅服务裸 `ssn_rpc_call` 场景）：
  - 头文件标注「独立于 ssn_client 使用时的简易 pending，两者不可混用同一连接」
  - 不合并实现（避免动高危区）；混用禁令写入两个头文件注释与协议层设计文档
- 后续若出现真实混用需求，另立 Issue 决策（本规格不扩scope）

### 3.4 `ssn_protocol_poll` / `ssn_protocol_run` 重定义

不删公开符号（minor 内破坏 ABI 违反 SemVer），改为**语义重定义 + 标注**：

- `ssn_protocol_poll(ctx, timeout_ms)` 重实现为「非阻塞单步模式」：
  至多一次 try-recv + 调对应 handle 原语，不再自建 select 循环、不再阻塞等待
- `ssn_protocol_run` 标注 deprecated 语义（文档注明「仅供无上层循环的嵌入场景/测试」），
  实现改为 `while(running) poll(100)` 薄壳
- 头文件 @note 明确：「生产应用应使用 ssn_client_poll / ssn_server_poll / ssn_node_poll；
  本接口为协议层独立嵌入模式」

### 3.5 文档同步点（文档腐败即 BUG）

| 文档 | 变更 |
|---|---|
| `docs/03-设计/协议层模块化设计.md` | 新增「事件循环归属」章节（权威循环=client/server 层；协议层=状态机+编解码+可选单步模式）；现状声明更新 |
| `docs/03-设计/架构设计总览.md` | 补一句事件循环归属说明 |
| `docs/01-白皮书/架构白皮书.md` | 2.2 分层说明表补「事件循环由 client/server 层唯一承载」 |
| `src/protocol/*.h`、`src/ssn_client.h`、`src/ssn_server.h` | 头文件 @note 同步（含 pending 混用禁令） |
| `CHANGELOG.md` | v2.6.0 条目记录（Added: handle 原语；Changed: poll 单步语义） |

## 4. 与 DDS 阶段 1 的关系

- 本规格作为阶段 1（DCPS 概念模型，DR-01~03）的**地基前置**：概念层的
  Publisher/Subscriber 复用统一的 handle 上行路径，避免在双轨上叠加第三轨
- 同车 v2.6.0 发布（次版本容纳新增 API + 行为等价重构）；若实施中发现行为
  无法完全等价，则拆分独立版本先行，不阻塞阶段 1 设计评审
- Issue #31 关联的选项 A/B/C 中，本规格实质为「A+B' 合体」：文档化（A）+
  接收分发正向收敛（B'，非原 B 反向方案）

## 5. 实施流程

```
feature/event-loop-unify 分支，TDD 红-绿-重构：

批次 1：handle 原语（红→绿）
├── 新增 test_protocol_handles.c（或并入 test_protocol_integration）：
│   reply 匹配/request 分发/pubsub/msg 四原语的单元测试（先红）
├── 实现四个 handle 原语（绿）
└── verify_exports.sh REQUIRED 列表补新符号

批次 2：client/server 瘦身切换（行为等价重构）
├── handle_response/handle_publish/ssn_server_input 切换到 handle 原语
├── 全量回归：16 套件 + ASAN（重点：hst UAF Test 10、maxconn Test 11、
│   cliauto Test 5 空闲保活、稳定性套件 T6 服务端重启重连）
└── 删除被收编的重复分发代码

批次 3：poll 单步化 + 文档同步 + 发版
├── ssn_protocol_poll/run 重定义（test_protocol / test_protocol_integration 适配验证）
├── 第 3.5 节文档全量同步 + CHANGELOG
└── 与 DDS 阶段 1 合并规划 v2.6.0 发版（四处版本号 + tag）
```

每批次独立提交（`[TDD]` 标识），批次间全量验证门禁（run_tests + verify_exports +
verify_examples）。

## 6. 验证方式

| 层面 | 方法 |
|---|---|
| 新原语单元测试 | 批次 1 新增用例（红→绿） |
| 行为等价回归 | 16 套件全绿（时序敏感用例为硬安全网：Test 10/11/Test 5/T6） |
| 内存安全 | ASAN 全量跑批（事发地为历史 UAF 高危区，零容忍） |
| 导出完整性 | verify_exports.sh（新增 handle 符号入 REQUIRED） |
| 示例冒烟 | verify_examples.sh（19 构建 + hello_world 往返） |
| 文档一致性 | 第 3.5 节清单逐项核对 |

## 7. 明确不做的事（YAGNI）

- ❌ 不采用「client/server 复用协议层循环」的反向收敛（1.3 节已否决）
- ❌ 不改线协议格式、不动传输层、不改锁模型与回调线程契约
- ❌ 不删除 `ssn_protocol_poll/run` 公开符号（SemVer minor 内禁止 breaking）
- ❌ 不合并 client 层与协议层两个 pending 池的实现（仅立混用禁令）
- ❌ 本规格不含具体实现代码（实现由 writing-plans 产出的任务计划承载）

## 8. 本次交付范围（本规格实施）

1. 本 spec 文档落库（docs/superpowers/specs/）
2. Issue #31 添加评论关联本规格，状态保持开放（待实施完成后关闭）
3. DDS 阶段 1 启动前评审本规格（作为地基前置的设计先行交付物）

后续实施不在本范围（按第 5 节流程另行启动 `feature/event-loop-unify`）。
