# 开发者体验优化设计（Spec）

## 文档信息

| 项目 | 内容 |
|------|------|
| 文档版本 | v1.0 |
| 创建日期 | 2026-08-08 |
| 状态 | 已批准 |
| 关联分支 | 见实施规划（4 批） |

## 1. 背景与目标

SSN（v2.3.2）文档体系完整、测试全绿，但开发者使用体验待优化：命名无含义、教培不完整、无高层语言封装。目标：**面向开发者的使用体验**——命名品牌化、教培完整化、提供有意义的 C++ 服务开发框架（对标 vsoa lwserverbase + nanomsg 生态）、docs 站点化。

## 2. 命名定义（零破坏）

- **SSN = Scalable Socket Network（可扩展套接字网络）**
- 落点：README 首段、白皮书 1.1、需求分析 1.1——统一措辞「SSN（Scalable Socket Network，可扩展套接字网络）是一个轻量级 IPC/分布式通信框架……」
- 明确定位：完整 IPC 栈（框架而非某层协议）

## 3. 教培内容规划（四方向，覆盖用户场景）

| 新增文档 | 位置 | 内容 |
|---|---|---|
| 模式化概念教程 | `06-使用手册/通信模式教程.md` | 四模式（REQ/REP、PUB/SUB、PUSH/PULL、PAIR）：何时用/怎么用/示例/常见坑（nanomsg 式教学） |
| 快速上手 | `06-使用手册/快速上手.md` | 5 分钟从零到第一个程序（构建→hello→收发→下一步），含场景模板 |
| 部署场景指南 | `05-部署手册/部署场景指南.md` | 单机多进程（Unix）、跨节点（TCP+发现）、混合传输、服务端场景（含 idle/keepalive 说明） |
| FAQ | `06-使用手册/FAQ.md` | 安装/构建/连接失败/超时/线程安全/事件循环驱动等常见问题 |
| 术语表 | `06-使用手册/术语表.md` | IPC/模式/协议/QoS/域/主题等术语统一 |
| 总索引更新 | `docs/README.md` | 新文档入索引 |

## 4. C++ 服务框架（批次 2，对标 vsoa lwserverbase）

### 4.1 定位与体量

有意义的微服务开发框架（非 RAII 包装），总量约 5,000-8,000 行 C++（含测试）。实现策略：**移植 vsoa lwserverbase core/ 骨架 + 自研 SSN 通信层**。

### 4.2 架构分层

```
include/ssn/framework/
├── ServiceBase.hpp        # 生命周期基类：OnInit/OnShutdown 钩子（OnStart/OnStop final 锁定）
├── ServiceTask.hpp        # 线程池基类（activate(N)/svc()/requestShutdown()）
├── ServiceManager.hpp     # 服务编排：Run<T>() 一行启动（Initialize→Start→Signals→Stop→Cleanup）
├── SsnService.hpp         # 通信服务基类（服务注册 + RPC 分发 + 内置端点）— 自研
├── SsnClient.hpp          # 客户端（按服务名管理连接、自动重连/心跳、RAII 响应）— 自研
└── util/（移植精简：config/logging 复用 ssn_log）
```

### 4.3 核心设计

| 设计 | 说明 |
|---|---|
| 三层渐进式基类 | `ServiceBase → ServiceTask → SsnService`，`ServiceManager::Run<MyService>()` 一行启动 |
| 服务定义 | `RegisterMethod<Req,Resp>("/url", handler)`——URL 即服务接口，回调类型安全 |
| 内置端点 | `/urls`（服务注册表）、`/health`、`/version` 自动注册 |
| 客户端管理 | `SsnClient`：连接上下文按目标管理、自动重连/心跳参数、RAII 响应 |
| DTO/JSON | **nlohmann/json**（header-only，MIT）：方法签名直接用 `json` 或用户结构体（`NLOHMANN_DEFINE_TYPE_INTRUSIVE`）；免自研反射 |
| 编码抽象 | `Encoder` 可插拔（JSON 默认 / binary 预留）——可选，非首批 |

### 4.4 移植范围（需确认 vsoa_framework 许可证）

- **移植**：lwserverbase `core/`（ServiceBase/ServiceTask/ServiceManager ~1,500 行）——纯框架件，不依赖 vsoa 传输
- **自研**：SsnService/SsnClient（绑定 SSN C API）——vsoa SDK 绑定 lwcomm 不可复用
- **不做**：vsoa_master 注册中心（SSN 用现有节点发现）、metrics 全套、codegen 工具
- **C++ 标准**：C++17

### 4.5 工期评估（单开发者 + TDD + 逐任务审查）

| 子步 | 内容 | 工期 |
|---|---|---|
| 2a | 移植 core/ 骨架（1-2 天）+ SsnService/SsnClient 自研（3-4 天）+ 内置端点（1-2 天）+ TDD 测试（2-3 天） | 约 1.5-2 周 |
| 2b | nlohmann/json 集成 + 类型安全 RegisterMethod/Call（2-3 天含测试） | 约 0.5 周 |
| 2c | examples/cpp/（echo、pubsub）+ test_ssn_cpp + CMake 目标 + 文档（2-3 天） | 约 0.5 周 |
| **合计** | | **约 2.5-3.5 周** |

## 5. 教培网站（批次 3）

| 项 | 方案 |
|---|---|
| 工具 | mkdocs + mkdocs-material（搜索/导航/版本切换） |
| 配置 | 仓库根 `mkdocs.yml`，从 `docs/` 构建（01-09 章节导航 + 教培新文档） |
| 发布 | GitHub Pages + CI（`.github/workflows/docs.yml`：mkdocs gh-deploy） |
| 版本 | mkdocs-material 版本切换（main=latest + v2.3.2 快照） |
| 约束 | 中文为主；README 顶部加站点链接 |

## 6. 其他优化（P2/P3 界定）

| 优先级 | 优化 | 说明 | 本次 |
|---|---|---|---|
| P1 | 命名 + 教培 + C++ 服务框架 | 本规格 | ✅ |
| P2 | 包管理集成（vcpkg/conan manifest） | 框架化后分发 | 可纳入批次 3 |
| P2 | 调试辅助（ssn_info 工具） | 利用内置 /urls /health | 可纳入批次 3 |
| P3 | 多语言绑定（Python/Go） | 框架稳定后 | 后续 |
| P3 | DDS 阶段 1 C++ 对齐 | SsnService 预留扩展点 | 阶段 1 时 |

## 7. 实施规划（四批，每批独立分支 + TDD）

```
批次 1（命名 + 教培文档）：feature/dev-exp-docs
├── SSN 命名定义（3 处）+ 5 新文档 + 总索引
└── 链接/一致性验证

批次 2（C++ 服务框架）：feature/cpp-framework（2a/2b/2c 子步，TDD）
├── 2a 框架核心 + 内置端点
├── 2b nlohmann/json 类型安全层
└── 2c 示例 + 测试 + CMake

批次 3（网站 + 包管理）：feature/docs-site
├── mkdocs + GitHub Pages CI + vcpkg/conan + ssn_info（可选）

批次 4（收尾）：发版 2.4.0（C++ 框架为次版本特性）
├── 版本同步 + CHANGELOG + tag
```

**版本计划注**：C++ 服务框架优先于 DDS 阶段 1——本规划批次 4 发版 **2.4.0**（C++ 框架）；原 DDS 路线图中阶段 1（DCPS 概念模型）的目标版本 2.4.0 **顺延为 2.5.0**（后续 DDS 规格更新时同步调整）。

**验证**：每批 TDD + 全量回归（7 套件 + 示例 + C++ 目标）+ 文档检查。
