# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# cd-ipc-ssn（SSN 进程间通信框架）

基于 C99 的高性能 IPC 框架：支持 RPC、发布/订阅（PubSub）、点对点消息，运行于 Unix Domain Socket / TCP / UDP 之上。分层架构：节点抽象层 → 客户端/服务端层 → 协议层 → 传输层 → VSI 平台抽象层。

## 常用命令

构建（产物 `libssn_transport.so` + `libssn_framework.so`，仅支持 Linux/POSIX，CMake ≥ 3.12；C 库 GCC ≥ 4.8，C++ 框架库需 GCC ≥ 7 / Clang ≥ 6，C++17）：

```bash
mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

运行测试（构建后位于 `build/`，每个可执行文件独立运行，全部通过后退出码为 0）：

```bash
./test_transport            # 传输层（67 断言）
./test_node_basic           # 节点基础（3 用例）
./test_node                 # 节点完整（6 用例）
./test_protocol             # 协议层（25 断言）
./test_protocol_integration # 协议集成（19 用例）
./example_server && ./example_client  # 服务端/客户端 API 功能测试
./test_cpp_*                # C++ 框架套件（7 个：service_base / service_task / service_manager / ssn_service / ssn_client / json / stability）
```

注意：

- 测试框架为各测试文件内自定义的 `ASSERT` 宏（C 套件）/ `CHECK` 宏（C++ 套件，统计 `g_cpp_passed`/`g_cpp_failed`），无外部测试框架；无法按用例名过滤，只能整体运行一个测试文件。
- 高级/手工测试：`./test_comprehensive`、`./test_thread_safety`、`./test_stress`（需要先手工启动服务端）。
- 一键验证：`bash test/run_tests.sh`（构建 + 14 个自动化套件）、`bash test/verify_examples.sh`（19 个示例构建 + hello_world 运行冒烟）。

## 架构分层

| 层 | 职责 | 位置 |
|---|---|---|
| 节点抽象层 | server + client 双角色封装 | `src/node/ssn_node.c`（`ssn_node_comm.c` 为通信实现） |
| 客户端/服务端层 | 连接管理、事件循环、消息路由、引用计数线程安全 | `src/ssn_client.c`、`src/ssn_server.c`、`src/ssn_cliauto.c`（自动重连/订阅状态机） |
| 协议层 | RPC 请求/应答、发布/订阅、点对点消息 | `src/protocol/{rpc,pubsub,msg}/`，公共定义在 `src/protocol/ssn_protocol.h` |
| 线协议 | 帧编解码 | `src/ssn_frame.c`（`ssn_header_t`） |
| 传输层 | Unix/TCP/UDP + 工厂；地址格式 `tcp://host:port`、`unix:///path`、`udp://host:port` | `src/transports/` |
| VSI 平台抽象 | 套接字/事件/线程/互斥锁，仅内部使用（保留 `ipc_` 前缀） | `src/vsi/` |
| 公共组件 | 日志、哈希表、版本、错误码 | `src/util/`、`src/version/`、`src/ssn_error.c` |

数据流（发送）：Node → Client → Protocol → Transport；接收方向相反。`ssn_client`/`ssn_server` 内部持有多协议实例，通过 `ssn_transport_t` 与传输层解耦。

## 命名与代码规范

### 命名
- 公开符号统一 `ssn_` 前缀；VSI 平台抽象内部保留 `ipc_` 前缀；类型 `ssn_<module>_t`；函数 `ssn_<module>_<action>`；宏 `SSN_UPPER_CASE`。
- 变量与函数使用 snake_case；结构体用 `typedef struct` 定义并加 `_t` 后缀，成员按类型大小排序（大的在前）。

### 代码风格
- 缩进：4 空格，不使用制表符；`{` 放在行尾、`}` 放在新行；控制语句（if/for/while/switch）必须使用大括号。
- 注释：文件头注释说明文件功能；函数注释说明功能/参数/返回值；行内注释用 `//`（与代码至少一个空格）；长注释用 `/* */`。
- 头文件：包含顺序为系统头文件 → 第三方头文件 → 自定义头文件；必须使用 `#ifndef`/`#define`/`#endif` 保护。
- 错误处理：错误必须用 `LOG_ERROR`/`LOG_WARNING` 等日志记录；错误码统一使用 `ssn_error.h` 中的 `SSN_ECODE_*`；返回值明确表示成功/失败。
- 长度限制：函数长度 ≤ 200 行；每行 ≤ 120 字符。

### 工具
- 使用 clang-format（`.clang-format`）统一格式化代码；用 cppcheck 做静态代码分析；定期代码审查。

