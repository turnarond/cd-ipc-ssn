# DDS 演进路线文档交付实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按已批准规格交付两份文档：白皮书新增「DDS 对标与演进路线」章节（含重编号），新建 `docs/03-设计/DDS演进设计.md` 详细设计。

**Architecture:** 纯文档操作，无代码改动。白皮书章节为愿景层（为何对标、目标形态、三阶段总览、定位声明）；设计文档为详细层（概念对照表、三阶段详设、学习要点、YAGNI、实施规划），两者内容对应规格第 3-7 节。完成后更新总索引并验证。

**Tech Stack:** Markdown、git。验证：grep 章节编号/链接检查、WSL 回归（代码零改动）。

## Global Constraints

- 文档语言中文（API 名、协议名等专有名词除外）
- 内容以规格 `docs/superpowers/specs/2026-08-04-dds-roadmap-design.md` 为唯一需求来源（确切名称、版本号、策略映射逐字使用）
- **代码（含注释）零改动**，仅文档
- 修改/新建 .md 保持仓库 CRLF 行尾（若用脚本处理需恢复：`perl -pi -e 's/\r?\n/\r\n/g' <file>`）
- 当前分支：`feature/dds-doc`（规格已提交于 cbfed67）
- 本次交付不含任何 DDS 实现代码（阶段 1-3 实现另行规划）

---

### Task 1: 白皮书新增「11. DDS 对标与演进路线」章节并重编号

**Files:**
- Modify: `docs/01-白皮书/架构白皮书.md`

**Interfaces:**
- Consumes: 规格第 2.1 节（章节位置与内容要求）
- Produces: 白皮书第 10-13 章编号连续（后续 Task 2 的设计文档不依赖白皮书，但总索引更新依赖章节名）

- [ ] **Step 1: 读白皮书第 10-12 章**

读 `docs/01-白皮书/架构白皮书.md` 的「10. 愿景 vs 已实现」「11. 实施规划与历史归档」「12. 总结」三个章节（约 606-639 行），确认各章标题与内容衔接。

- [ ] **Step 2: 插入新章节「11. DDS 对标与演进路线」**

在「## 10. 愿景 vs 已实现（决策摘要）」章节末尾之后、「## 11. 实施规划与历史归档」之前插入：

```markdown
## 11. DDS 对标与演进路线

### 11.1 为什么对标 DDS

DDS（Data Distribution Service，OMG 标准）是数据分发领域的行业标准，其核心模型 DCPS（Data-Centric Publish-Subscribe，以数据为中心的发布/订阅）与 SSN 的发布/订阅能力存在概念同源性。对标 DDS 的根本目的是**学习 DDS**：以 DDS 的概念模型为参照，逐步设计、渐进靠近，将 SSN 从「以连接为中心的 IPC 框架」演进为「以数据为中心的分布式通信框架」。

### 11.2 目标形态（DCPS 模型映射总览）

| DDS 概念 | SSN 演进目标 |
|----------|-------------|
| DomainParticipant（域参与者） | `ssn_domain_t`（通信域入口） |
| Topic（类型化主题） | `ssn_topic_t`（名称 + 类型名占位） |
| Publisher / DataWriter | `ssn_publisher_t`（数据写入） |
| Subscriber / DataReader | `ssn_subscriber_t`（回调式读取） |
| QoS 策略（RELIABILITY 等） | `ssn_qos_t`（轻量策略子集） |
| 发现机制（SPDP/SEDP） | 增强现有组播/目录发现 |

### 11.3 三阶段演进路线总览

| 阶段 | 目标版本 | 主题 | 一句话概述 |
|------|---------|------|-----------|
| 阶段 1 | 2.4.0 | DCPS 概念模型 | 引入 Domain/Topic/Publisher/Subscriber 抽象层，建立「数据为中心」心智模型 |
| 阶段 2 | 2.5.0 | QoS 策略语义 | 引进 RELIABILITY/HISTORY/DEADLINE/LIFESPAN/PARTITION 五种核心策略语义 |
| 阶段 3 | 2.6.0 | 发现与高级特性 | 增强为 SPDP/SEDP 风格发现，引入 ContentFilteredTopic 与 Listener/WaitSet |

### 11.4 定位声明

SSN 采用**概念借鉴型**定位：借鉴 DDS 的概念模型与关键机制，保持轻量 IPC 定位。**不追求** OMG 规范全量实现、RTPS 线协议互操作或与真实 DDS 实现（FastDDS/OpenDDS 等）兼容。详细设计见 `docs/03-设计/DDS演进设计.md`。
```

