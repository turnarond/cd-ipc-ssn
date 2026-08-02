# docs 文档重构实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 docs/ 下 18 个文档按「整理归档为主」策略重组为 01-07 + 09 编号体系，修正过时状态与命名残留，重写核心使用文档，产出完整、一致、可导航的文档体系。

**Architecture:** 纯文档操作，无代码改动。四阶段执行：① 目录重组（git mv + 内容合并）→ ② 归档与状态标注 → ③ 命名改写 → ④ 重写与补全（白皮书/使用指南/总索引）。每阶段独立提交，全部完成后执行四项验证（链接完整性、命名一致性、编号完整性、代码回归）。

**Tech Stack:** Markdown、git（git mv / git rm）、sed（命名改写）、bash（验证脚本）、WSL（代码回归测试）。

## Global Constraints

- 交互、文档、文件夹名称一律使用中文（API 名、协议名等专有名词除外）
- 目录编号：01-白皮书、02-需求分析、03-设计、04-实施规划、05-部署手册、06-使用手册、07-测试方案、09-归档（08 预留）
- 命名改写 `ipc_` → `ssn_`，**豁免**：① VSI 内部组件（`ipc_mutex`、`ipc_thread`、`ipc_socket`、`ipc_event`、`ipc_platform`、`ipc_memory_barrier` 等）；② 迁移指南.md 的映射表；③ 09-归档 下文档正文
- 归档文档正文**不得修改**，仅头部加标注块
- **代码（含注释）零改动**，仅文档
- 当前分支：`feature/docs-refactor`（已 rebase 到最新 main，main 已含 fix 分支）
- 新写/修改的 .md 文件保持仓库现有行尾风格（CRLF）；不新增 .sh 文件
- 每次提交信息遵循仓库现有风格（`docs: ...` 前缀）

---

### Task 1: 传输层设计吸收合并协议适配器设计

**Files:**
- Modify: `docs/架构设计/传输层设计.md`
- Read-only: `docs/架构设计/协议适配器设计.md`（合并源，合并后删除）

**Interfaces:**
- Consumes: 无（首个任务）
- Produces: 合并后的 `传输层设计.md`（后续 Task 2 将移动并删除协议适配器设计.md）

- [ ] **Step 1: 通读两份文档，确定合并方案**

读 `docs/架构设计/协议适配器设计.md`（992 行）与 `docs/架构设计/传输层设计.md`（342 行），两者在「传输层接口、配置结构、地址解析、错误码」逐字段重复。

- [ ] **Step 2: 以传输层设计.md 为主体，吸收协议适配器设计的独特内容**

在 `传输层设计.md` 中追加/整合以下**独特章节**（其余重复部分不复制）：
1. **连接池设计**：`ssn_connection_pool_t` 的连接池/健康检查设计（协议适配器设计.md 独有内容）——标注「未实现」
2. **错误码章节**：统一为 `SSN_ECODE_*` 宏（修正传输层设计.md 中过时的 `SSN_ERR_*` 枚举）
3. **代码风格规范章节**（若协议适配器设计.md 含有，搬运原文）

删除协议适配器设计.md 中与传输层设计.md 重复的接口/配置/地址/错误码段落（不搬运）。

- [ ] **Step 3: 验证合并结果**

运行：
```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -c "ssn_connection_pool_t" docs/架构设计/传输层设计.md   # 预期 ≥1（连接池章节存在）
grep -c "SSN_ECODE" docs/架构设计/传输层设计.md               # 预期 ≥1（错误码已统一）
wc -l docs/架构设计/传输层设计.md                              # 预期在 400~700 行之间
```
预期：三检查均通过。

- [ ] **Step 4: 提交**

```bash
cd /mnt/d/personal/cd-ipc-ssn
git add docs/架构设计/传输层设计.md
git commit -m "docs: 传输层设计合并协议适配器设计（连接池/错误码/风格章节）"
```

---

### Task 2: 目录重组为 01-09 编号体系

**Files:**
- Modify: 17 个文档路径移动（git mv）+ 删除 `docs/架构设计/协议适配器设计.md`

**Interfaces:**
- Consumes: Task 1 合并完成的 `传输层设计.md`
- Produces: 与规格第 2 节一致的目录结构（后续所有任务在新路径上操作）