### 相关文档
- 线程安全设计见 `docs/03-设计/核心模块/线程安全设计.md`：`ssn_client` 采用引用计数 + 细粒度锁 + `valid` 状态标记；回调可能在其他线程执行，回调中不得调用 `ssn_client_close`。
- 注意：`docs/03-设计/核心模块/线程安全设计.md` 正文中仍保留 VSI 内部 `ipc_` 命名示例，以代码实际为准。

## 文档规范（必须遵守）

- 所有交互、代码注释、生成的文档与文件夹名称一律使用中文（API 名、协议名等专有名词除外）。
- 文档目录按阅读顺序编号排列：

```
docs/
├── README.md（文档总索引）
├── 01-白皮书（架构白皮书）
├── 02-需求分析（需求分析文档）
├── 03-设计（架构设计、核心模块）
├── 04-实施规划（迁移指南）
├── 05-部署手册（部署手册）
├── 06-使用手册（API使用指南、使用指南）
├── 07-测试方案
└── 09-归档（已归档历史文档）
```

- 新增文档必须放入对应编号目录；`docs/` 已按上表编号组织。

## 开发流程（TDD，必须遵守）

- 所有开发流程使用 TDD，永远遵循**红-绿-重构**循环：先写/更新测试 → 确认测试因正确原因失败（红）→ 实现最小修复（绿）→ 重构改进（保持绿）
- **每次请求实现，必须附带对应的测试代码，或指明要让它变绿的测试用例**——不允许无测试的实现
- 新需求先在 `test/` 中编写对应测试用例；bug 修复先编写能复现问题的回归测试
- 测试在本机 WSL 中运行（仓库位于 Windows，Linux 构建环境在 WSL）：

```bash
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh"
```

## 代码整洁与架构整洁（必须遵守）

- **代码整洁**：遵循标准 Clean Code 要求——函数短小单一职责（≤ 200 行）、命名表达意图、消除重复（DRY）、无死代码、注释说明「为什么」而非「是什么」、错误处理完整
- **架构整洁**：遵循标准 Clean Architecture 要求——依赖方向由外向内（接口依赖倒置）、分层职责清晰（本仓库：节点抽象 → 客户端/服务端 → 协议 → 传输 → VSI）、模块间通过接口解耦、可独立测试
- 新增代码须符合既有分层边界，不得越层调用

## 开发工具集

- 开发过程中按项目工程需要创建对应的工具集（如构建/验证脚本、检查脚本、辅助工具），放入 `test/`、`examples/utils/` 或 `tools/`（按性质）
- 工具脚本保持 LF 行尾（.gitattributes 已强制）、位置无关（基于脚本自身路径定位）

## 问题记录

- 开发过程中发现的问题，如果当前没有时间修改，**记住或直接提 issue 到仓库**（GitHub Issues），在适当时机集中修复
- 已记录的问题（含位置与严重性）应可在 `.remember/` 或 issue 列表中追溯

## 分支与版本迭代规范（必须遵守）

- 每次需求开发、需求变更、bug 修复以及 issue 修改都必须新建分支开发，禁止直接在 `main` 上提交：
  - 需求变更：`feature/<简述>`（如 `feature/node-discovery`）
  - Bug 修复 / issue 修改：`fix/<简述>`（如 `fix/client-deadlock`）
  - 开发完成、测试通过后合回 `main`。
- 版本号采用语义化版本 `主版本.次版本.修订版本`：
  - 需求变更 → 次版本 +1（2.3.0 → 2.4.0）；bug 修复 → 修订版本 +1（2.3.0 → 2.3.1）；破坏性 API 变更 → 主版本 +1。
- 发版前必须同步更新以下位置：
  1. `VERSION` 文件
  2. `src/version/ssn_version.h` 的 `SSN_VERSION_*` 宏与 `SSN_VERSION_STRING`
  3. `CMakeLists.txt` 的 `VERSION_MAJOR/MINOR/PATCH` 与 `SOVERSION`
  4. `CHANGELOG.md`（按 Added / Changed / Fixed / Removed 分组记录，格式参照现有条目）
- 发版提交后打 git 标签：`git tag vX.Y.Z` 并推送（仓库现有 tag：v2.3.1、v2.3.2、v2.4.0~v2.4.4）。

## 产品级约定（最高优先级）

- 完整工程治理规范见 `docs/08-工程规范/产品级框架约定规则.md`（战略对齐、SemVer、需求冻结、SOP、测试体系、防腐败机制、强制检查清单）——与本文档冲突时以规范书最新修订为准，并同步此处。
