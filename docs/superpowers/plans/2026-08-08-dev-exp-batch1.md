# 开发者体验优化批次 1 实施计划（命名 + 教培文档）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完成批次 1：SSN 命名定义（Scalable Socket Network）+ 五篇教培文档（模式教程/快速上手/部署场景/FAQ/术语表）+ 总索引更新。

**Architecture:** 纯文档工作，无代码改动。命名定义统一措辞（3 处）；五篇教培文档按规格第 3 节结构撰写（以现有代码/文档为依据，准确映射 SSN 能力）；总索引纳入新文档。完成后链接与一致性验证。

**Tech Stack:** Markdown、git。验证：grep 结构/链接检查、WSL 回归（代码零改动）。

## Global Constraints

- 文档语言中文（API 名、协议名等专有名词除外）
- 命名定义措辞统一：**SSN（Scalable Socket Network，可扩展套接字网络）**——逐字使用
- 教培文档内容以现有代码与文档为依据（不得虚构 API 或行为；涉及示例引用实际示例目录）
- **代码（含注释）零改动**，仅文档
- 修改/新建 .md 保持仓库既有行尾（工作区 CRLF 视角；勿引入行尾变更噪音）
- 当前分支：`feature/dev-exp`
- 提交信息格式：`docs: ...`

---

### Task 1: SSN 命名定义（3 处）

**Files:**
- Modify: `README.md`（首段）、`docs/01-白皮书/架构白皮书.md`（1.1 项目背景）、`docs/02-需求分析/需求分析.md`（1.1 框架定位）

**Interfaces:**
- Consumes: 规格第 2 节
- Produces: 命名定义统一（后续教培文档引用同一措辞）

- [ ] **Step 1: 确认三处现状**

读 `README.md` 首段（「# ssn (cd-ipc-ssn)」下方）、`架构白皮书.md` 1.1（约 20 行）、`需求分析.md` 1.1（约 22 行），确认当前措辞。

- [ ] **Step 2: 三处统一修改**

将三处首句改为统一措辞（保留原句其余部分）：

```markdown
SSN（Scalable Socket Network，可扩展套接字网络）是一个轻量级 IPC/分布式通信框架……
```

（README 首段英文版同步：`SSN (Scalable Socket Network) is a lightweight inter-process communication (IPC) framework...`）

- [ ] **Step 3: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -c "Scalable Socket Network" README.md docs/01-白皮书/架构白皮书.md docs/02-需求分析/需求分析.md   # 预期 ≥3
```

- [ ] **Step 4: 提交**

```bash
cd /mnt/d/personal/cd-ipc-ssn
git add README.md docs/01-白皮书/架构白皮书.md docs/02-需求分析/需求分析.md
git commit -m "docs: 定义 SSN 命名含义（Scalable Socket Network，可扩展套接字网络）"
```

---

### Task 2: 通信模式教程（docs/06-使用手册/通信模式教程.md）

**Files:**
- Create: `docs/06-使用手册/通信模式教程.md`

**Interfaces:**
- Consumes: 现有 API（ssn_client_call/ssn_server_add_method、publish/subscribe、client_message/server_message、node API）
- Produces: 模式教学文档（读者学习「什么时候用什么模式」）

- [ ] **Step 1: 撰写文档（6 章）**

1. **通信模式总览**：四模式对照表（模式 | 典型场景 | SSN 对应 API | 示例）
2. **REQ/REP（请求应答）**：概念（同步、超时、回调）；SSN 对应：`ssn_client_call` + `ssn_server_add_method`（RPC 方法注册）；示例：calculator（参考 `examples/basic/02_rpc_call`）；常见坑（超时设置、回调中不得调用 close）
3. **PUB/SUB（发布订阅）**：概念（主题、异步、一对多）；SSN 对应：`ssn_server_publish` + `ssn_client_subscribe`；示例：news feed（参考 `examples/basic/03_pubsub`）；常见坑（订阅回调、`url_len` strlen 语义）
4. **PUSH/PULL（管道）**：概念（任务分发、负载均衡）；SSN 对应：`ssn_client_message` + `ssn_server_message`（点对点消息，无内置队列——注明差异）；示例：任务分发场景代码；常见坑（无队列语义，需应用层管理）
5. **PAIR（对等）**：概念（一对一双向）；SSN 对应：`ssn_node_send_to_peer` / `ssn_node_rpc_call`（节点对等通信）；示例：节点互联（参考 `examples/node/`）；常见坑（对等地址、事件循环驱动）
6. **模式选择指南**：决策流程（同步/异步、一对多/一对一、是否需要应答 → 推荐模式）

**要点**：每个模式含「何时用 / 怎么用（代码）/ 常见坑」三小节；代码示例基于真实 API（对照头文件）；PUSH/PULL 与 PAIR 的 SSN 映射注明差异（SSN 无内置队列/连接池）。

- [ ] **Step 2: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -c "^## " docs/06-使用手册/通信模式教程.md          # 预期 6（总览 + 四模式 + 选择指南）
grep -c "ssn_client_call\|ssn_server_add_method" docs/06-使用手册/通信模式教程.md   # ≥1（REQ/REP API 真实）
grep -c "ssn_client_subscribe" docs/06-使用手册/通信模式教程.md                       # ≥1（PUB/SUB API 真实）
grep -rn "ipc_\|cd_ipc" docs/06-使用手册/通信模式教程.md | wc -l                     # 预期 0（无旧命名）
```