- [ ] **Step 1: 创建目标目录**

```bash
cd /mnt/d/personal/cd-ipc-ssn/docs
mkdir -p 01-白皮书 02-需求分析 03-设计/架构设计 03-设计/核心模块 \
         04-实施规划 05-部署手册 06-使用手册 07-测试方案 09-归档
```

- [ ] **Step 2: 逐个 git mv（17 篇）**

```bash
cd /mnt/d/personal/cd-ipc-ssn
git mv docs/架构设计/架构概览.md             docs/01-白皮书/架构白皮书.md
git mv docs/架构设计/架构设计总览.md         docs/03-设计/架构设计/
git mv docs/架构设计/传输层设计.md           docs/03-设计/架构设计/
git mv docs/架构设计/外层架构重构方案.md     docs/09-归档/
git mv docs/核心模块/协议层模块化设计.md     docs/03-设计/核心模块/
git mv docs/核心模块/QoS框架设计.md          docs/03-设计/核心模块/
git mv docs/核心模块/节点发现设计.md         docs/03-设计/核心模块/
git mv docs/核心模块/节点抽象设计.md         docs/03-设计/核心模块/
git mv docs/核心模块/会话优先协议设计.md     docs/09-归档/
git mv docs/实施规划/2.0版本实施规划.md      docs/09-归档/
git mv docs/实施规划/迁移指南.md             docs/04-实施规划/
git mv docs/使用指南/API使用指南.md          docs/06-使用手册/
git mv docs/使用指南/使用指南.md             docs/06-使用手册/
git mv docs/使用指南/README.md              docs/README.md
git mv docs/测试方案/协议层集成测试方案.md   docs/07-测试方案/
git mv THREAD_SAFETY.md                     docs/03-设计/核心模块/线程安全设计.md
```

- [ ] **Step 3: 删除已合并的协议适配器设计**

```bash
git rm docs/架构设计/协议适配器设计.md
```

- [ ] **Step 4: 验证目录结构（与规格第 2 节逐项对照）**

```bash
cd /mnt/d/personal/cd-ipc-ssn
ls docs/
ls docs/03-设计/架构设计/ docs/03-设计/核心模块/ docs/09-归档/
git status --short | grep -c "^R"   # 预期 16（17 篇移动含 1 篇改名）
```
预期：`ls docs/` 显示 01-白皮书 02-需求分析 03-设计 04-实施规划 05-部署手册 06-使用手册 07-测试方案 09-归档 README.md 和 superpowers/（superpowers 目录为规格/计划存放处，保持不动）。

- [ ] **Step 5: 提交**

```bash
cd /mnt/d/personal/cd-ipc-ssn
git add -A docs/ THREAD_SAFETY.md
git commit -m "docs: 目录重组为 01-09 编号体系（移动 17 篇，删除已合并的协议适配器设计）"
```

---

### Task 3: 归档 3 篇历史文档（头部标注）

**Files:**
- Modify: `docs/09-归档/外层架构重构方案.md`、`docs/09-归档/2.0版本实施规划.md`、`docs/09-归档/会话优先协议设计.md`

**Interfaces:**
- Consumes: Task 2 的归档目录
- Produces: 3 篇带归档标注的文档（Task 11 验证豁免项）

- [ ] **Step 1: 为 3 篇文档头部加归档标注块（正文不动）**

在每篇文档第一行标题前插入：

```markdown
> 📦 **已归档（2026-08-02）**
>
> 本文档仅作历史参考，不再维护更新。当前实现以 03-设计/ 与 06-使用手册/ 下的文档为准。
>
> - 归档原因（外层架构重构方案）：重构任务已完成，其提出的 client/、server/、cliauto/ 子目录规划未被采纳
> - 归档原因（2.0版本实施规划）：规划任务已全部完成，属历史记录
> - 归档原因（会话优先协议设计）：方案未采纳，实际地址格式为 tcp://、unix://、udp://
```

