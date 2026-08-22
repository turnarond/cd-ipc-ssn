# Changelog

All notable changes to the ssn (cd-ipc-ssn) IPC framework.

## [2.5.7] - 2026-08-22

### Performance
- **接收路径 head 偏移（评审 P1-9）**：`ssn_stream_feed` 原实现每处理完一个包就
  memmove 剩余字节——小包突发（128KB 内 n 个 64B 包）总移动量 O(n²)，可到百 MB 级。
  修复：`ssn_stream_ctx_t` 增加 head 读偏移——解析从 `buffer+head` 读，包消费只推进
  head（O(1) 不搬数据）；仅当缓冲近满（无法容纳新数据）时 compact 一次。行为等价
  （分片帧/粘包/跨 feed 调用均保持），16 套件回归全绿 + ASAN 0 错误

## [2.5.6] - 2026-08-22

### Fixed
- **node destroy 生命周期加固（功能评审 P1-15）**：`ssn_node_destroy` 原无 valid
  标志且 ref_count 无递增点（延迟销毁分支死代码）——destroy ACTIVE 节点时内部调
  `ssn_node_stop`（重新加锁）依赖锁序正确，头文件未明确「destroy 只能调用一次」
  （重复调用即悬垂 UB）。修复：`ssn_node_t` 加 valid 标志（create 置 true、
  destroy 置 false，防未 free 重入路径）；头文件明确单次约束与 ref_count 预留
  说明；`test_node` 新增 Test 7（ACTIVE 节点 destroy 幂等生命周期，ASAN 0 错误）

### Docs
- `SsnService.hpp` builtinVersion 注释版本号去硬编码（引用 SSN_VERSION_STRING，
  避免每次发版同步）

## [2.5.5] - 2026-08-21

### Fixed
- **cliauto keepalive 参数被忽略（功能评审遗留 P1）**：CONNECTED 分支原以
  ping(50ms)+poll(10ms) 忙等循环（约 60ms/圈），`keepalive` 从未使用（与头文件/
  文档「keepalive 为 ping 间隔」不符，空闲连接持续空转 CPU）。修复：每 tick 结束
  按 keepalive 睡眠；ping 超时窗口从硬编码 50ms 改为 keepalive
- **`ssn_stream_feed` 回调语义文档澄清（功能评审遗留 P1）**：头文件 @return 未说明
  「回调返回 false = 请求停止处理（正常结束）」——实现把停止当成功返回 true 是
  合理行为，但文档误导（停止≠错误，流层错误才返回 false）。修复：头文件补回调
  语义说明，实现注释明确；调用方行为不受影响

## [2.5.4] - 2026-08-21

### Fixed
- **`ssn_client_set_on_publish` 死接线（P1-3）**：`ssn_client_handle_publish` 无匹配
  订阅时仅兜底 onmsg、从不读 onsub——cliauto 内部 `set_on_publish(ssn_client_auto_msg_cb)`
  永不触发，订阅消息丢失。修复：无匹配时优先调用 onsub（PUBLISH 语义），再兜底 onmsg

### Cleanup（工程化评审批次 B）
- **死代码批量删除（P1-11，均有 grep 零引用依据）**：`ssn_set_url`、`ssn_address_copy/equal`、
  `SSN_ERR_*` 枚举（ssn_error_t 更名残留）、factory `register/cleanup/is_type_supported/
  get_supported_types`、`ssn_node_run`、`ssn_server_peer_address`、hash_table
  `contains/capacity/is_empty/foreach/hash_int/hash_pointer` + allocator typedef、
  vsi `ipc_socket_shutdown/set_send_timeout/bind_to_interface`、未用变量
  （ssn_client_connect 的 errcode/ret/on/off/opt/suc、ssn_server_start 的 en）——共 370 行
- **重构（DRY）**：抽取 `ssn_client_collect_and_call_pending`（消灭三份「锁内收集
  pending + 锁外回调」拷贝，含三份 struct timeout_item 定义）；抽取
  `node_ensure_connected`（消灭 send_to_peer/subscribe/rpc_call 三份「未连接则
  connect + 更新 peer_address」拷贝）
