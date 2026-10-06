# Repository Guidelines

## 项目结构与模块组织

核心 C/C++ 源码位于 `src/`：传输层在 `src/transports/`，协议在 `src/protocol/`，节点抽象在 `src/node/`，平台适配在 `src/vsi/`，C++17 服务框架在 `src/framework/`。公共 C++ 头文件位于 `include/ssn/framework/`，测试位于 `test/`，可运行示例位于 `examples/`，第三方单头依赖位于 `third_party/`。对外文档统一维护在 `docs/`，沿用 `01-白皮书`、`02-需求分析`、`03-设计` 等中文编号目录；文档是唯一对外接口，代码、API、版本或行为变化必须同步更新文档。

## 构建、测试与开发命令

```bash
cmake -S . -B build          # 配置 C99/C++17 工程
cmake --build build -j4      # 构建动态库、测试及示例
bash test/run_tests.sh       # 构建并运行 23 个自动化套件
bash test/verify_exports.sh  # 校验公开 API 导出符号
bash test/verify_examples.sh # 验证 19 个示例及消费集成
```

Linux/POSIX 为支持环境；仓库位于 Windows 时必须在 WSL 中构建和测试。疑难崩溃可用 Linux `gdb` 或 Windows `cdb` 定位。

## 编码风格与命名约定

交流、代码注释与生成文档一律使用中文（API、协议等专有名词除外）。遵循现有 C99/C++17 风格，并以《代码整洁之道》《架构整洁之道》为准绳、全局考量：4 空格缩进，职责单一，依赖方向清晰、分层一致，避免跨层耦合。公开 C 符号使用 `ssn_` 前缀，类型采用 `ssn_<module>_t`，函数采用 `ssn_<module>_<action>`，宏采用 `SSN_UPPER_CASE`；VSI 内部符号使用 `ipc_`。不要引入 `.superpower`、`.claude`、`.omc`、`SDD` 等 AI/插件/流程中间目录，保持 Git 仓库工程结构简洁可维护。项目工具优先以 Python 组织成职责明确、自成工程的系列配套工具，用于测试、部署和冒烟验证。

## 测试与架构要求

所有功能或缺陷修改必须遵循 TDD 的“红—绿—重构”：先新增失败测试，再完成最小实现，最后清理结构。测试文件命名为 `test/test_<module>.c` 或 `.cpp`，并接入 `CMakeLists.txt` 与测试脚本。评审需覆盖边界、并发、异常路径及长期运行中的内存、线程、句柄泄漏。

测试遇阻须定位根本原因：连续追问“为什么”，同时查清业务逻辑根因与技术约束根因；可用 Linux/WSL `gdb` 或 Windows `cdb` 调试，Windows 不便时转到 WSL。属逻辑缺陷的必须从根本修复，禁止循环叠加无关紧要的临时补丁；修复经评审后须删除临时补丁代码，以免误导后续维护。

SDK 头文件或接口变更必须保持源码与二进制 ABI 向前兼容，避免使用者因 ABI 不匹配而被迫用新 SDK 重编。若新功能与整体架构冲突较大、改动别扭，先共同讨论重构方案再动手。

## 分支、提交与交付

每项需求、变更或 Issue 修改都从独立分支开始，如 `feature/node-qos`、`fix/socket-leak`。禁止代理直接 `commit` 或 `push`；由维护者统一提交，相关改动应合并为少量、完整提交。历史采用 `docs:`、`fix:`、`release:` 等前缀，提交信息应简洁说明结果。PR 需包含变更目的、关联 Issue、验证命令与结果；界面变化附截图。

产品研发按 SDD 推进需求、方案设计、任务规划、实施和交付，关键阶段安排专家评审，其中方案设计评审为必经门禁，未通过不得进入实施。白皮书与 roadmap 应明确版本边界；打包发布时同步创建 Git tag 并记录版本信息。非本项目问题只做必要排查，向对应工程、SDK 或文档供应方提交 Issue；暂缓的本项目问题也应登记 Issue。