- [ ] **Step 2: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -l "已归档" docs/09-归档/*.md | wc -l   # 预期 3
```

- [ ] **Step 3: 提交**

```bash
git add docs/09-归档/
git commit -m "docs: 归档 3 篇历史文档并标注原因"
```

---

### Task 4: 有效文档信息表与状态标注（03-设计 7 篇 + 07 测试方案）

**Files:**
- Modify: `docs/03-设计/架构设计/架构设计总览.md`、`docs/03-设计/架构设计/传输层设计.md`、`docs/03-设计/核心模块/协议层模块化设计.md`、`docs/03-设计/核心模块/QoS框架设计.md`、`docs/03-设计/核心模块/节点发现设计.md`、`docs/03-设计/核心模块/节点抽象设计.md`、`docs/03-设计/核心模块/线程安全设计.md`、`docs/07-测试方案/协议层集成测试方案.md`

**Interfaces:**
- Consumes: Task 2 的新路径
- Produces: 各文档头部信息表（Task 11 验证项）

- [ ] **Step 1: 为 7 篇文档头部加统一信息表**

每篇标题下方插入：

```markdown
## 文档信息

| 项目 | 内容 |
|------|------|
| 文档版本 | v1.0 |
| 状态 | （见下表） |
| 更新日期 | 2026-08-02 |
```

状态值按文档内容设置：

| 文档 | 状态 | 补充处理 |
|---|---|---|
| 架构设计/架构设计总览.md | 已实施 | 将正文「1.2 待实现的模块」表中 client/server/cliauto/node 行的「⏳ 待实施/📋 待实现」改为「✅ 已实现（2.1~2.3.0）」，并在 1.1 完成表补齐 ssn_client/ssn_server/ssn_cliauto/ssn_node |
| 架构设计/传输层设计.md | 有效 | 无 |
| 核心模块/协议层模块化设计.md | 已实施 | 正文开头加一行「> ✅ 本设计已随 v2.1.0 落地于 src/protocol/」 |
| 核心模块/QoS框架设计.md | 部分过时 | 正文中「动态调整」「兼容层」等未落地设计段落标注「> ⚠️ 未实现」 |
| 核心模块/节点发现设计.md | 部分过时 | 「目录服务模式」段落标注「> ⚠️ 未实现（2.0 中为可选规划）」 |
| 核心模块/节点抽象设计.md | 部分过时 | 「全局 API 表」设计段落标注「> ⚠️ 未采用，实际为 ssn_node_* 函数式 API」 |
| 核心模块/线程安全设计.md | 有效 | 标题行「# IPC SSN 线程安全实现文档」改为「# 线程安全设计」 |

**额外更新 `docs/07-测试方案/协议层集成测试方案.md`（状态段与路径修正）：**
1. 状态段：正文「部分完成/待实现」的标注（序列化、路由、回调等）更新为「已全部完成 —— 2026-08-02 全量测试 116 用例通过（7 套件）」
2. 执行路径：将绝对路径 `/home/yanchaodong/...` 改为相对路径说明（构建后在 `build/` 下执行各测试可执行文件，或运行 `bash test/run_tests.sh`）
3. 头部补「文档信息」表（状态：有效，版本 v1.0，日期 2026-08-02）

- [ ] **Step 2: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -l "文档信息" docs/03-设计/*/*.md | wc -l   # 预期 7
grep -c "已实现（2.1~2.3.0）\|已实现" docs/03-设计/架构设计/架构设计总览.md  # 预期 ≥1
grep -c "116 用例通过\|全部完成" docs/07-测试方案/协议层集成测试方案.md  # 预期 ≥1（状态已更新）
grep -c "/home/yanchaodong" docs/07-测试方案/协议层集成测试方案.md      # 预期 0（绝对路径已清除）
```

- [ ] **Step 3: 提交**

```bash
git add docs/03-设计/ docs/07-测试方案/
git commit -m "docs: 有效文档补充信息表与状态标注（03-设计 7 篇、测试方案状态段更新）"
```

---

### Task 5: 4 篇设计文档 ipc_ 命名改写（约 390 处）

**Files:**
- Modify: `docs/03-设计/核心模块/QoS框架设计.md`（127 处）、`docs/03-设计/核心模块/节点发现设计.md`（117 处）、`docs/03-设计/核心模块/节点抽象设计.md`（114 处）、`docs/03-设计/架构设计/架构设计总览.md`（32 处）

**Interfaces:**
- Consumes: Task 4 的新路径与信息表
- Produces: 4 篇文档命名与代码一致（Task 11 命名检查通过）

- [ ] **Step 1: 备份并统计改写范围**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -o "ipc_[a-z_]*" docs/03-设计/核心模块/QoS框架设计.md docs/03-设计/核心模块/节点发现设计.md docs/03-设计/核心模块/节点抽象设计.md docs/03-设计/架构设计/架构设计总览.md | sort | uniq -c
```
预期：出现 `ipc_client_`、`ipc_server_`、`ipc_node_`、`ipc_qos_`、`ipc_discovery_`、`ipc_generate_node_id`、`g_ipc_node_api` 等业务符号；若出现 `ipc_mutex`、`ipc_thread`、`ipc_socket`、`ipc_event`、`ipc_platform`、`ipc_memory_barrier` 属 **VSI 豁免项，不得改写**。