- **常量收敛（P1-12）**：`IPC_*` 应用层常量改名 `SSN_*`（TIMER_PERIOD/SERVER_BACKLOG/
  DEF_SEND_TIMEOUT/SERVER_DEF_HANDSHAKE_TIMEOUT/SERVER_KEEPALIVE_TIMEOUT），
  消除与 ssn_ 命名体系并存；删除 transport 死宏（MAX_BUFFER_SIZE/DEFAULT_TIMEOUT_MS/
  DEFAULT_BACKLOG）
- 注释/命名/拼写清理：文件头 `ipc_*`→`ssn_*`、`IPC client/server`→`SSN`、
  `sendmsg faield`/`Registeation` 拼写、过时 TODO（deal recv msg / init recv buffer /
  搬迁残留）、connect 默认超时注释（3 秒非 5 秒）

### Changed
- 测试：16 套件 715 例保持全绿；verify_exports 通过（151 符号、25 关键 API）

## [2.5.3] - 2026-08-21

### Fixed
- **服务端 hst 链表 UAF（P0，确定性堆破坏）**：`ssn_server_cli_destroy` 用
  `cli->hst.alive` 判断是否从 hst 链表摘除，而 alive==0 同时表示「定时器到期未消费」
  与「已摘除」二义——定时器将 alive 置 0 并 signal evtfd 后、evtfd 被消费前，对端
  FIN 触发 recv 0 → destroy 因 alive==0 跳过摘除即 free(cli)，残留 hst 节点随后被
  `ssn_server_handle_event_input` 遍历（DELETE_FROM_LIST + 读 cli->transport）→ UAF。
  修复：hst 增加显式 `linked` 标志（入链置 true、摘除置 false），三处摘除路径统一
  以 linked 判断链表归属；回归测试：`test_ssn_server` Test 10（手动控制 poll 时序，
  修复前 ASAN 稳定报 heap-use-after-free）
- **锁内 EAGAIN 忙等（P0，慢对端冻结服务端 5s/DoS）**：`ssn_send_message` 非阻塞
  send 遇 EAGAIN 时 `nanosleep(1ms)` 盲重试最多 send_timeout_ms（默认 5000ms），
  不检测 socket 可写性；调用方持锁（server_response/do_publish/client_call），慢对端
  填满 SO_SNDBUF 后事件循环持锁空转最多 5s。修复：改 `poll(POLLOUT)` 等待可写
  （受剩余超时预算约束），可写后立即重试
- **`ssn_client_request` 无锁 pending 登记 + ref 泄漏（P0）**：subscribe/unsubscribe/
  ping 的 `alloc_pending_index`+`seqno++`+`seqno_to_index` 登记在加锁前执行，与定时器
  线程、并发请求竞态（槽位重复/seqno 重复 → 应答串线）；pending 池满时 return false
  未 unref（引用泄漏）。修复：登记整体移入锁内（与 call_ex 对齐），失败路径补 unref
- **公开 API 导出缺失（P1）**：`-fvisibility=hidden` 下 ssn_frame.h/ssn_error.h/
  ssn_node.h 部分函数未标 SSN_API 不导出（外部 find_package 消费者链接失败）；
  且 -O3 下声明处 default 可见性可能被 IPA 丢弃（符号残留 GLOBAL HIDDEN）。修复：
  补 SSN_API + `used`+`noinline` 属性 + 关键函数定义处兜底；新增
  `test/verify_exports.sh`（nm -D 断言 25 个关键 API 导出）并接入 CI
- **`ssn_client_ping` 栈 use-after-return（P1）**：回调 arg 指向栈上 volatile bool，
  超时返回后 pending 残留，迟到应答/超时回调写已失效栈帧。修复：等待结束后主动
  撤销 pending 登记
- **RPC 方法并发移除 UAF（P1）**：`ssn_server_handle_rpc_request` 解锁后读取
  `cmd->arg`，跨线程 remove_method 时读已释放内存。修复：锁内拷贝 callback 与 arg
