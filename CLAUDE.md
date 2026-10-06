# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

SSN（Scalable Socket Network）：轻量级 IPC/分布式通信框架，完整 IPC 栈，运行于 Unix Domain Socket / TCP / UDP 之上，提供 RPC、发布/订阅、点对点消息。

**本文件只提供 Claude Code 必需的构建与架构信息。工程规范、分支/提交流程、评审门禁以 `AGENTS.md` 与 `docs/08-工程规范/产品级框架约定规则.md` 为准（后者最高优先级）。**

## 语言与协作约定

- 交互、代码注释、文档、文件夹/文件名一律用**中文**（API 名、协议名等专有名词除外）；文档目录按阅读顺序编号（`01-白皮书` … `10-推广文章`）。
- **禁止代理直接 `commit` 或 `push`**；实现完成后由维护者统一提交，改动需合并为少量完整提交。每项需求/变更/Issue 从独立分支开始（`feature/*`、`fix/*`、`docs/*`）。
- 不新增 `.superpower`、`.claude`、`.omc` 等 AI/插件中间目录；工具以 Python 组织在职责独立的目录中（现有 `tools/document_guard/`）。
- TDD 红—绿—重构：先写失败测试，再最小实现，最后重构。功能/缺陷修改必须附带对应测试。
- 文档是唯一对外接口：代码、API、版本、行为变化必须同步文档，不得让文档腐败。

## 常用命令

仓库为 Linux/POSIX 目标；**在 Windows 上必须进 WSL 构建与测试**（`/mnt/d/personal/cd-ipc-ssn`）。崩溃定位用 `gdb`（Linux/WSL）或 `cdb`（Windows）。

```bash
cmake -S . -B build && cmake --build build -j4   # 配置并构建两个库、测试、示例
bash test/run_tests.sh                            # 一键：构建 + 全部自动化套件（套件清单见脚本内 TESTS/CPP_TESTS）
cd build && ./test_cpp_node_types                 # 跑单个套件（先跑 run_tests.sh 完成构建）
bash test/verify_exports.sh                       # nm -D 校验公开 C API 导出符号（支持 BUILD_DIR 环境变量）
bash test/verify_examples.sh                      # 构建全部示例 + hello_world 冒烟 + find_package 集成
python tools/document_guard/check_docs.py         # 文档一致性守卫（版本、套件数、链接、AI 引用）
python -m unittest discover -s tools/document_guard/tests -v
```

`test_comprehensive` / `test_thread_safety` / `test_stress` 需手工起服务端，不在 `run_tests.sh` 内。

## 架构

```
应用 / 高级 SDK 用户
  ├── C++ 服务框架  include/ssn/framework  → libssn_framework.so（C++17）
  │     ServiceBase（生命周期状态机）→ ServiceTask（线程池）→ ServiceManager::Run<T>()
  │     → SsnService（服务端基类 + /urls /health /version 内置端点）/ SsnClient（同步 callJson + 订阅）
  └── ssn::Node      include/ssn/node       → 同库（v3.0.0 新增，多 Peer、原始消息、自定义事件循环）
        Node → NodeImpl → PeerRegistry/PeerSession/EventQueue（私有实现位于 src/node/，不安装、不被公共头引用）
────────────────────────────────────────────────────────────
Node C API      src/node/ssn_node.c（旧 ssn_node_* 将在 v3.0.0 移除）
Client/Server   ssn_client_* / ssn_server_*（双向 connect/accept 编排）
协议层           src/protocol/{ssn_protocol, rpc/, pubsub/, msg/}（REQ/REP、PUB/SUB、PUSH/PULL、PAIR）
传输层           src/transports/ssn_transport_{unix,tcp,udp}.c + factory（连接池）
VSI 平台抽象     src/vsi/ipc_{platform,socket,event,thread,mutex}.c（内部符号 ipc_ 前缀）
工具层           src/util/（log、mutex、hash_table）、src/version/
```

**两个动态库**：`ssn_transport`（C99，公开 C API）、`ssn_framework`（C++17，链接前者）。二者均设 `VERSION`/`SOVERSION = 主版本号`，并启用 `visibility=hidden`：