- [ ] **Step 2: 批量改写业务符号**

```bash
cd /mnt/d/personal/cd-ipc-ssn
# 依次替换（顺序执行，先长后短避免部分命中）
for f in docs/03-设计/核心模块/QoS框架设计.md \
         docs/03-设计/核心模块/节点发现设计.md \
         docs/03-设计/核心模块/节点抽象设计.md \
         docs/03-设计/架构设计/架构设计总览.md; do
  sed -i 's/ipc_client_/ssn_client_/g; s/ipc_server_/ssn_server_/g; s/ipc_node_/ssn_node_/g; s/ipc_qos_/ssn_qos_/g; s/ipc_discovery_/ssn_discovery_/g; s/ipc_generate_node_id/ssn_generate_node_id/g; s/g_ipc_node_api/g_ssn_node_api/g' "$f"
done
```
**注意**：改写后检查每篇文档是否出现除 VSI 豁免外的 `ipc_` 残留；出现新模式（如 `ipc_protocol_`、`ipc_frame_`）时按同样规则追加 sed 替换。**VSI 豁免词（ipc_mutex/ipc_thread/ipc_socket/ipc_event/ipc_platform/ipc_memory_barrier）必须保留**。

- [ ] **Step 3: 修复 sed 引起的行尾变化（重要）**

仓库 .md 文件为 CRLF 行尾，sed 会改写为 LF。执行：

```bash
cd /mnt/d/personal/cd-ipc-ssn
perl -pi -e 's/\r?\n/\r\n/g' docs/03-设计/核心模块/QoS框架设计.md \
  docs/03-设计/核心模块/节点发现设计.md \
  docs/03-设计/核心模块/节点抽象设计.md \
  docs/03-设计/架构设计/架构设计总览.md
```