- [ ] **Step 3: 重编号原 11/12 章为 12/13**

- 原「## 11. 实施规划与历史归档」→「## 12. 实施规划与历史归档」
- 原「## 12. 总结」→「## 13. 总结」
- 检查章节内是否有交叉引用旧编号的文本（grep 后修正）

- [ ] **Step 4: 恢复 CRLF 行尾（若使用脚本处理过）**

```bash
cd /mnt/d/personal/cd-ipc-ssn
perl -pi -e 's/\r?\n/\r\n/g' docs/01-白皮书/架构白皮书.md
```

- [ ] **Step 5: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -n "^## " docs/01-白皮书/架构白皮书.md | tail -6   # 预期 10 愿景 11 DDS 12 实施规划 13 总结
grep -c "## 11. DDS 对标与演进路线" docs/01-白皮书/架构白皮书.md   # 预期 1
grep -c "## 11. 实施规划" docs/01-白皮书/架构白皮书.md            # 预期 0（已重编号）
grep -c "DDS演进设计" docs/01-白皮书/架构白皮书.md                # 预期 ≥1（11.4 指向设计文档）
```

- [ ] **Step 6: 提交**

```bash
cd /mnt/d/personal/cd-ipc-ssn
git add docs/01-白皮书/架构白皮书.md
git commit -m "docs: 白皮书新增 DDS 对标与演进路线章节（含 11/12 章重编号）"
```

---

### Task 2: 新建 docs/03-设计/DDS演进设计.md

**Files:**
- Create: `docs/03-设计/DDS演进设计.md`
- Modify: `docs/README.md`（总索引 03-设计/核心模块 节新增链接）

**Interfaces:**
- Consumes: 规格第 2.2、3-7 节（全部设计内容）
- Produces: 设计文档（白皮书 11.4 指向它；docs/README.md 总索引链接它）

- [ ] **Step 1: 创建设计文档头部与文档信息表**

```markdown
# DDS 演进设计

## 文档信息

| 项目 | 内容 |
|------|------|
| 文档版本 | v1.0 |
| 状态 | 有效（路线图设计，实现按阶段推进） |
| 更新日期 | 2026-08-05 |
| 关联规格 | `docs/superpowers/specs/2026-08-04-dds-roadmap-design.md` |
```

- [ ] **Step 2: 撰写第 1-3 章（目标定位、概念对照表、路线总览）**

1. **设计目标与定位**：概念借鉴型（从规格 1.2 展开）——采用 DDS 概念模型与关键机制，保持 SSN 轻量定位；学习 DDS 为根本目的
2. **SSN vs DDS 概念对照表**（每行：DDS 概念 | 定义一句话 | SSN 演进目标 | 状态）：

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

3. **三阶段演进路线总览**：与规格 2.1 白皮书 11.3 的表一致（阶段 1/2/3、目标版本 2.4.0/2.5.0/2.6.0、主题、一句话概述）

- [ ] **Step 3: 撰写第 4-6 章（三阶段详设，内容对应规格 3-5 节）**

4. **阶段 1：DCPS 概念模型（目标版本 2.4.0）**
   - 目标（含注：现有 QoS/发现设计落地差距属于阶段 1 实现时补全范围）
   - API 草案代码块（从规格 3.2 逐字复制）
   - 与现有代码的关系（规格 3.3：domain 聚合 node、Topic→URL 路径映射、复用 node_publish/subscribe、现有 API 零改动）
   - 验证（规格 3.4：test_dds_concept.c、examples/dds/01_domain_topic、回归）
   - 学习要点（规格 3.5：Global Data Space、Topic 解耦、发布者-订阅者匿名性）

5. **阶段 2：QoS 策略语义（目标版本 2.5.0）**
   - 目标
   - 策略映射表（从规格 4.2 逐字复制：RELIABILITY/HISTORY/DEADLINE/LIFESPAN/PARTITION 五行）
   - API（规格 4.3：ssn_qos_t 结构字段）
   - 验证（规格 4.4：test_dds_qos.c、examples/dds/02_qos）
   - 学习要点（规格 4.5：QoS 是契约非单端配置、可靠性与实时性权衡）

6. **阶段 3：发现与高级特性（目标版本 2.6.0）**
   - 目标
   - 机制映射表（从规格 5.2 逐字复制：SPDP/SEDP/ContentFilteredTopic/Listener+WaitSet 四行）
   - 验证（规格 5.3：test_dds_discovery.c、examples/dds/03_discovery、过滤场景）
   - 学习要点（规格 5.4：发现协议分层、数据为中心自动连接价值）

- [ ] **Step 4: 撰写第 7-9 章（学习要点汇总、YAGNI、实施规划）**

7. **每阶段学习要点**：汇总三阶段的「学习要点」小节（对照 DDS 规范概念的理解要点，每阶段 3-5 条）
8. **明确不做的事（YAGNI）**：从规格第 8 节逐字复制 5 条 ❌
9. **实施规划**：从规格第 7 节逐字复制（每阶段独立 feature/dds-stage-<n> 分支 + TDD、阶段间评审、发版 bump 2.4.0→2.5.0→2.6.0）

- [ ] **Step 5: 更新 docs/README.md 总索引**

在 `docs/README.md` 的「03-设计 / 核心模块」列表末尾追加：

```markdown
- [DDS演进设计](03-设计/DDS演进设计.md) —— 参考 DDS 的三阶段演进路线（DCPS 概念模型、QoS 策略语义、发现与高级特性）
```

- [ ] **Step 6: 恢复 CRLF 行尾（若使用脚本处理过）**

```bash
cd /mnt/d/personal/cd-ipc-ssn
perl -pi -e 's/\r?\n/\r\n/g' docs/03-设计/DDS演进设计.md docs/README.md
```

- [ ] **Step 7: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -c "^## " docs/03-设计/DDS演进设计.md          # 预期 9（第 1-9 章）
grep -c "ssn_domain_t" docs/03-设计/DDS演进设计.md   # ≥1（阶段 1 API 草案）
grep -c "RELIABILITY" docs/03-设计/DDS演进设计.md     # ≥1（阶段 2 策略映射）
grep -c "SPDP" docs/03-设计/DDS演进设计.md            # ≥1（阶段 3 发现）
grep -c "不实现 RTPS" docs/03-设计/DDS演进设计.md     # ≥1（YAGNI）
grep -c "DDS演进设计" docs/README.md                  # ≥1（总索引已更新）
```