- C 公开函数必须标 `SSN_API`（`src/ssn_export.h`，含 `used`+`noinline`，防止 `-O3` 下被 IPA 局部化——历史回归 P1-1）；库公开头变更需保持源码与二进制 ABI 向前兼容。
- C++ 公开类必须标 `SSN_FRAMEWORK_API`（**不能**加 `used`，类上非法，`-Werror` 直接失败）。例外：类内含嵌套**私有** `Impl` 时（如 `ssn::Node`）必须逐成员标注——类级标注会把可见性传给嵌套类，私有实现符号进入动态符号表（实测 `Node::Impl` 泄漏 11 个符号）。改完用 `nm -D -C build/libssn_framework.so` 复核。
- 新增公开 C 符号后，把符号名加进 `test/verify_exports.sh` 的 `REQUIRED` 列表。

**事件循环驱动是全局模型**：`ssn_node_poll` / `ssn_client_poll` / `ssn_server_poll` 必须被周期性调用（或置于独立线程），连接握手、订阅生效、消息收发与回调才会发生——例如发布前需先 poll 服务端，否则订阅握手未生效会丢首条消息。`libssn_framework` 的 `ServiceManager::Run` 与 `ssn::Node::startBackground` 内部封装了该循环。

`ssn::Node` 采用的约束（见 `docs/03-设计/方案设计/2026-09-14-C++17多Peer-Node设计.md`）：外部 `poll()` 与 `startBackground()` 二选一，首次成功调用后生命周期内不得混用；回调执行期间用户回调不持内部锁，但回调中不得调用会等待当前事件线程的操作（`poll()` 返回 `InvalidState`，`waitStopped()` 返回 `WouldDeadlock`）；`MessageView` 只在回调期间有效，跨线程需 `copy()`；`PeerId` 含代际，过期 ID 不得命中新连接；锁序固定 `NodeImpl → PeerRegistry → PeerSession`。

头文件安装同时保留**顶层扁平路径**与**镜像源码树的子目录路径**（如 `include/transports/`、`include/node/`），后者是公共头之间相对引用成立的前提；`CMakeLists.txt` 中新增公共头需按此双份安装，并补齐引用链上被间接包含的头文件。

## 改代码时容易踩的坑

- **新增/改动测试套件**：需改 `CMakeLists.txt`（`add_executable` + 链接 `ssn_framework`/`ssn_transport`）与 `test/run_tests.sh` 的 `TESTS`/`CPP_TESTS` 数组。
- **测试数字是受守卫的事实**：文档中的「自动化套件数 / 断言数 / 示例数 / test_protocol 断言数」口径硬编码在 `check_docs.py` 的 `KEY_FACT_PATTERNS`，分布在 README、白皮书、需求分析、测试架构、部署手册、CHANGELOG 等处约 20 个文件。变更套件或断言数时，一并改守卫基线与全部文档口径（守卫测试先红，见 `tools/document_guard/README.md`），否则 CI 的文档一致性检查失败。v3.0.0 已新增 7 个 Node 套件（types/peer_registry/lifecycle/backend/integration/backpressure/concurrency），守卫基线已同步为 24 套件、1479 例；`check_docs.py` 须保持 0 问题。
- **版本号五处同步**：`VERSION`、`src/version/ssn_version.h`、`CMakeLists.txt` 的 `VERSION_MAJOR/MINOR/PATCH`、`CHANGELOG.md` 最新 `## [x.y.z]`，以及文档口径。
- **新增文档**：须在 `docs/README.md` 与 `docs/_sidebar.md` 登记（守卫校验相对链接有效）。`docs/**` 内**禁止**出现 `CLAUDE.md`、`.claude`、`superpowers` 等字样（守卫 `PROHIBITED_REFERENCES`）——因此本文件刻意不进 docs 索引。
- **长期运行稳定性**：评审与测试需覆盖内存/句柄/线程泄漏、并发与异常路径，不只覆盖功能happy path。