- [ ] **Step 4: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
git diff --stat docs/03-设计/   # 每篇变化行数应与 grep 统计的 ipc_ 出现处一致量级（非整文件）
grep -rn "ipc_" docs/03-设计/ | grep -v "ipc_mutex\|ipc_thread\|ipc_socket\|ipc_event\|ipc_platform\|ipc_memory_barrier" | wc -l   # 预期 0
```

- [ ] **Step 5: 提交**

```bash
cd /mnt/d/personal/cd-ipc-ssn
git add docs/03-设计/
git commit -m "docs: 设计文档业务符号 ipc_ 统一为 ssn_ 前缀"
```

---

### Task 6: 使用指南.md 全面重写（按当前 API）

**Files:**
- Rewrite: `docs/06-使用手册/使用指南.md`（821 行 → 重写）
- Modify: `docs/06-使用手册/API使用指南.md`（校对修正）
- Read-only: `src/ssn_client.h`、`src/ssn_server.h`、`src/node/ssn_node.h`、`src/ssn_cliauto.h`、`CHANGELOG.md`

**Interfaces:**
- Consumes: Task 2 的新路径
- Produces: 与当前代码一致的完整用户教程（Task 11 命名检查覆盖）

- [ ] **Step 1: 核对头文件获取权威 API 签名，并校对 API使用指南.md**

对照 `src/ssn_client.h`、`src/ssn_server.h`、`src/node/ssn_node.h` 记录全部公开函数签名（作为重写依据）。要点（v2.1+ 现状）：

**同时校对 `docs/06-使用手册/API使用指南.md`**：将其全部函数签名、类型名、`SSN_ECODE_*` 错误码与头文件逐项核对，修正任何不一致（若有），并在其头部补 Task 4 同款「文档信息」表（状态：有效，版本 v1.0，日期 2026-08-02）。
- `ssn_server_create(const char* server_info)` 单参数
- `ssn_server_add_method(server, url, callback, arg)`
- `ssn_client_message(client, url, data)` 两参数
- `ssn_client_subscribe(client, url, callback, arg, timeout_ms)` 返回 bool
- `ssn_node_subscribe(node, peer_address, url, callback, arg, timeout_ms)` 含 peer_address
- 错误码使用 `SSN_ECODE_*` 宏（`src/ssn_error.h`）
- `ssn_url_ref_t` 仅含 `url`/`url_len` 字段

- [ ] **Step 2: 重写正文**

保持章节骨架，替换过时内容：
1. 「快速开始」：签名更新
2. 「核心概念」：保留并校对（地址格式 `tcp://`、`unix://`、`udp://`）
3. 「五组示例」（hello/rpc/pubsub/node/错误处理）：**全部按当前 API 重写**，删除不存在的类型（`ssn_server_client_t`、`ssn_server_response_t` 等），示例可参考 `examples/` 下同名示例
4. 「错误码」：改为 `SSN_ECODE_*` 表（`SSN_ECODE_SUCCESS`、`SSN_ECODE_NET_CONNECT`、`SSN_ECODE_TIMEOUT`、`SSN_ECODE_INVALID_ARGS`、`SSN_ECODE_NOT_FOUND` 等，来自 `src/ssn_error.h`）
5. 「FAQ」「性能」：保留并校对（最大数据包 128KB = `SSN_MAX_PACKET_SIZE`）
6. 「版本历史」：补 2.1.0（命名迁移）、2.2.0（cliauto/测试）、2.3.0（架构升级），内容取自 `CHANGELOG.md`
7. 头部加 Task 4 同款「文档信息」表（状态：有效，版本 v1.0，日期 2026-08-02）

- [ ] **Step 3: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -c "ssn_server_create" docs/06-使用手册/使用指南.md          # ≥1
grep -c "SSN_ECODE_" docs/06-使用手册/使用指南.md                 # ≥1
grep -c "ssn_server_client_t\|ssn_server_response_t" docs/06-使用手册/使用指南.md  # 预期 0（旧类型清除）
```

- [ ] **Step 4: 提交**

```bash
git add docs/06-使用手册/
git commit -m "docs: 使用指南按当前 API 全面重写，API使用指南与头文件核对校对"
```

---

### Task 7: 架构白皮书.md 升级（由架构概览）

**Files:**
- Rewrite: `docs/01-白皮书/架构白皮书.md`（原架构概览.md，598 行）
- Read-only: `README.md`（架构图与版本）、`CHANGELOG.md`

**Interfaces:**
- Consumes: Task 2 的新路径
- Produces: 面向决策者的白皮书（01 类目唯一文档）

- [ ] **Step 1: 升级内容**

1. 头部信息表：版本 v2.1.0 → v2.3.0，状态「有效」，日期 2026-08-02
2. 总体架构图：与 `README.md`「架构概述」图核对一致
3. 清理/改写旧枚举设计代码块：`IPC_TRANSPORT_*`、`IPC_DISCOVERY_*` → `SSN_` 前缀
4. 新增「愿景 vs 已实现」小节，明确标注：已实现（节点抽象、协议模块化、QoS 基础、节点发现、多传输）vs 愿景未实现（TLS/DTLS 安全传输、连接池、压缩）
5. 删除「四阶段实施计划」章节（属规划性质，2.0 已执行完毕），改为指向 04-实施规划/迁移指南.md 与 09-归档/

- [ ] **Step 2: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -c "2.3.0" docs/01-白皮书/架构白皮书.md          # ≥1（版本已更新）
grep -c "愿景 vs 已实现\|未实现" docs/01-白皮书/架构白皮书.md   # ≥1
grep -c "IPC_TRANSPORT_\|IPC_DISCOVERY_" docs/01-白皮书/架构白皮书.md  # 预期 0
```

- [ ] **Step 3: 提交**