- [ ] **Step 8: 提交**

```bash
cd /mnt/d/personal/cd-ipc-ssn
git add docs/03-设计/DDS演进设计.md docs/README.md
git commit -m "docs: 新增 DDS 演进设计（三阶段路线图）并更新总索引"
```

---

### Task 3: 全量验证与收尾

**Files:**
- 无文件修改（仅验证 + 提交验证报告）

**Interfaces:**
- Consumes: Task 1-2 全部交付物
- Produces: 交付完成证据

- [ ] **Step 1: 文档一致性检查**

```bash
cd /mnt/d/personal/cd-ipc-ssn
# 白皮书章节编号连续（10-13）
grep -n "^## " docs/01-白皮书/架构白皮书.md | tail -4
# 设计文档与规格章节对应（规格 2.2 的 9 章结构）
grep -c "^## " docs/03-设计/DDS演进设计.md   # 9
# 总索引链接目标存在
[ -f docs/03-设计/DDS演进设计.md ] && echo "OK: 索引目标存在"
# 白皮书 11.4 指向的设计文档路径存在
[ -f docs/03-设计/DDS演进设计.md ] && echo "OK: 白皮书指向存在"
```

- [ ] **Step 2: 命名与残留检查**

```bash
cd /mnt/d/personal/cd-ipc-ssn
# 新文档不得引入旧命名（对照表内的历史说明除外）
grep -n "ipc_\|cd_ipc" docs/03-设计/DDS演进设计.md | wc -l   # 预期 0
```

- [ ] **Step 3: 代码回归（确认文档操作未影响代码）**

```bash
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh 2>&1 | tail -2"
```
预期：7 套件全部通过。

- [ ] **Step 4: 汇总验证结果并提交**

将三项检查输出保存至 `.superpowers/sdd/dds-doc/verification.md`（目录不存在则创建），并：

```bash
cd /mnt/d/personal/cd-ipc-ssn
git status --short   # 应干净（Task 1-2 已提交）
```

若任一检查失败：修复对应文件后重新验证，直至全部通过。

---

## Self-Review（实施前确认清单）

- [ ] 规格 2.1（白皮书章节位置/内容/重编号）↔ Task 1 步骤 2-3
- [ ] 规格 2.2（设计文档 9 章结构）↔ Task 2 步骤 2-4
- [ ] 规格 3-5 节（三阶段详设）↔ Task 2 步骤 3 的内容来源
- [ ] 规格 7（实施流程）/ 8（YAGNI）↔ Task 2 步骤 4
- [ ] 规格 6（验证方式）↔ Task 3 的检查项
- [ ] 规格 9（本次交付范围仅文档）↔ 计划无代码改动
