# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

# cd-ipc-ssn（SSN 进程间通信框架）

基于 C99 的高性能 IPC 框架：支持 RPC、发布/订阅（PubSub）、点对点消息，运行于 Unix Domain Socket / TCP / UDP 之上。分层架构：节点抽象层 → 客户端/服务端层 → 协议层 → 传输层 → VSI 平台抽象层。

## 常用命令

构建（产物 `libssn_transport.so`，仅支持 Linux/POSIX，CMake ≥ 3.12，GCC ≥ 4.8）：

```bash
mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

运行测试（构建后位于 `build/`，每个可执行文件独立运行，全部通过后退出码为 0）：

```bash
./test_transport            # 传输层（55 用例）
./test_node_basic           # 节点基础（3 用例）
./test_node                 # 节点完整（5 用例）
./test_protocol             # 协议层（25 用例）
./test_protocol_integration # 协议集成（19 用例）
./example_server && ./example_client  # 服务端/客户端 API 功能测试
```

注意：

- 测试框架为各测试文件内自定义的 `ASSERT` 宏（统计 `g_tests_passed`/`g_tests_failed`），无外部测试框架；无法按用例名过滤，只能整体运行一个测试文件。
- 高级/手工测试：`./test_comprehensive`、`./test_thread_safety`、`./test_stress`（需要先手工启动服务端）。
- `test/run_tests.sh` 已过时（引用了已删除的 `test_ipc_*` 目标），不要依赖它。
- `CMakeLists.txt` 中 project `VERSION` 仍停留在 2.0.0，与 `ssn_version.h`（2.3.0）不一致，改版本号时需同步（见「版本迭代规范」）。

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

- 公开符号统一 `ssn_` 前缀；VSI 平台抽象内部保留 `ipc_` 前缀；类型 `ssn_<module>_t`；函数 `ssn_<module>_<action>`；宏 `SSN_UPPER_CASE`。
- 完整代码风格见 `CODE_STYLE.md`（4 空格缩进、snake_case、头文件 `#ifndef` 保护、错误必须用 `LOG_ERROR` 等记录、函数 ≤ 200 行、行长 ≤ 120 字符）。
- 线程安全设计见 `THREAD_SAFETY.md`：`ssn_client` 采用引用计数 + 细粒度锁 + `valid` 状态标记；回调可能在其他线程执行，回调中不得调用 `ssn_client_close`。
- 注意：`CODE_STYLE.md`、`THREAD_SAFETY.md` 正文中仍残留旧 `ipc_` 命名示例，以代码实际为准。

## 文档规范（必须遵守）

- 所有交互、代码注释、生成的文档与文件夹名称一律使用中文（API 名、协议名等专有名词除外）。
- 文档目录按阅读顺序编号排列：

```
docs/
├── 01-蓝图（白皮书、愿景）
├── 02-需求分析
├── 03-设计（架构设计、核心模块设计）
├── 04-实施规划
├── 05-部署手册
├── 06-使用手册
└── 07-测试方案
```

- 新增文档必须放入对应编号目录；当前 `docs/` 下已有 `架构设计`、`核心模块`、`实施规划`、`使用指南`、`测试方案` 等未编号目录，后续整理时按上表映射补号。

## 开发流程（TDD，必须遵守）

- 所有开发流程使用 TDD：先写/更新测试 → 确认测试因正确原因失败 → 实现最小修复 → 重新运行验证通过
- 新需求先在 `test/` 中编写对应测试用例；bug 修复先编写能复现问题的回归测试
- 测试在本机 WSL 中运行（仓库位于 Windows，Linux 构建环境在 WSL）：

```bash
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh"
```

## 分支与版本迭代规范（必须遵守）

- 每次需求变更或 bug 修复都必须新建分支开发，禁止直接在 `main` 上提交：
  - 需求变更：`feature/<简述>`（如 `feature/node-discovery`）
  - Bug 修复：`fix/<简述>`（如 `fix/client-deadlock`）
  - 开发完成、测试通过后合回 `main`。
- 版本号采用语义化版本 `主版本.次版本.修订版本`：
  - 需求变更 → 次版本 +1（2.3.0 → 2.4.0）；bug 修复 → 修订版本 +1（2.3.0 → 2.3.1）；破坏性 API 变更 → 主版本 +1。
- 发版前必须同步更新以下位置（当前不一致，需一并修正）：
  1. `VERSION` 文件
  2. `src/version/ssn_version.h` 的 `SSN_VERSION_*` 宏与 `SSN_VERSION_STRING`
  3. `CMakeLists.txt` 的 `VERSION_MAJOR/MINOR/PATCH` 与 `SOVERSION`
  4. `CHANGELOG.md`（按 Added / Changed / Fixed / Removed 分组记录，格式参照现有条目）
- 发版提交后打 git 标签：`git tag vX.Y.Z`（当前仓库尚无任何 tag）。