```bash
git add docs/01-白皮书/
git commit -m "docs: 架构概览升级为架构白皮书（版本 v2.3、愿景标注、清理旧枚举）"
```

---

### Task 8: 迁移指南补 2.2/2.3 章节

**Files:**
- Modify: `docs/04-实施规划/迁移指南.md`（102 行）

**Interfaces:**
- Consumes: Task 2 的新路径
- Produces: 完整的迁移历史（Task 11 命名检查豁免项）

- [ ] **Step 1: 补写章节**

在迁移指南.md 现有 v2.0→v2.1 章节后追加（内容取自 `CHANGELOG.md`）：

```markdown
## v2.1 → v2.2 迁移要点（2026-05-06）

- `ipc_client_auto_state_t` 枚举改名 `ssn_client_auto_state_t`
- `ssn_cliauto` 订阅改用 `ssn_client_subscribe`（替代 `ssn_client_message`）
- `ssn_client_connect` 返回类型由 `int` 修正为 `bool`
- `VSOA` 引用统一改名为 `SSN`（如 `VSOA_CLIENT_AUTO_MAX_PING_LOST` → `SSN_CLIENT_AUTO_MAX_PING_LOST`）

## v2.2 → v2.3 迁移要点（2026-05-07）

- 无公开 API 命名变更；本版本为架构升级（CollectionScheduler、DataPipeline、DeviceStateMachine、DiagnosticsCollector），涉及 driver-sdk 侧
```

- [ ] **Step 2: 验证**

```bash
grep -c "v2.1 → v2.2\|v2.2 → v2.3" docs/04-实施规划/迁移指南.md   # 预期 2
```

- [ ] **Step 3: 提交**

```bash
git add docs/04-实施规划/迁移指南.md
git commit -m "docs: 迁移指南补充 2.2/2.3 版本章节"
```

---

### Task 9: 文档总索引 docs/README.md + 02/05 占位

**Files:**
- Rewrite: `docs/README.md`（原使用指南/README.md，255 行 → 总索引）
- Create: `docs/02-需求分析/README.md`、`docs/05-部署手册/README.md`

**Interfaces:**
- Consumes: Task 2-8 完成的全部文档路径
- Produces: 读者入口（docs/README.md），Task 11 链接检查对象

- [ ] **Step 1: 重写 docs/README.md 为总索引**

```markdown
# SSN 文档中心

> 面向 ssn（cd-ipc-ssn）IPC 框架的完整文档体系。按阅读顺序编号，从「白皮书」开始，依次阅读「设计」→「实施规划」→「使用手册」→「测试方案」；历史文档见「归档」。

## 阅读顺序建议

01 白皮书 → 03 设计 → 04 实施规划 → 06 使用手册 → 07 测试方案

## 01-白皮书

- [架构白皮书](01-白皮书/架构白皮书.md) —— 愿景、价值、适用场景、总体架构

## 02-需求分析

- 待补写（见 `02-需求分析/README.md`）

## 03-设计

### 架构设计

- [架构设计总览](03-设计/架构设计/架构设计总览.md) —— 分层架构与模块依赖
- [传输层设计](03-设计/架构设计/传输层设计.md) —— 传输层接口、工厂、连接池

### 核心模块

- [协议层模块化设计](03-设计/核心模块/协议层模块化设计.md)
- [QoS框架设计](03-设计/核心模块/QoS框架设计.md)
- [节点发现设计](03-设计/核心模块/节点发现设计.md)
- [节点抽象设计](03-设计/核心模块/节点抽象设计.md)
- [线程安全设计](03-设计/核心模块/线程安全设计.md)

## 04-实施规划

- [迁移指南](04-实施规划/迁移指南.md) —— v2.0→v2.3 命名与 API 迁移

## 05-部署手册

- 待补写（见 `05-部署手册/README.md`）

## 06-使用手册

- [API使用指南](06-使用手册/API使用指南.md) —— 权威 API 参考
- [使用指南](06-使用手册/使用指南.md) —— 快速上手与完整教程

## 07-测试方案

- [协议层集成测试方案](07-测试方案/协议层集成测试方案.md)

## 09-归档

- [外层架构重构方案](09-归档/外层架构重构方案.md)、[2.0版本实施规划](09-归档/2.0版本实施规划.md)、[会话优先协议设计](09-归档/会话优先协议设计.md) —— 已归档历史文档，仅作参考

## 开发规范

- [CLAUDE.md](../CLAUDE.md) —— 编码规范、TDD 流程、分支与版本迭代规范
```

