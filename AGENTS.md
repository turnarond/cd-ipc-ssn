# AGENTS.md

C99 IPC 框架（RPC/PubSub/消息，Unix/TCP/UDP）。产物两个共享库：`libssn_transport.so`（C 核心）+ `libssn_framework.so`（C++17 服务框架，链接前者）。

## 环境与构建（关键：Linux-only）

- 仅支持 Linux/POSIX。**本仓库位于 Windows，构建与测试必须在 WSL 中运行**，不要在 Windows 原生环境执行 cmake/make：

```bash
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh"
```

- 工具链门槛：CMake ≥ 3.12；C 库 GCC ≥ 4.8；`ssn_framework` 需 GCC ≥ 7 / C++17。

## 验证（改完代码必须全绿）

CI（`.github/workflows/ci.yml`）依次跑三步，本地提交前同样跑齐：

```bash
bash test/run_tests.sh        # 构建 + 16 个自动化套件
bash test/verify_exports.sh   # 公开符号导出校验（需先构建）
bash test/verify_examples.sh  # 19 个示例构建 + hello_world 冒烟（需先构建两库）
```

测试体系怪癖：

- 无外部测试框架：C 套件用文件内自定义 `ASSERT` 宏，C++ 用 `CHECK` 宏。**无法按用例名过滤，只能整文件运行**。
- 高级测试 `test_comprehensive` / `test_thread_safety` / `test_stress` **需要先手工启动服务端**，不在自动化套件内。
- 新增功能必须附带测试（TDD 红-绿-重构是硬性要求），测试放 `test/` 并加入 `test/run_tests.sh` 与 `CMakeLists.txt`。

## 易踩坑的架构事实

- 分层依赖方向严格由外向内：节点抽象(`src/node/`) → client/server(`src/ssn_client.c` 等) → 协议(`src/protocol/`) → 帧编解码(`src/ssn_frame.c`) → 传输(`src/transports/`) → VSI 平台抽象(`src/vsi/`)。不得越层调用。
- API 是**事件循环驱动**：连接握手、收发、回调都靠周期调用 `ssn_node_poll` / `ssn_client_poll` / `ssn_server_poll`（通常放独立线程）。写测试/示例时漏掉 poll 会得到「消息丢失」假象。
- 回调可能在其他线程执行，**回调内禁止调用 `ssn_client_close`**（引用计数线程安全设计，见 `docs/03-设计/核心模块/线程安全设计.md`）。
- 两库都开了 `-fvisibility=hidden`：公开函数必须标 `SSN_API`（`src/ssn_export.h`），否则不导出——曾有消费者 find_package 链接失败的回归，`verify_exports.sh` 就是为此而设。
- **新增公共头文件**必须同步 `CMakeLists.txt` 的 install 规则并确认完整 include 引用链（如 `ssn_node.h` 相对引用 `../ssn_client.h`），缺一则安装后无法编译（见 CMakeLists.txt:160-176 注释）。
- 命名前缀：公开符号 `ssn_`；VSI 内部保留 `ipc_`。错误码统一用 `SSN_ECODE_*`（`src/ssn_error.h`），错误必须打日志。

## 流程硬约束

- **禁止直接提交 main**：需求走 `feature/<简述>`，修复走 `fix/<简述>`，测试通过后合回。
- SemVer：需求变更次版本 +1，bug 修复修订版本 +1。发版必须同步四处：`VERSION`、`src/version/ssn_version.h`、`CMakeLists.txt`(VERSION_MAJOR/MINOR/PATCH/SOVERSION)、`CHANGELOG.md`，然后打 tag `vX.Y.Z`。
- 代码变更必须同步 `docs/` 对应文档（文档腐败即 BUG）；非阻断问题创建 Issue 并打「技术债」标签，不顺手动代码。
- 完整治理规范见 `docs/08-工程规范/产品级框架约定规则.md` 与全局 skill `engineering-governance`；更多细节见根目录 `CLAUDE.md`。

## 文档与注释语言

- 所有交互、注释、生成的文档与文件夹名称一律中文（API/协议等专有名词除外）。
- `docs/` 按编号目录组织（01-白皮书 … 09-归档），新文档放入对应编号目录。