- [ ] **Step 3: 提交**

```bash
cd /mnt/d/personal/cd-ipc-ssn
git add docs/06-使用手册/通信模式教程.md
git commit -m "docs: 新增通信模式教程（REQ/REP、PUB/SUB、PUSH/PULL、PAIR 四模式教学）"
```

---

### Task 3: 快速上手（docs/06-使用手册/快速上手.md）

**Files:**
- Create: `docs/06-使用手册/快速上手.md`

**Interfaces:**
- Consumes: 构建流程（CLAUDE.md/部署手册）、hello 示例（examples/basic/01_hello_world）
- Produces: 5 分钟入门教程

- [ ] **Step 1: 撰写文档（5 节）**

1. **5 分钟开始**：环境要求 → 构建（cmake + make）→ 第一个程序（server + client 最小代码，基于真实 API，含事件循环驱动说明）→ 运行验证
2. **常用场景模板**：RPC 服务（server + client 代码模板）、PubSub 推送、点对点消息——每模板 10-20 行可复制代码
3. **下一步指引**：链接（使用指南/API 指南/通信模式教程/示例目录/部署手册）
4. **常见错误速查**：5-8 条（连接失败/事件循环未驱动/url_len 错误/超时设置）——指向 FAQ
5. **示例索引**：15 个示例一句话说明 + 运行方式

- [ ] **Step 2: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -c "^## " docs/06-使用手册/快速上手.md          # 预期 5
grep -c "ssn_server_create" docs/06-使用手册/快速上手.md   # ≥1（代码模板真实）
grep -c "ssn_server_poll" docs/06-使用手册/快速上手.md      # ≥1（事件循环驱动说明）
```

- [ ] **Step 3: 提交**

```bash
cd /mnt/d/personal/cd-ipc-ssn
git add docs/06-使用手册/快速上手.md
git commit -m "docs: 新增快速上手教程（5 分钟入门 + 场景模板）"
```

---

### Task 4: 部署场景指南（docs/05-部署手册/部署场景指南.md）

**Files:**
- Create: `docs/05-部署手册/部署场景指南.md`

**Interfaces:**
- Consumes: 部署手册（环境/构建）、传输能力、节点发现、idle/keepalive 行为
- Produces: 场景化部署指南

- [ ] **Step 1: 撰写文档（4 章）**

1. **单机多进程（Unix Socket）**：场景说明（同机多服务）、部署要点（unix:// 路径、权限、socket 文件清理）、示例拓扑
2. **跨节点（TCP + 发现）**：场景说明（多机服务）、部署要点（tcp:// 地址、节点发现配置、防火墙/端口）、示例拓扑
3. **混合传输场景**：同机内部用 Unix、跨机用 TCP 的混合部署；客户端连接多地址说明
4. **服务端运维场景**：事件循环驱动（server poll 线程模式）、idle/keepalive 行为说明（v2.3.2 起应用层 idle 默认 10 秒，活跃连接自动重置）、优雅退出（stop/destroy 顺序）

**要点**：内容与代码行为一致（idle 默认 10s、事件循环需驱动——参照测试架构文档与部署手册）；每章含部署检查清单。

- [ ] **Step 2: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -c "^## " docs/05-部署手册/部署场景指南.md    # 预期 4
grep -c "unix://\|tcp://" docs/05-部署手册/部署场景指南.md   # ≥1（地址示例）
grep -c "idle" docs/05-部署手册/部署场景指南.md              # ≥1（idle 行为说明）
```

- [ ] **Step 3: 提交**

```bash
cd /mnt/d/personal/cd-ipc-ssn
git add docs/05-部署手册/部署场景指南.md
git commit -m "docs: 新增部署场景指南（单机/跨节点/混合/服务端运维）"
```

---

### Task 5: FAQ 与术语表

**Files:**
- Create: `docs/06-使用手册/FAQ.md`、`docs/06-使用手册/术语表.md`