- [ ] **Step 2: 创建 02/05 占位 README**

`docs/02-需求分析/README.md`：

```markdown
# 需求分析（待补写）

本目录用于存放需求分析文档（如 SSN 框架需求规格、使用场景与功能清单）。

> ⚠️ 尚未补写。规划中，后续版本迭代时补充。
```

`docs/05-部署手册/README.md`：

```markdown
# 部署手册（待补写）

本目录用于存放部署手册（如构建产物说明、环境要求、集成部署步骤）。

> ⚠️ 尚未补写。规划中，后续版本迭代时补充。
```

- [ ] **Step 3: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
ls docs/02-需求分析/ docs/05-部署手册/          # 各含 README.md
grep -c "01-白皮书\|03-设计\|09-归档" docs/README.md   # ≥3（索引覆盖编号类目）
```

- [ ] **Step 4: 提交**

```bash
git add docs/README.md docs/02-需求分析/ docs/05-部署手册/
git commit -m "docs: 新增文档总索引与 02/05 占位目录"
```

---

### Task 10: 根 README 链接更新 + CODE_STYLE 并入 CLAUDE.md

**Files:**
- Modify: `README.md`（「文档链接」章节）、`CLAUDE.md`（「命名与代码规范」章节扩充）
- Delete: `CODE_STYLE.md`（内容并入 CLAUDE.md 后 git rm）

**Interfaces:**
- Consumes: Task 2-9 的最终文档路径
- Produces: 根入口链接一致；CODE_STYLE 内容合并后的 CLAUDE.md

- [ ] **Step 1: 更新 README.md「文档链接」章节**

将「文档链接」下的 8 条链接替换为（新路径）：

```markdown
- [架构白皮书](docs/01-白皮书/架构白皮书.md)
- [文档中心索引](docs/README.md)
- [架构设计总览](docs/03-设计/架构设计/架构设计总览.md)
- [传输层设计](docs/03-设计/架构设计/传输层设计.md)
- [协议层模块化设计](docs/03-设计/核心模块/协议层模块化设计.md)
- [API 使用指南](docs/06-使用手册/API使用指南.md)
- [使用指南](docs/06-使用手册/使用指南.md)
- [迁移指南](docs/04-实施规划/迁移指南.md)
- [测试方案](docs/07-测试方案/协议层集成测试方案.md)
```

- [ ] **Step 2: 将 CODE_STYLE.md 内容并入 CLAUDE.md**

读取 `CODE_STYLE.md` 全文，将「命名与代码规范」章节扩充为包含：

1. 现有命名规范（snake_case、`ssn_`/`ipc_`(VSI) 前缀、`_t` 后缀、`SSN_UPPER_CASE` 宏）
2. CODE_STYLE.md 的要点（以精简条目并入，不整篇复制）：
   - 缩进：4 空格，不用制表符；`{` 行尾、`}` 新行；控制语句必须用大括号
   - 注释：文件头注释、函数注释（功能/参数/返回值）、`//` 行内注释、`/* */` 块注释
   - 头文件：包含顺序（系统→第三方→自定义）、`#ifndef` 保护
   - 错误处理：`LOG_ERROR`/`LOG_WARNING` 记录、错误码用 `ssn_error.h` 的 `SSN_ECODE_*`
   - 长度限制：函数 ≤ 200 行、行 ≤ 120 字符
   - 工具：clang-format 格式化、cppcheck 静态分析

- [ ] **Step 3: 删除 CODE_STYLE.md**

```bash
cd /mnt/d/personal/cd-ipc-ssn
git rm CODE_STYLE.md
```