- **`ssn_server_run` 无引用计数守卫（P1）**：跨线程 destroy 后 run 循环仍访问
  server（UAF）且未 start 时无限空转。修复：循环持引用计数 + valid 守卫 +
  max_fd<0 退出（对齐 poll 的 ref 模式）
- **`ssn_client_send_timeout` 忽略 connect 结果（P1）**：重建 transport 时 connect
  失败仍发布 fd=-1 的新 transport（假连接状态卡死）。修复：失败保留旧 transport
- **并发 `ssn_client_close` 双重删除全局链表（P1）**：valid 检查/置位与链表删除
  分属不同锁，双线程 close 时第二个 DELETE 命中已摘除节点 → 链表头丢失。修复：
  valid 检查/置位移入 client->lock
- **`free_pending_index` 不清 seqno 映射（P1）**：槽位释放后 seqno_to_index 残留，
  seqno 回绕/迟到应答错配。修复：释放时同步清映射（校验槽位 seqno）
- **NULL 解引用（P1/P2）**：`ssn_client_fds(NULL)` 先解引用 evtfd、
  `ssn_client_subscribe` 未连接时日志参数解引用 NULL url——判空前置
- **删除 `src/core/ssn_global.h` 死头文件（P0）**：与 `src/ssn_global.h` 同名同
  保护宏、内含冲突常量与遗留 API，任何 TU 误包含会静默吞掉真实头文件声明

### Changed
- 测试数字同步：自动化 16 套件 715 例（C 230 + C++ 485，实测）；README/部署手册/
  工程规范/测试架构/CLAUDE.md 全量核对；CI 注释 14→16 套件
- 测试体系：`test_ssn_server` 新增 Test 10（hst UAF 回归），10 用例

## [2.5.2] - 2026-08-20

### Fixed
- **空闲连接误判断开（Issue #22，P0 回归）**：`ssn_client_process_events` 局部变量
  `pkt_e` 未初始化——socket 无数据（`did_recv=false`）时读取未初始化栈值（UB），垃圾值
  可能为 true → 误判「连接丢失」→ 断开。v2.5.1 保活改造后 cliauto 每次 tick 都 poll，
  空闲连接稳定触发「建立后 ~50ms 误断 → 循环重连」（edge-framework 场景复现）。修复：
  `pkt_e` 初始化为 false；回归测试：`test_cliauto` Test 5（空闲 5 轮循环无断开）、
  `test_ssn_client` Test 13（空闲 poll 保持连接）
- **transport 发布/销毁竞态 UAF（P0）**：`ssn_client_poll` 无锁销毁 transport 与
  `ssn_client_connect` 无锁赋值竞争——poll 线程检测旧连接丢失时销毁
  `client->transport`，可能销毁 connect 刚创建/正在使用的新 transport（UAF）。
  稳定性套件 T6（服务端重启重连）在负载下偶发：glibc fd_set 越界 abort /
  tcache 堆损坏（ASAN 定位 `unix_transport_connect` 读已释放 transport）。修复：
  connect 全程使用局部 transport、握手成功后一次性锁内发布；poll 销毁持锁；
  `ssn_client_call_ex` sendmsg 移回锁内（消除无锁读 transport 窗口）；回归测试：
  `test_ssn_client` Test 14（服务端反复启停 + 并发 poll/connect 30 轮）

## [2.5.1] - 2026-08-20

### Fixed
- **cliauto 保活（Issue #14）**：keepalive ping 实现（原从不发送 PING_ECHO，半开连接
  无法感知，自动重连永不触发）——新增 `ssn_client_ping` API，CONNECTED 分支按周期
  ping，连续 `SSN_CLIENT_AUTO_MAX_PING_LOST` 次无应答判定断开；状态机 state/running
  读写全部在 mutex 下（消除数据竞争 UB）；错误路径补 LOG；删除死代码
