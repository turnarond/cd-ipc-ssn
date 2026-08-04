# DDS 对标演进路线设计（Spec）

## 文档信息

| 项目 | 内容 |
|------|------|
| 文档版本 | v1.0 |
| 创建日期 | 2026-08-04 |
| 状态 | 已批准 |
| 关联分支 | `feature/dds-doc`（文档交付）、后续 `feature/dds-stage-{1,2,3}`（实现） |

## 1. 背景与目标

### 1.1 背景

SSN（cd-ipc-ssn）是轻量 IPC 框架（v2.3.0），已有：主题字符串发布/订阅（`ssn_pubsub`）、QoS 基础框架设计、节点发现（组播/目录服务）设计。DDS（OMG Data Distribution Service）是数据分发领域事实标准，其 DCPS（Data-Centric Publish-Subscribe）模型与 SSN 的 pubsub 能力存在概念同源性。

### 1.2 目标

- 以**学习 DDS** 为根本目的，参考 DDS 进行**逐步设计、渐进靠近**：白皮书确立愿景方向，设计文档给出三阶段演进路线
- 采用**概念借鉴型**定位：采用 DDS 概念模型与关键机制，保持 SSN 轻量定位，不追求 OMG 规范全量实现与 RTPS 线协议互操作

## 2. 产出物

### 2.1 白皮书新增章节

- 位置：插入「10. 愿景 vs 已实现」之后，原「11. 实施规划与历史归档」「12. 总结」顺延为 12、13
- 标题：「11. DDS 对标与演进路线」
- 内容（愿景层）：
  1. 为什么对标 DDS：学习 DCPS 概念模型，将 SSN 逐步演进为「数据为中心的分布式通信框架」
  2. 目标形态：DCPS 模型映射总览（Domain/Topic/Publisher/Subscriber）
  3. 三阶段路线图总览（每阶段一句话 + 目标版本：2.4.0 / 2.5.0 / 2.6.0）
  4. 定位声明：概念借鉴，不追求 OMG 规范全量实现

### 2.2 新建设计文档 `docs/03-设计/DDS演进设计.md`

章节结构（与第 3-6 节对应）：
1. 设计目标与定位（概念借鉴型）
2. SSN vs DDS 概念对照表（DomainParticipant/Topic/Publisher/Subscriber/DataWriter/DataReader/QoS/发现/Listener/WaitSet/Partitions/ContentFilteredTopic）
3. 三阶段演进路线总览
4. 阶段 1：DCPS 概念模型（详细）
5. 阶段 2：QoS 策略语义（详细）
6. 阶段 3：发现与高级特性（详细）
7. 每阶段学习要点
8. 明确不做的事（YAGNI）
9. 实施规划（每阶段独立 feature 分支 + TDD）

## 3. 阶段 1：DCPS 概念模型（目标版本 2.4.0）

### 3.1 目标

引入 DDS 核心概念抽象，建立「数据为中心」心智模型。注：现有 QoS/发现设计文档标注的落地差距，属于阶段 1 **实现**时的补全范围（本规格第 9 节的文档交付不涉及代码）。

### 3.2 新增 API 草案（概念层，底层复用现有实现，纯新增不破坏现有 API）

```c
/* 域参与者（DomainParticipant）——通信域入口，概念层根对象 */
ssn_domain_t *ssn_domain_create(const char *domain_name);        // 域标识（对齐 DDS DomainId 概念）
void ssn_domain_destroy(ssn_domain_t *domain);

/* 主题（Topic）——名称 + 类型名（概念占位） */
ssn_topic_t *ssn_topic_create(ssn_domain_t *domain, const char *name,
                              const char *type_name);            // type_name 阶段 1 不实现类型系统
void ssn_topic_destroy(ssn_topic_t *topic);

/* 发布者/数据写入器（Publisher/DataWriter） */
ssn_publisher_t *ssn_publisher_create(ssn_domain_t *domain);
int ssn_publisher_write(ssn_publisher_t *pub, ssn_topic_t *topic,
                        const void *data, size_t len, const ssn_qos_t *qos);

/* 订阅者/数据读取器（Subscriber/DataReader）——回调式（Listener 风格） */
ssn_subscriber_t *ssn_subscriber_create(ssn_domain_t *domain,
                                        ssn_dds_msg_handler_t cb, void *arg);
int ssn_subscriber_subscribe(ssn_subscriber_t *sub, ssn_topic_t *topic);
```

### 3.3 与现有代码的关系

- `ssn_domain_t` 聚合/管理底层 `ssn_node` 实例
- Topic 名称映射现有 URL 路径（`/topic` 语义）
- Publisher/Subscriber 复用 `ssn_node_publish` / `ssn_node_subscribe` 能力
- **现有 `ssn_node`/`ssn_pubsub` API 零改动**

### 3.4 验证

- 新增 `test/test_dds_concept.c`（域创建/销毁、主题注册、发布订阅往返、多域隔离）
- 新增 demo：`examples/dds/01_domain_topic`
- 既有 7 套件 + `test/verify_examples.sh` 全部通过（回归）

### 3.5 学习要点

