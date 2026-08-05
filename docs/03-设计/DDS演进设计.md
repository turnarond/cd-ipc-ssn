# DDS 演进设计

## 文档信息

| 项目 | 内容 |
|------|------|
| 文档版本 | v1.0 |
| 状态 | 有效（路线图设计，实现按阶段推进） |
| 更新日期 | 2026-08-05 |
| 关联规格 | `docs/superpowers/specs/2026-08-04-dds-roadmap-design.md` |

## 1. 设计目标与定位

### 1.1 设计目标

以**学习 DDS** 为根本目的：参考 DDS 进行**逐步设计、渐进靠近**。白皮书第 11 章《DDS 对标与演进路线》确立愿景方向，本文档给出三阶段演进路线的详细设计——阶段 1 引入 DCPS 概念模型、阶段 2 落地 QoS 策略语义、阶段 3 增强发现机制与高级特性，将 SSN 从「以连接为中心的 IPC 框架」逐步演进为「以数据为中心的分布式通信框架」。

### 1.2 定位：概念借鉴型

SSN 采用**概念借鉴型**定位：采用 DDS 的概念模型与关键机制，保持 SSN 轻量 IPC 定位。**不追求** OMG 规范全量实现、RTPS 线协议互操作或与真实 DDS 实现（FastDDS/OpenDDS 等）兼容（详见第 8 章「明确不做的事」）。

### 1.3 与既有设计的关系

本设计建立在 SSN 既有能力（主题字符串发布/订阅 `ssn_pubsub`、QoS 基础框架设计、节点发现设计）之上：概念层 API 为纯新增，底层复用现有实现，现有 `ssn_node`/`ssn_pubsub` API 零改动（各阶段的「与现有代码的关系」详见第 4-6 章）。

## 2. SSN vs DDS 概念对照表

下表将 DDS（OMG DCPS 规范）核心概念逐一映射到 SSN 的演进目标，并标注对应演进阶段；「状态」为「不做」的条目即第 8 章 YAGNI 的明确范围。

| DDS 概念 | 定义 | SSN 演进目标 | 状态 |
|----------|------|-------------|------|
| DomainParticipant | 参与通信域的应用实体，域内隔离 | `ssn_domain_t` | 阶段 1 |
| Topic | 数据主题：名称 + 类型 + 键 | `ssn_topic_t` | 阶段 1 |
| Publisher | 数据发布端实体 | `ssn_publisher_t` | 阶段 1 |
| DataWriter | Publisher 下的写入器（类型化） | `ssn_publisher_write` 概念 | 阶段 1（合并） |
| Subscriber | 数据接收端实体 | `ssn_subscriber_t` | 阶段 1 |
| DataReader | Subscriber 下的读取器（类型化） | `ssn_subscriber_subscribe` 概念 | 阶段 1（合并） |
| QoS 策略 | 发布/订阅契约（20+ 种） | `ssn_qos_t` 五策略子集 | 阶段 2 |
| 发现机制 | SPDP（参与者级）+ SEDP（端点级） | 增强组播/目录发现 | 阶段 3 |
| Listener / WaitSet | 事件回调与等待机制 | 事件化回调 + 简单 WaitSet | 阶段 3 |
| Partitions | 域内逻辑分区 | URL 前缀命名空间 | 阶段 2 |
| ContentFilteredTopic | 订阅端内容过滤 | 简单通配/前缀过滤 | 阶段 3 |
| 类型系统（IDL/TypeSupport） | 结构化数据类型定义 | 不实现（type_name 占位） | 不做 |

## 3. 三阶段演进路线总览

| 阶段 | 目标版本 | 主题 | 一句话概述 |
|------|---------|------|-----------|
| 阶段 1 | 2.4.0 | DCPS 概念模型 | 引入 Domain/Topic/Publisher/Subscriber 抽象层，建立「数据为中心」心智模型 |
| 阶段 2 | 2.5.0 | QoS 策略语义 | 引进 RELIABILITY/HISTORY/DEADLINE/LIFESPAN/PARTITION 五种核心策略语义 |
| 阶段 3 | 2.6.0 | 发现与高级特性 | 增强为 SPDP/SEDP 风格发现，引入 ContentFilteredTopic 与 Listener/WaitSet |

每阶段独立设计详见第 4-6 章；学习要点汇总见第 7 章；明确不做的事与实施规划见第 8-9 章。验证方式贯穿三阶段：每阶段新增 `test/test_dds_*.c` 单元测试与 `examples/dds/` 概念 demo，既有 7 套件 + `test/verify_examples.sh` 全量回归（概念层纯新增，不破坏现有 API）。

## 4. 阶段 1：DCPS 概念模型（目标版本 2.4.0）

### 4.1 目标

引入 DDS 核心概念抽象（Domain/Topic/Publisher/Subscriber），建立「数据为中心」心智模型。

> 注：现有 QoS/发现设计文档标注的落地差距，属于阶段 1 **实现**时的补全范围（本文档交付不涉及代码）。

### 4.2 新增 API 草案（概念层，底层复用现有实现，纯新增不破坏现有 API）

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

### 4.3 与现有代码的关系

- `ssn_domain_t` 聚合/管理底层 `ssn_node` 实例
- Topic 名称映射现有 URL 路径（`/topic` 语义）
- Publisher/Subscriber 复用 `ssn_node_publish` / `ssn_node_subscribe` 能力
- **现有 `ssn_node`/`ssn_pubsub` API 零改动**

### 4.4 验证

- 新增 `test/test_dds_concept.c`（域创建/销毁、主题注册、发布订阅往返、多域隔离）
- 新增 demo：`examples/dds/01_domain_topic`
- 既有 7 套件 + `test/verify_examples.sh` 全部通过（回归）

