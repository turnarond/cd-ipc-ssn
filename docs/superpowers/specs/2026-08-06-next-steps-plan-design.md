# 下一步规划（2.3.1 稳定化 + 2.4.0 DDS 阶段 1）设计（Spec）

## 文档信息

| 项目 | 内容 |
|------|------|
| 文档版本 | v1.0 |
| 创建日期 | 2026-08-06 |
| 状态 | 已批准 |
| 关联分支 | 多分支（见执行步骤） |

## 1. 背景

SSN（v2.3.0）已完成：docs 体系重构（01-09 编号）、命名统一（ipc_→ssn_）、examples 迁移、DDS 演进路线文档（PR #1 待合并）。当前遗留 8 项 issue（handoff 记录），DDS 三阶段路线图已批准。

## 2. 总体路线（两轮）

```
本轮 2.3.1（稳定化）
├─ 步骤 0：合并 PR #1（dds-doc → main）
├─ 步骤 1：代码修复 6 项（正确性 + 清理）
├─ 步骤 2：文档补写 2 项（02/05 + 坏链）
└─ 步骤 3：发版 2.3.1（VERSION/CHANGELOG/tag）

下轮 2.4.0（DDS 阶段 1）
└─ feature/dds-stage-1（按 DDS演进设计.md 第 4 章实施）
```

**执行原则**：
- 每项修复独立 `fix/<简述>` 分支（用户规范），TDD（回归测试先行）
- 2.3.1 发版：修订 +1（2.3.0 → 2.3.1），同步 VERSION/ssn_version.h/CMakeLists/CHANGELOG + `git tag v2.3.1`
- 2.4.0 阶段 1 严格按已批准规格 `docs/03-设计/DDS演进设计.md` 第 4 章实施

## 3. 8 项 issue 修复设计

| # | Issue | 修复方案 | 验证 |
|---|---|---|---|
| ① | UDP 服务端握手限制（`udp_transport_accept` 不支持） | **评估 + 标注**：确认框架限制本质，在 `src/transports/ssn_transport_udp.c` 头注释与传输层设计文档标注「UDP 仅支持对等/客户端模式，不支持 server 握手」；`examples/protocols/03_udp` README 同步说明 | 构建通过 + 文档一致 |
| ② | `ssn_node_destroy` 自锁（ssn_node.c:301-314 持锁调 stop） | 修复锁序：destroy 路径在调用 `ssn_node_stop` 前释放 `node->lock`（按实际代码结构最小改动） | 新增回归用例：ACTIVE 节点直接 destroy 不挂死 |
| ③ | error_handling/timeout 示例 Test 2 断言不成立 | 重写测试逻辑：基于回调结果（`ipc_hdr == NULL`）判断超时失败而非 send 返回值 | 示例运行断言通过 |
| ④ | IPv6 snprintf 截断警告（ssn_transport.c:169/205） | 检查 `snprintf` 返回值并处理截断（或按需增大缓冲） | `-Wformat-truncation` 消失 + 构建通过 |
| ⑤ | `ssn_ecode_version` 无原型死符号（ssn_error.c:27） | **删除**该函数（全库零引用） | 构建通过（无符号变更） |
| ⑥ | `typedef struct ipc_node` 内部标签残留（ssn_node.h:113） | 改名 `struct ssn_node`（含 .c 定义处） | 构建通过 |
| ⑦ | 02 需求分析 / 05 部署手册补写 | 完整规格：需求分析 4 章（框架定位/功能需求/非功能需求/DDS 对标映射）、部署手册 4 章（环境要求/构建/安装集成/常见问题） | 文档结构 + 链接验证 |
| ⑧ | superpowers 计划/规格文档 25 个坏链 | 修正 `docs/superpowers/plans/2026-08-02-docs-refactor.md` 等文档的相对链接 | 链接检查 0 死链 |

**分组执行**：
- 批次 A（正确性，独立分支）：②③④
- 批次 B（清理，独立分支）：①⑤⑥⑧
- 批次 C（文档）：⑦
- 每批次修复后全量验证（run_tests + verify_examples），最后合并发版

## 4. 2.4.0 DDS 阶段 1（下轮主线，规划边界）

| 项 | 内容 |
|---|---|
| 分支 | `feature/dds-stage-1` |
| 交付 | `ssn_domain_t`/`ssn_topic_t`/`ssn_publisher_t`/`ssn_subscriber_t` 概念层（复用现有 transport/protocol，纯新增） |
| 测试 | `test/test_dds_concept.c`（域/主题/发布订阅/多域隔离）+ demo `examples/dds/01_domain_topic` |
| 过程 | TDD + 子代理驱动 + 全量审查（沿用既有流程） |
| 发版 | 2.4.0（次版本 +1） |

## 5. 发版细节（2.3.1 与 2.4.0 共用）

1. 同步更新 4 处：`VERSION`、`src/version/ssn_version.h`、`CMakeLists.txt`、`CHANGELOG.md`（按 Added/Changed/Fixed 分组）
2. 白皮书「版本历史」表同步
3. 发版提交后打 tag：`git tag v2.3.1` / `git tag v2.4.0`
4. 全量验证门禁：7 套件 + 15 示例 + 文档链接检查全过

## 6. 本次交付范围（本规格实施）

1. 步骤 0：合并 PR #1（dds-doc → main）
2. 步骤 1-2：8 项修复（3 批次）+ 02/05 补写
3. 步骤 3：发版 2.3.1（含 tag）
4. 2.4.0 阶段 1 不在本规格实施范围（下轮另行启动）