- Global Data Space 心智（所有参与者共享一个逻辑数据空间）
- Topic 作为解耦点（发布者与订阅者仅通过 Topic 名称耦合）
- 发布者-订阅者匿名性（无需知道对方地址，由框架发现与路由）

## 4. 阶段 2：QoS 策略语义（目标版本 2.5.0）

### 4.1 目标

引进 DDS QoS 策略**语义**（非全量规范），使数据分发行为可配置。

### 4.2 策略映射表

| DDS 策略 | SSN 落地方式 |
|---|---|
| RELIABILITY（BEST_EFFORT/RELIABLE） | 发送语义映射：RELIABLE → 启用重试/确认（复用 EAGAIN 处理与超时机制）；BEST_EFFORT → 当前尽力发送 |
| HISTORY（KEEP_LAST/KEEP_ALL） | 接收端历史缓存：KEEP_LAST(N) → 环形缓冲最近 N 条（与现有 pending 池机制对齐） |
| DEADLINE | 订阅超时检测：约定周期内未收到数据 → 触发监听回调（复用现有 timeout 机制） |
| LIFESPAN | 消息 TTL：帧头带时间戳，超期丢弃（ssn_header 扩展） |
| PARTITION | URL 前缀命名空间：`/partition/topic` 语义（纯映射，无新协议） |

### 4.3 API

`ssn_qos_t` 结构（`ssn_qos_reliability_t`、`ssn_qos_history_t`、`deadline_ms`、`lifespan_ms`、`partition` 字段），随 `ssn_publisher_write` / `ssn_subscriber_subscribe` 传入。

### 4.4 验证

- 新增 `test/test_dds_qos.c`：可靠 vs 尽力场景、历史缓存条数、DEADLINE 触发、TTL 丢弃
- 新增 demo：`examples/dds/02_qos`

### 4.5 学习要点

- QoS 是「发布者-订阅者契约」而非单端配置（两侧策略交集生效）
- 可靠性与实时性的权衡（RELIABLE 的延迟代价）

## 5. 阶段 3：发现与高级特性（目标版本 2.6.0）

### 5.1 目标

对齐 DDS 发现机制精髓 + 高级特性。

### 5.2 机制映射表

| DDS 机制 | SSN 落地方式 |
|---|---|
| SPDP（Simple Participant Discovery Protocol） | 现有组播发现增强为「域内参与者公告」：域名 + 节点能力广播 |
| SEDP（Simple Endpoint Discovery Protocol） | 端点级发现：主题/发布者/订阅者信息交换，订阅方自动发现匹配的发布方 |
| ContentFilteredTopic | 订阅端过滤表达式（简单通配/前缀匹配，不实现完整 filter 语言） |
| Listener / WaitSet | 现有回调扩展为事件化（on_data_available / on_publication_matched 等）+ 简单 WaitSet |

### 5.3 验证

- 新增 `test/test_dds_discovery.c`：动态加入/离开的自动发现
- 新增 demo：`examples/dds/03_discovery`
- 过滤场景测试

### 5.4 学习要点

- 发现协议的分层（参与者级 → 端点级）
- 数据为中心范式的「自动连接」价值（新增节点零配置接入）

## 6. 验证方式（贯穿三阶段）

| 层面 | 方法 |
|---|---|
| 单元测试 | 每阶段新增 `test/test_dds_*.c`（沿用 ASSERT 框架，随 CMakeLists 注册） |
| 概念 demo | `examples/dds/` 新增示例（01_domain_topic、02_qos、03_discovery） |
| 回归 | 既有 7 套件 + `test/verify_examples.sh` 全部通过（概念层纯新增，不破坏现有 API） |
| 文档同步 | 设计文档与代码同步更新（命名/API 与头文件一致） |

## 7. 实施流程（每阶段独立）

1. 每阶段一个 `feature/dds-stage-<n>` 分支，TDD：先写 `test_dds_*.c`（红）→ 实现概念层 → 全量验证（绿）
2. 阶段间评审（子代理驱动 + 审查流程）
3. 发版：每阶段完成 bump 次版本（2.4.0 → 2.5.0 → 2.6.0），更新 VERSION/CHANGELOG/白皮书

## 8. 明确不做的事（YAGNI）

- ❌ 不实现 RTPS 线协议（不追求与 FastDDS/OpenDDS 互操作）
- ❌ 不实现全量 20+ QoS 策略（只选 RELIABILITY/HISTORY/DEADLINE/LIFESPAN/PARTITION 五种子集）
- ❌ 不实现 IDL 类型系统与 TypeSupport（Topic 的 type_name 仅概念占位）
- ❌ 不改造现有 `ssn_node`/`ssn_pubsub` API（概念层纯新增）
- ❌ 本规格不含具体实现代码（实现由各阶段计划的 writing-plans 产出）

## 9. 本次交付范围（本规格实施）

1. 白皮书新增第 11 章「DDS 对标与演进路线」（含章节重编号：原 11/12 → 12/13）
2. 新建 `docs/03-设计/DDS演进设计.md`（详细设计全文，与第 3-7 节内容对应）
3. 经 writing-plans → 子代理驱动实施 → 审查 → 合并推送