- **server 握手竞态（Issue #15）**：`conn_timeout_ms<=0` 回退默认握手超时（原 alive=0
  使定时器首个 tick 销毁迟到的握手连接——「连接建立但握手迟到」真实竞态）；
  `ssn_server_poll` 负数超时按无限等待处理（原 -1 计算非法 timespec → pselect 恒 EINVAL）
- **哈希表字符串键（Issue #16）**：新增 `ssn_hash_table_*_str` API（内容哈希 + 表内
  复制 key），修复按指针比较导致的内容相同地址不同字符串注销静默失败、栈 buffer key
  悬挂、重复注册旧值泄漏；rpc_register/pubsub subscribe 改用字符串键 API；
  公开函数补 SSN_API 导出标记
- **ssn_framework 版本化（Issue #17）**：VERSION/SOVERSION + CXX_VISIBILITY_PRESET
  hidden（升级不再覆盖旧文件）；新增 SSN_FRAMEWORK_API 导出宏（5 个公开类标记导出）
- **server 命名与死代码（Issue #18）**：内部函数 `ipc_server_handle_*` 更名
  `ssn_server_handle_*`（ipc_ 前缀仅限 VSI）；新增 `ssn_server_options_t` 别名
  （兼容非破坏）；删除空壳死代码；`ssn_client_ref/unref` 补头文件声明（消除隐式声明）
- **协议层（Issue #19）**：RPC 应答复用 `SSN_MSG_TYPE_RPC_REQUEST`（原私有宏 0x10
  与主路径两套体系不一致）；REQ 分支校验应答类型；seqno 分配跳过在途序号（回绕保护）；
  base.destroy 统一为子类 destroy（消除 double free 隐患）

### Changed
- 测试体系扩至 **16 套件**（新增 test_cliauto 19 断言、test_hash_table 50 断言；
  example_server 9 用例），run_tests.sh 同步

## [2.5.0] - 2026-08-20

### Added
- **CMake 包配置（find_package）**：`install(EXPORT ssnTargets)` 导出
  `ssn::ssn_transport` / `ssn::ssn_framework` 目标，生成并安装
  `ssnConfig.cmake` / `ssnConfigVersion.cmake`——CMake 项目可
  `find_package(ssn REQUIRED)` 后直接 `target_link_libraries(... ssn::ssn_transport)`
  集成；include 目录改为 BUILD/INSTALL_INTERFACE 分离（安装后以 `<prefix>/include`
  为根，源码树内仍用 `src/`）