### 4.5 学习要点

- Global Data Space 心智（所有参与者共享一个逻辑数据空间）
- Topic 作为解耦点（发布者与订阅者仅通过 Topic 名称耦合）
- 发布者-订阅者匿名性（无需知道对方地址，由框架发现与路由）

## 5. 阶段 2：QoS 策略语义（目标版本 2.5.0）

### 5.1 目标

引进 DDS QoS 策略**语义**（非全量规范），使数据分发行为可配置。

### 5.2 策略映射表

| DDS 策略 | SSN 落地方式 |
|---|---|
| RELIABILITY（BEST_EFFORT/RELIABLE） | 发送语义映射：RELIABLE → 启用重试/确认（复用 EAGAIN 处理与超时机制）；BEST_EFFORT → 当前尽力发送 |
| HISTORY（KEEP_LAST/KEEP_ALL） | 接收端历史缓存：KEEP_LAST(N) → 环形缓冲最近 N 条（与现有 pending 池机制对齐） |
| DEADLINE | 订阅超时检测：约定周期内未收到数据 → 触发监听回调（复用现有 timeout 机制） |
| LIFESPAN | 消息 TTL：帧头带时间戳，超期丢弃（ssn_header 扩展） |
| PARTITION | URL 前缀命名空间：`/partition/topic` 语义（纯映射，无新协议） |

### 5.3 API

`ssn_qos_t` 结构（`ssn_qos_reliability_t`、`ssn_qos_history_t`、`deadline_ms`、`lifespan_ms`、`partition` 字段），随 `ssn_publisher_write` / `ssn_subscriber_subscribe` 传入。

### 5.4 验证

- 新增 `test/test_dds_qos.c`：可靠 vs 尽力场景、历史缓存条数、DEADLINE 触发、TTL 丢弃
- 新增 demo：`examples/dds/02_qos`

### 5.5 学习要点

- QoS 是「发布者-订阅者契约」而非单端配置（两侧策略交集生效）
- 可靠性与实时性的权衡（RELIABLE 的延迟代价）

## 6. 阶段 3：发现与高级特性（目标版本 2.6.0）

### 6.1 目标

对齐 DDS 发现机制精髓（SPDP/SEDP 分层发现），并引入高级特性（ContentFilteredTopic、Listener/WaitSet）。

### 6.2 机制映射表

| DDS 机制 | SSN 落地方式 |
|---|---|
| SPDP（Simple Participant Discovery Protocol） | 现有组播发现增强为「域内参与者公告」：域名 + 节点能力广播 |
| SEDP（Simple Endpoint Discovery Protocol） | 端点级发现：主题/发布者/订阅者信息交换，订阅方自动发现匹配的发布方 |
| ContentFilteredTopic | 订阅端过滤表达式（简单通配/前缀匹配，不实现完整 filter 语言） |
| Listener / WaitSet | 现有回调扩展为事件化（on_data_available / on_publication_matched 等）+ 简单 WaitSet |

### 6.3 验证

- 新增 `test/test_dds_discovery.c`：动态加入/离开的自动发现
- 新增 demo：`examples/dds/03_discovery`
- 过滤场景测试

### 6.4 学习要点

- 发现协议的分层（参与者级 → 端点级）
- 数据为中心范式的「自动连接」价值（新增节点零配置接入）

## 7. 每阶段学习要点

汇总第 4-6 章各阶段的「学习要点」，作为对照 DDS 规范概念的理解要点清单。

### 7.1 阶段 1：DCPS 概念模型

1. Global Data Space 心智：所有参与者共享一个逻辑数据空间
2. Topic 作为解耦点：发布者与订阅者仅通过 Topic 名称耦合
3. 发布者-订阅者匿名性：无需知道对方地址，由框架发现与路由

### 7.2 阶段 2：QoS 策略语义

1. QoS 是「发布者-订阅者契约」而非单端配置（两侧策略交集生效）
2. 可靠性与实时性的权衡（RELIABLE 的延迟代价）
3. 时间语义进入契约：DEADLINE 以约定周期触发超时监听回调，LIFESPAN 以帧头时间戳驱动 TTL 丢弃

### 7.3 阶段 3：发现与高级特性

1. 发现协议的分层（参与者级 SPDP → 端点级 SEDP）
2. 数据为中心范式的「自动连接」价值（新增节点零配置接入）
3. 端点级信息交换使匹配自动化：订阅方自动发现匹配的发布方（SEDP 映射）
4. 订阅端过滤减少无关数据传输（ContentFilteredTopic 映射，简单通配/前缀匹配）

## 8. 明确不做的事（YAGNI）

- ❌ 不实现 RTPS 线协议（不追求与 FastDDS/OpenDDS 互操作）
- ❌ 不实现全量 20+ QoS 策略（只选 RELIABILITY/HISTORY/DEADLINE/LIFESPAN/PARTITION 五种子集）
- ❌ 不实现 IDL 类型系统与 TypeSupport（Topic 的 type_name 仅概念占位）
- ❌ 不改造现有 `ssn_node`/`ssn_pubsub` API（概念层纯新增）
- ❌ 本规格不含具体实现代码（实现由各阶段计划的 writing-plans 产出）

## 9. 实施规划

### 9.1 实施流程（每阶段独立）

1. 每阶段一个 `feature/dds-stage-<n>` 分支，TDD：先写 `test_dds_*.c`（红）→ 实现概念层 → 全量验证（绿）
2. 阶段间评审（子代理驱动 + 审查流程）
3. 发版：每阶段完成 bump 次版本（2.4.0 → 2.5.0 → 2.6.0），更新 VERSION/CHANGELOG/白皮书