**Interfaces:**
- Consumes: 使用指南/API 指南/部署手册（问题与术语来源）
- Produces: 常见问题速查 + 术语统一

- [ ] **Step 1: 撰写 FAQ（8-12 条，按类别分组）**

1. **构建与安装**：WSL 构建、.so 找不到（rpath/LD_LIBRARY_PATH）、CMake 版本
2. **连接与通信**：连接失败（地址格式 `://` 必需）、事件循环未驱动（server 不收包）、UDP 不支持 server 握手（限制标注）
3. **消息与订阅**：收不到订阅消息（url_len strlen 语义）、回调时机（回调返回后数据失效）
4. **超时与线程**：超时不生效（poll 毫秒语义）、回调线程安全（不得在回调调 close）、多线程 poll 同一 server 不支持

每问：问题 → 原因 → 解决方案（指向相关文档/代码位置）。

- [ ] **Step 2: 撰写术语表（15-20 条）**

按字母/类别组织：IPC、RPC、PubSub、Topic（主题）、URL（`tcp://` 格式）、节点（Node）、域（Domain，DDS 概念）、QoS、传输（Unix/TCP/UDP）、事件循环、回调、DTO、帧（Frame）、握手、心跳/keepalive、idle 超时、stub/代理 等。每条：术语 → 定义（SSN 语境）→ 相关文档链接。

- [ ] **Step 3: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -c "^## " docs/06-使用手册/FAQ.md        # ≥3（类别分组）
grep -c "url_len" docs/06-使用手册/FAQ.md      # ≥1（订阅问题覆盖）
grep -c "事件循环" docs/06-使用手册/FAQ.md      # ≥1（常见坑覆盖）
grep -c "^| \|^-" docs/06-使用手册/术语表.md    # ≥15（术语条目）
```

- [ ] **Step 4: 提交**

```bash
cd /mnt/d/personal/cd-ipc-ssn
git add docs/06-使用手册/FAQ.md docs/06-使用手册/术语表.md
git commit -m "docs: 新增 FAQ 与术语表"
```

---

### Task 6: 总索引更新 + 全量验证

**Files:**
- Modify: `docs/README.md`（总索引）

**Interfaces:**
- Consumes: Task 1-5 全部交付物
- Produces: 索引完整（读者入口）

- [ ] **Step 1: 更新 docs/README.md 总索引**

- 05-部署手册 节点：追加 `部署场景指南.md` 链接
- 06-使用手册 节点：追加 `通信模式教程.md`、`快速上手.md`、`FAQ.md`、`术语表.md` 链接
- 阅读顺序建议：更新（快速上手 → 模式教程 → 使用指南 → 部署）

- [ ] **Step 2: 全量验证（四项）**

```bash
cd /mnt/d/personal/cd-ipc-ssn
# 1. 链接完整性：docs/README.md 中全部 .md 链接目标存在
grep -oE '\]\([^)#][^)]*\.md\)' docs/README.md | sed -E 's/.*\]\((.*)\)/\1/' | while read -r l; do [ -f "docs/$l" ] || echo "DEAD: $l"; done
# 2. 命名一致性：新文档无旧命名
grep -rn "ipc_\|cd_ipc" docs/06-使用手册/通信模式教程.md docs/06-使用手册/快速上手.md docs/06-使用手册/FAQ.md docs/06-使用手册/术语表.md docs/05-部署手册/部署场景指南.md | wc -l   # 预期 0
# 3. 命名定义一致性：三处 + 新文档引用
grep -rc "Scalable Socket Network" README.md docs/01-白皮书/架构白皮书.md docs/02-需求分析/需求分析.md   # 预期 ≥1 每处
# 4. 代码回归（确认文档零改动影响）
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh 2>&1 | tail -2"   # 7 套件全过
```

- [ ] **Step 3: 提交**

```bash
cd /mnt/d/personal/cd-ipc-ssn
git add docs/README.md
git commit -m "docs: 总索引更新（纳入教培五文档），批次 1 全量验证通过"
```

---

## Self-Review（实施前确认清单）

- [ ] 规格第 2 节（命名定义 3 处）↔ Task 1
- [ ] 规格第 3 节（五文档 + 索引）↔ Task 2-6（模式教程/快速上手/部署场景/FAQ+术语表/索引）
- [ ] 规格第 7 节批次 1（独立分支 + TDD + 验证）↔ 全局约束与各任务验证步骤
- [ ] 文档内容准确性（API/行为不得虚构）↔ 各任务「以现有代码为依据」要点
- [ ] 批次 2（C++ 框架）不在本计划范围（另行规划）