- **CI 持续集成**：`.github/workflows/ci.yml`——Ubuntu 构建 + 全量 14 套件 +
  `verify_examples.sh`（19 示例 + hello_world 冒烟 + find_package 集成验证），
  push 到 main/feature/*/fix/* 与 PR 触发
- **docsify 文档网站**：`docs/index.html` + `_sidebar.md` 零改写接入现有 39 篇文档
  （全文搜索、侧边栏导航），`.github/workflows/pages.yml` 自动部署 GitHub Pages；
  `docs/03-设计`、`06-使用手册`、`07-测试方案`、`09-归档` 补目录索引 README
- **集成示例**：`examples/cmake_integration/`（C 库 + C++ 框架双消费者，
  展示安装后 find_package 用法）

### Changed
- `test/verify_examples.sh` 增加 find_package 集成验证（安装临时前缀 → 消费工程
  配置/构建/运行，C 与 C++ 双消费者）；`.gitignore` 补 cmake_integration 构建产物
- DDS 演进路线顺延：阶段 1（DCPS 概念模型）目标 2.6.0、阶段 2 2.7.0、阶段 3 2.8.0
  （原 2.5.0 起点被本次工程化版本占用）

## [2.4.4] - 2026-08-19

### Fixed
- **用户旅程**：C 示例/教程事件循环空转周期导致客户端连接必然失败——`ssn_client_connect`
  握手重试预算对齐 connect 超时（默认 3s，原固定 500ms）；全部 C 示例事件循环改为事件
  驱动 poll（消除 poll+sleep 空转）；README 第一个应用重写（补服务端 poll/编译命令）
- **线程安全（回归 Test 12）**：client 超时/应答/释放路径回调移出锁外（回调内再调 API
  不再自锁死锁）；sub_handlers 增删查加锁；get_pending 改快照拷贝（TOCTOU）；transport
  读写统一加锁；node_poll 解锁执行回调 + node_comm 锁序修复；server 引用计数延迟释放
  （回调中 destroy 无 UAF）+ 发送改非阻塞（慢客户端不再拖垮事件循环）
- **传输层**：TCP6/UDP6 地址解析支持 `[::1]:port` 方括号格式（原 strchr 取首个冒号
  必失败）；TCP6 listen bind 改用 addr6；非阻塞 connect 补 SO_ERROR 检查（拒连不再
  误报成功）；tcp/unix/udp send 的 EAGAIN 不再误判错误
- **线协议**：`ssn_send_message` 长度预校验（防截断写头部）+ 循环发送处理部分写入 +
  EAGAIN 有界重试
- **协议层**：RPC 超时实现（pending 槽不再永久占用，触发超时回调）；协议工厂创建完整
  子类对象（混用不再崩溃）
- **C++ 框架**：SsnClient connect 数据竞争（并发 connect 不再 std::terminate/节点泄漏）；
  RegisterMethod 增加 Resp 编译期类型约束
- **工具脚本**：run_examples.sh 路径补 examples/ 前缀（原全部判「目录不存在」）+ 两脚本
  转 LF；build_examples.sh 补 4 个 C++ 示例（15→19）；verify_examples.sh 增加
  hello_world 运行冒烟
- **strncpy 溢出**：server ifname/srv_name、node node_id/node_type/node_name 改 snprintf
  （防越界写与非 NUL 终止越界读）

### Changed
- 文档全仓数字同步：自动化 14 套件 625 例（原 13/14/7 并存）、19 个示例（原 15/17/19
  并存）、example_client 12 例（原 5/8/9/10 四个值）
- 白皮书 QoS/节点发现「已实现」→「仅预留能力位/愿景」；3.2/3.3 设计稿标注现状声明；
  DDS 路线阶段 1 顺延 2.5.0 起（原 2.4.0 已过期）
- 使用手册/快速上手/C++ 指南教程模板修正：pause()/sleep 替代 poll 的模板改为事件循环、
  订阅回调 %s→%.*s、subscribe 语义、Run 信号还原、SsnClient connect 语义
- 架构设计总览/协议层模块化设计补现状声明（设计稿/预留用途标注）

## [2.4.3] - 2026-08-19

### Fixed
- transport 构造 fd 泄漏（Issue #10）：tcp/unix/udp_transport_create 构造时创建
  socket fd，connect/accept 路径重建时覆盖未关闭——每连接泄漏 1 个 fd，长期运行
  触发 EMFILE。修复：connect 重建前关闭旧 fd、accept 覆盖前关闭构造 fd
- 回归：Test 10（tcp 失败 + udp 重建路径 fd 不增长）、T8 恢复严格断言
  （10 轮生命周期循环 fd 不增长）

## [2.4.2] - 2026-08-18

### Fixed
- 稳定性加固（深度评审 C1/I1/I2/I4/I5）：
  - 订阅回调异常保护（handleMsg try/catch，修复回调异常 → std::terminate 进程崩溃）
  - ServiceManager::Run 用户钩子异常路径信号状态还原（掩码/处理器/停止标志，全退出路径覆盖）
  - subscribe/unsubscribe 与 disconnect 共享互斥锁（并发 node 释放后使用窗口消除）
  - ServiceTask failed() 失败标志 + /health status "degraded"（svc 线程异常退出可观测，修复服务「假活」）
  - SsnClient pollLoop 空转让步（消除连接后忙等 100% CPU）
- 测试版本断言改为引用 SSN_VERSION_STRING（发版不再触发测试红）

### Added
- 稳定性测试套件（test_cpp_stability，13 用例 286 断言）：回调异常回归、Run 异常信号还原、并发串行化、超时/handler 风暴恢复、服务端重启重连、重复 Run、生命周期 fd 循环、空/半开连接清理、信号风暴、在途调用、并发 subscribe、svc 失败可观测
- 示例：03_robust_client（三态错误处理 + 重连退避 + RAII）、04_concurrent_client（两层串行化教学）——示例总数 15→19

## [2.4.1] - 2026-08-16

### Fixed
- Issue #5 技术债 12 项集中修复：
  - ServiceTask::activate 边界（num_threads<=0 拒绝、线程创建异常回滚）
  - ServiceManager Run 结束还原信号掩码与处理器（含还原顺序修正）
  - registerJson 尾斜杠 URL 拒绝（保留 "/" 兜底）
  - SsnClient disconnect 与 callJson 互斥（并发 UAF 窗口消除）
  - SsnService OnInit 失败回滚（销毁已建节点）
  - ServiceTask 析构回收线程（消除 joinable 线程 terminate）
  - 框架文件版权头补齐、死代码删除
  - pubsub 示例析构兜底（start 失败路径不再 terminate）
- 文档化：publish/unregister 与 stop 并发约束、超时窗口竞态说明

## [2.4.0] - 2026-08-16

### Added
- C++ 服务框架（libssn_framework）：ServiceBase/ServiceTask/ServiceManager 三层渐进基类，ServiceManager::Run\<T\>() 一行启动
- SsnService 服务端基类：类型安全 RegisterMethod\<Req,Resp\>、内置端点 /urls、/health、/version、publish 发布
- SsnClient 客户端：同步 Call\<Req,Resp\>、订阅 subscribe、连接管理
- nlohmann/json v3.11.3 vendor（MIT，DTO 序列化）
- 新增 6 个 C++ 测试套件（170 断言）与 2 个 C++ 示例（examples/cpp/），全量 13 套件 294 例 + 17 示例
- 安装布局修复：ssn_node.h/ssn_error.h/ssn_global.h/nlohmann 纳入 install，并补齐相对引用所需的子目录路径（transports/util/version/vsi），安装后框架头可直接编译
- C++ 服务框架使用指南（docs/06-使用手册/C++服务框架指南.md）与 README/白皮书同步

### Changed
- C 公开头（ssn_log.h、ssn_error.h）加 extern "C" 保护：C++ 可直接包含 C API 头
- 发布前文档修正：README 链接空格修复、C++ 编译器要求补注（GCC ≥ 7 / Clang ≥ 6）

### Fixed
- 服务端 accept 后无条件 recv(timeout=0) 无限阻塞（Issue #4，P1）：任意客户端 connect 后不发数据即可挂死整个服务端（单连接 DoS）——删除立即 recv，由下一轮 poll 的 FD_ISSET 先验接管，回归测试 Test 8

## [2.3.2] - 2026-08-08

### Added
- 回归测试：服务端 idle 超时（定时器线程存活）、64 KiB 大消息往返
- 发布就绪评估报告（`docs/09-归档/发布评估-v2.3.1.md`，双视角评分）

### Changed
- 使用手册与示例全面完善：API 使用指南/使用指南与代码一致（版本号 v2.3.1、`url_len` strlen 语义、示例回调、事件循环驱动说明）、15 个示例 README 按实际代码重写（预期输出/日志/行号真实）、CLAUDE.md 固化开发规范（TDD 红-绿-重构、Clean Code/架构整洁、开发工具集、问题记录）
- 恢复 `ssn_client_set_on_message` 同时设置 onmsg 与 onsub 的行为（与 v2.1 文档描述一致）
- 测试用例数同步：共 122 例（test_node 6、example_client 8）

### Fixed
- 服务端定时器线程在空列表时退出且不重启（ISSUE-D：对称退出标志，idle 超时/心跳检测失效）
- `SSN_MAX_PACKET_SIZE` 宏重定义（ISSUE-E：删除 8192 死宏，统一为协议设计值 131072）
- `ssn_stream_feed` 分片帧接收缺陷（跨多次 recv 的大包被丢弃，累积重组修复）
- examples：multithread 服务器挂死（单事件循环线程 + 停止标志）、timeout/transport_selection 事件循环缺失、消息长度 off-by-one（9 处）、`url_len` 含 NUL 不一致（统一 strlen）、sleep 整数除法延迟失效（2 处）、订阅回调日志 url 乱码（`%s` → `%.*s`）

## [2.3.1] - 2026-08-06

### Fixed
- `ssn_node_destroy` 对 ACTIVE 节点直接销毁时的持锁自锁（回归测试）
- IPv6 地址 snprintf 截断警告（超长地址返回错误）
- examples：error_handling/timeout 超时测试断言改为基于回调结果
- UDP 传输标注「不支持 server 模式握手」限制（源码/设计文档/示例 README）
- ssn_client_poll 毫秒超时换算错误（poll(500) 实际仅等待 500ns，回归测试）
- 客户端定时器线程空列表时不再退出（pending 超时机制可靠，回归测试）

### Changed
- 删除无引用死符号 `ssn_ecode_version`；`struct ipc_node` 内部标签统一为 `ssn_node`
- 补写 02 需求分析（含 DDS 对标需求映射）与 05 部署手册；修复 superpowers 文档坏链

## [2.3.0] - 2026-05-07

### Added
- **driver-sdk Phase 3: Architecture Upgrades**
  - `CollectionScheduler`: unified scan scheduler with priority queues and scan groups for shared-bus devices (e.g., RS-485), replaces naive `sleep(3)` in main loop
  - `DataPipeline`: ring buffer + reporter thread between collection and IPC send; supports store-and-forward for disconnected scenarios
  - `DeviceStateMachine`: explicit `DeviceState` enum (Disconnected→Connecting→Connected→Idle→Error) with exponential backoff reconnection
  - `DiagnosticsCollector`: atomic counters per-device and per-driver (connects, collections, IPC sends, errors, latency); configurable periodic log output
- **driver-sdk Phase 2: Feature Completeness**
  - XML config parsing via Poco DOM (previously empty stub, config was SQLite-only)
  - Resource ownership clarified: `shared_ptr<CDevice>` throughout `CDriver` and `ResourceManager`
- **driver-sdk Phase 1: Stability Fixes**
  - `std::atomic<bool>` for main loop stop flag (was plain `bool` — UB at high optimization)
  - `ipc_mutex_` protects concurrent `obj_mapper_`/`client_handle_` access in `UpdateTagsData`
  - Designated-initializer UB in `ssn_url_ref_t` fixed (moved to constructor init list)
  - `CDevice::DestroyTimers()` implemented; `CUserTimer` destructor now calls `vsoa_timer_stop`/`vsoa_timer_delete`
  - `drv_destroy_timer` implemented; `drv_settagdata_text` boundary checks added
- **Unit tests**: 4 new test suites (diagnostics, state machine, data pipeline, scheduler) with 36 tests

### Changed
- `CDriver::devices_` migrated from raw `CDevice*` to `std::shared_ptr<CDevice>`
- `CMainTask` main loop uses `CollectionScheduler::Tick()` for dynamic-interval sleep
- `CDevice::SetDeviceConnected()` now drives state machine transitions
- `PocoXML` and `PocoFoundation` added to link list

### Fixed
- `new[]`/`free` allocator mismatch risk in `ConfigLoader::FreeDrivers` (changed to `delete[]`)
- `conn_type` in `InitDeviceInterface` now correctly parsed from string to int

## [2.2.0] - 2026-05-06

### Added
- Server API functional test (`example_server`, 4 tests): create/destroy, start/stop, add RPC method, echo
- Client API functional test (`example_client`, 5 tests): create/destroy, connect/disconnect, RPC call, subscribe, send message
- Self-contained test infrastructure (embedded server thread, no external setup required)

### Changed
- `ssn_cliauto`: updated enum `ipc_client_auto_state_t` → `ssn_client_auto_state_t`
- `ssn_cliauto`: subscribe now uses `ssn_client_subscribe` instead of `ssn_client_message`
- `ssn_cliauto`: `ssn_client_connect` return type fixed from `int` to `bool`
- `ssn_cliauto.h`: `VSOA` references renamed to `SSN`, `VSOA_CLIENT_AUTO_MAX_PING_LOST` → `SSN_CLIENT_AUTO_MAX_PING_LOST`
- `README.md`: rewritten with current architecture diagram, API examples, full test table
- `CHANGELOG.md` / `VERSION`: added version tracking files
- Documentation: 10+ docs fully migrated from `ipc_`/`cd-ipc-ssn` to `ssn_`/`SSN` naming
- `ssn_version.h`: bumped to 2.2.0

### Fixed
- Client: `ssn_client_process_events` no longer disconnects on `EAGAIN`/`EWOULDBLOCK` (same pattern as server fix in v2.1.0)
- `ssn_frame.c`: log messages `ipc` → `ssn`
- `ssn_cliauto.c`: comments `IPC` → `SSN`, old file name references removed

### Removed
- `src/cd_ipc_client_refactored.c` — obsolete refactoring draft

### Test Results (8 suites, 116 tests)
- test_transport: 55/0 passed
- test_node_basic: 3/3 passed
- test_node: 5/5 passed
- test_protocol: 25/0 passed
- test_protocol_integration: 19/0 passed
- example_server: 4/4 passed
- example_client: 5/5 passed

## [2.1.0] - 2026-04-29

### Added
- Protocol layer receive-side implementation (RPC, PubSub, Message polling)
- Node-level background server poller support for tests
- Complete protocol integration test suite (19 tests)

### Changed
- **Unified naming**: all public types, functions, and macros migrated from `cd_ipc_`/`ipc_` prefix to `ssn_` prefix
  - `ipc_client_t` → `ssn_client_t`, `ipc_server_t` → `ssn_server_t`
  - `ipc_node_t` → `ssn_node_t`, `ipc_header_t` → `ssn_header_t`
  - `IPC_MAX_PACKET_SIZE` → `SSN_MAX_PACKET_SIZE`, etc.
- **Client/Server refactored**: integrated new protocol layer modules (ssn_rpc, ssn_pubsub, ssn_msg)
- `ssn_node_subscribe` now requires `peer_address` parameter for explicit connection targeting
- `ssn_client_set_on_message` now sets both `onmsg` and `onsub` callbacks
- `create_server_address` now includes proper protocol prefix (`tcp://`, `unix://`)
- Error code type renamed from `ssn_error_t` to `ssn_ecode_t` to avoid conflict with transport layer

### Fixed
- Deadlock: `ssn_node_get_client`/`ssn_node_get_server` removed redundant internal locking
- TCP/UDP transports: `get_option` now correctly returns socket fd (was always -1)
- Server: client connections no longer destroyed on `EAGAIN` from non-blocking recv
- Protocol send functions: replaced stack-allocated header with `SSN_MAX_PACKET_SIZE` buffer (fixes SIGSEGV)
- Protocol poll functions: implemented real receive+parse+dispatch logic (were empty stubs)
- `test_protocol_integration`: fixed NULL pointer dereference in callbacks, RPC response routing

### Test Results
- test_transport: 55/0 passed
- test_node_basic: 3/3 passed
- test_node: 5/5 passed
- test_protocol: 25/0 passed
- test_protocol_integration: 19/0 passed

## [2.0.0] - 2026-04-21

### Added
- Node discovery mechanism (multicast, directory service)
- QoS framework (reliability, priority, bandwidth control)
- Node abstraction layer (service registration, topic management)
- Multi-protocol support (Unix Socket, TCP, UDP)
- Example code and documentation

### Known Limitations
- TLS/DTLS secure transport not yet implemented

## [1.0.0] - 2026-04-19

### Added
- Transport layer abstraction (Unix Socket / TCP / UDP)
- Node abstraction layer Phase 1 (create, start, stop, destroy)
- Communication APIs (send message, publish/subscribe, RPC)
- Version management
- Unit tests

### Known Limitations
- Node discovery mechanism (Phase 2 planned)
- QoS support (Phase 3 planned)
- TLS/DTLS secure transport (future version)