- [ ] **Step 4: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -c "120 字符\|clang-format\|cppcheck" CLAUDE.md    # ≥1（风格要点已并入）
grep -c "docs/01-白皮书\|docs/README.md" README.md      # ≥1（链接已更新）
ls CODE_STYLE.md 2>&1 | grep -c "No such"               # 预期 1（文件已删除）
```

- [ ] **Step 5: 提交**

```bash
git add README.md CLAUDE.md
git commit -m "docs: 根 README 链接更新；CODE_STYLE 内容并入 CLAUDE.md 后删除"
```

---

### Task 11: 全量验证（规格第 5 节四项检查）

**Files:**
- 无文件修改（仅验证）

**Interfaces:**
- Consumes: Task 1-10 全部交付物
- Produces: 重构完成证据

- [ ] **Step 1: 链接完整性检查**

检查两个索引文档（`docs/README.md`、根 `README.md`）中所有相对链接的目标文件存在：

```bash
cd /mnt/d/personal/cd-ipc-ssn
# 检查 docs/README.md 内的相对链接（形如 01-白皮书/xxx.md）
grep -oE '\]\([^)#][^)]*\.md\)' docs/README.md | sed -E 's/.*\]\((.*)\)/\1/' \
  | while read -r link; do
      [ -f "docs/$link" ] || echo "DEAD LINK in docs/README.md: $link"
    done
# 检查根 README.md 内的 docs 链接（形如 docs/xx/yy.md）
grep -oE '\]\(docs/[^)#][^)]*\.md\)' README.md | sed -E 's/.*\]\((.*)\)/\1/' \
  | while read -r link; do
      [ -f "$link" ] || echo "DEAD LINK in README.md: $link"
    done
# 抽查其余文档互链（每个 docs 内 .md 的相对链接，排除 superpowers/）
find docs -name "*.md" -not -path "docs/superpowers/*" -exec grep -oE '\]\(\.\.?/[^)#][^)]*\.md\)' {} + \
  | sed -E 's/^([^:]+):.*\]\((.*)\)/\1|\2/' \
  | while IFS='|' read -r file link; do
      target="$(dirname "$file")/$(echo "$link" | sed 's|\.\./||g')"
      [ -f "$target" ] || echo "DEAD LINK in $file: $link"
    done
```
预期：无 DEAD LINK 输出（三组检查均静默通过）。

- [ ] **Step 2: 命名一致性检查**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -rn "ipc_" docs/ --include="*.md" \
  --exclude-dir=superpowers --exclude-dir=09-归档 \
  | grep -v "docs/04-实施规划/迁移指南.md" \
  | grep -v "ipc_mutex\|ipc_thread\|ipc_socket\|ipc_event\|ipc_platform\|ipc_memory_barrier" | wc -l
```
预期：0。

- [ ] **Step 3: 编号完整性检查**

```bash
cd /mnt/d/personal/cd-ipc-ssn
ls -d docs/0*/ | sed 's|docs/||;s|/$||'   # 预期输出 01-白皮书 02-需求分析 03-设计 04-实施规划 05-部署手册 06-使用手册 07-测试方案 09-归档
```

- [ ] **Step 4: 代码回归（WSL）**

```bash
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh"
```
预期：7 套件全部通过（代码零改动，仅验证文档操作未影响代码）。

- [ ] **Step 5: 汇总验证结果并提交验证报告**

将四项检查结果输出保存至 `docs/superpowers/plans/2026-08-02-docs-refactor-verification.md`（若全部通过），并：

```bash
cd /mnt/d/personal/cd-ipc-ssn
git add docs/superpowers/plans/
git commit -m "docs: 文档重构全量验证通过（链接/命名/编号/代码回归）"
```
若任一检查失败：修复后重跑对应检查，直至全部通过再提交。

---

## Self-Review（实施前确认清单）

- [ ] 规格第 2 节目录结构 ↔ Task 2 移动清单逐项对应
- [ ] 规格第 3 节映射表 18 行 ↔ Task 1-10 覆盖（#1 总索引→Task 9、#2 白皮书→Task 7、#3-#16→Task 2/3/4/6/8、#17 线程安全→Task 2/4、#18 CODE_STYLE→Task 10）
- [ ] 规格第 4 节改写规则（命名豁免、归档不动、信息表）↔ Task 3/4/5 约束
- [ ] 规格第 5 节验证方式 ↔ Task 11 四项检查
- [ ] 规格第 7 节「明确不做的事」（不补 02/05 正文、不删归档、不改代码）↔ 计划内无违反操作
