# 2.3.1 稳定化实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完成 2.3.1 稳定化：合并 PR #1、集中修复 8 项遗留 issue（3 批次）、补写 02/05 文档、发版 2.3.1（含 tag）。

**Architecture:** 按规格第 3 节分批执行——批次 A（正确性：node 自锁/Test 2 断言/IPv6 警告）、批次 B（清理：UDP 标注/死符号/内部标签/坏链）、批次 C（文档：02 需求分析 + 05 部署手册）。每批次独立分支 `fix/231-batch-<a|b|c>`，合并回 main 后切下一批次；最后在 main 上发版 2.3.1。

**Tech Stack:** C99、CMake、git、WSL（测试运行）。TDD：每项代码修复先写/确认失败再修复。

## Global Constraints

- 每批次一个分支：`fix/231-batch-a`、`fix/231-batch-b`、`fix/231-batch-c`（从最新 main 切出，合并后删除）
- 代码修复遵循 TDD：先写回归测试（或确认失败）→ 最小修复 → 全量验证
- 修改 .c/.h 保持仓库 CRLF 行尾（sed 后需 `perl -pi -e 's/\r?\n/\r\n/g' <file>` 恢复）；.sh 由 .gitattributes 强制 LF
- 版本号 2.3.1：发版前同步 `VERSION`、`src/version/ssn_version.h`、`CMakeLists.txt`（VERSION_PATCH 1）、`CHANGELOG.md`、白皮书版本历史表
- 全量验证门禁：`wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh"`（7 套件）+ `bash test/verify_examples.sh`（15 示例）
- 发版后打 tag：`git tag v2.3.1`

---

### Task 0: 合并 PR #1（feature/dds-doc → main）

**Files:**
- 无文件修改（git 操作）

**Interfaces:**
- Consumes: 无
- Produces: main 含 DDS 文档（后续所有批次分支从最新 main 切出）

- [ ] **Step 1: 合并 PR #1**

```bash
cd /d/personal/cd-ipc-ssn
git checkout main
git pull origin main
git merge origin/feature/dds-doc --no-edit
git push origin main
git branch -d feature/dds-doc
gh pr close 1 --comment "已合并至 main"
```

- [ ] **Step 2: 验证合并结果**

```bash
cd /d/personal/cd-ipc-ssn
git log --oneline -3          # 应含 8f12a9d（下一步规划规格）
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh 2>&1 | tail -2"   # 7 套件全过
```

---

### Task 1: 批次 A — node_destroy 自锁修复（issue ②）

**Files:**
- Modify: `src/node/ssn_node.c`（destroy 锁序，约 301-314 行）
- Test: `test/test_node.c`（新增回归用例）

**Interfaces:**
- Consumes: 无
- Produces: `ssn_node_destroy` 对 ACTIVE 节点安全（后续批次不依赖，但批次 A 合并为 2.3.1 正确性基线）

- [ ] **Step 1: 读代码确认锁序问题**

读 `src/node/ssn_node.c` 的 `ssn_node_destroy`（约 301-314 行）：确认其在持有 `node->lock`（非递归 pthread_mutex）时对 ACTIVE 节点调用 `ssn_node_stop`（stop 内部再次加锁 → 自锁挂死）。

- [ ] **Step 2: 写回归测试（TDD 红）**

在 `test/test_node.c` 中新增用例 `test_node_destroy_active`（仿照现有 `test_node_creation` 结构）：
1. `ssn_node_create` 创建节点（`node_type = "test"`、`node_name = "destroy-active"`）
2. `ssn_node_start` 启动（server 角色，监听 127.0.0.1 临时端口）
3. **不调用 stop，直接 `ssn_node_destroy`**
4. 断言：destroy 正常返回且进程不挂死

测试代码骨架（沿用 test_node.c 的 ASSERT 模式，含 `test_node_create_config()` 现有辅助函数）：

```c
static void test_node_destroy_active(void)
{
    ssn_node_config_t cfg;
    test_node_create_config(&cfg, "test", "destroy-active");
    ssn_node_t *node = ssn_node_create(&cfg);
    ASSERT(node != NULL, "Create node for destroy-active test");
    ASSERT(ssn_node_start(node), "Start node");

    /* 不调用 stop，直接 destroy：自锁时此处挂死，时间戳断言失败 */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    ssn_node_destroy(node);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    ASSERT(elapsed < 5.0, "Destroy active node completes without deadlock");
}
```

并在 `main()` 的用例列表中追加 `test_node_destroy_active` 调用（打印 `Test N: PASSED/FAILED` 的现有模式）。

**注意**：自锁是挂死而非崩溃——上述时间戳断言在挂死时 destroy 不返回、测试无法完成（用 `timeout 10 ./test_node` 运行可观察到进程被 kill，即红态证据）。

- [ ] **Step 3: 运行测试确认失败（红）**

```bash
cd /mnt/d/personal/cd-ipc-ssn/build && cmake .. >/dev/null && make test_node >/dev/null 2>&1
timeout 10 ./test_node 2>&1 | tail -5
```
预期：挂死（timeout 杀进程）或 destroy 用例断言失败。

- [ ] **Step 4: 最小修复**

修改 `ssn_node_destroy` 锁序：在调用 `ssn_node_stop` 前先释放 `node->lock`，stop 完成后再重新加锁继续清理（或按实际代码结构重构——原则：**destroy 路径内不得在持锁时调用会再次加锁的函数**）。保持其他行为不变。

- [ ] **Step 5: 运行测试确认通过（绿）**

```bash
cd /mnt/d/personal/cd-ipc-ssn/build && make test_node >/dev/null 2>&1 && timeout 30 ./test_node 2>&1 | tail -3
```
预期：全部用例通过（含新增 destroy-active 用例），进程正常退出。

- [ ] **Step 6: 恢复 CRLF（若用脚本处理过）并提交**

```bash
cd /d/personal/cd-ipc-ssn
perl -pi -e 's/\r?\n/\r\n/g' src/node/ssn_node.c test/test_node.c
git add src/node/ssn_node.c test/test_node.c
git commit -m "fix(node): ssn_node_destroy 修复 ACTIVE 节点持锁自锁（回归测试）"
```

---

### Task 2: 批次 A — error_handling/timeout 示例 Test 2 断言重写（issue ③）

**Files:**
- Modify: `examples/advanced/02_error_handling/error_handling.c`、`examples/advanced/03_timeout/timeout.c`

**Interfaces:**
- Consumes: 无
- Produces: 两示例超时测试断言可成立（示例可运行验证）

- [ ] **Step 1: 读示例确认现状**

读两个文件的 Test 2（超时场景）：确认当前用 `ssn_client_call` 的返回值（send 成功即 0）判断「Expected to fail」——send 成功不等于调用失败，断言不成立。

- [ ] **Step 2: 重写 Test 2 判断逻辑**

改为**基于回调结果**判断：调用 `ssn_client_call` 时传回调；回调中 `ipc_hdr == NULL`（服务器未响应/超时）即视为「预期失败成立」。重写后结构：

```c
// Test 2 期望逻辑（以 error_handling.c 为例，timeout.c 同构）：
static bool expect_fail_observed;   // 回调中置位

static void expect_fail_cb(ssn_client_t *client, ssn_header_t *hdr,
                           ssn_data_ref_t *data, void *arg) {
    (void)client; (void)data; (void)arg;
    if (hdr == NULL) { expect_fail_observed = true; }   // 超时 = 预期失败成立
}

// Test 2 中：
expect_fail_observed = false;
ssn_client_call(client, &url, &req, expect_fail_cb, NULL, 200);  // 短超时
ssn_client_poll(client, 500);                                    // 驱动回调
// 断言：expect_fail_observed == true（若为 false 打印 "Expected to fail, but succeeded" 并判失败）
```

- [ ] **Step 3: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
cd examples/advanced/02_error_handling && make clean >/dev/null && make >/dev/null 2>&1 && echo "02 构建 OK"
cd ../03_timeout && make clean >/dev/null && make >/dev/null 2>&1 && echo "03 构建 OK"
```
（示例为自包含测试程序，运行输出应含全部 Test 通过——如需运行：`./error_handling` 与 `./timeout` 需各自服务端，按示例 README 说明验证。）

- [ ] **Step 4: 提交**

```bash
cd /d/personal/cd-ipc-ssn
perl -pi -e 's/\r?\n/\r\n/g' examples/advanced/02_error_handling/error_handling.c examples/advanced/03_timeout/timeout.c
git add examples/advanced/02_error_handling/error_handling.c examples/advanced/03_timeout/timeout.c
git commit -m "fix(examples): error_handling/timeout Test 2 改为基于回调判断超时"
```

---

### Task 3: 批次 A — IPv6 snprintf 截断警告修复（issue ④）

**Files:**
- Modify: `src/transports/ssn_transport.c`（169/205 行附近）

**Interfaces:**
- Consumes: 无
- Produces: 构建无 `-Wformat-truncation` 警告

- [ ] **Step 1: 读代码确认**

读 `src/transports/ssn_transport.c` 的 `ssn_address_parse` 中两处 `snprintf(addr->address_str, SSN_TRANSPORT_MAX_ADDRESS_LEN, "tcp6://[%s]:%d", ...)`（约 169 行）与 `"udp6://[%s]:%d"`（约 205 行）——`host` 最长 255 字符导致 `-Wformat-truncation`。

- [ ] **Step 2: 修复（消除截断警告）**

方案：snprintf 后检查返回值 `>= SSN_TRANSPORT_MAX_ADDRESS_LEN` 时视为地址过长错误（`LOG_ERROR` + 返回 false），不截断使用不完整地址：

```c
int n = snprintf(addr->address_str, SSN_TRANSPORT_MAX_ADDRESS_LEN,
                 "tcp6://[%s]:%d", host, port);
if (n < 0 || (size_t)n >= SSN_TRANSPORT_MAX_ADDRESS_LEN) {
    LOG_ERROR("address too long: tcp6://[%s]:%d", host, port);
    return false;
}
```
udp6 同构。同时检查同函数中 tcp/udp 非 v6 的 snprintf 是否有相同模式并一并处理。

- [ ] **Step 3: 验证（警告消失）**

```bash
cd /mnt/d/personal/cd-ipc-ssn && rm -rf build && mkdir build && cd build
cmake .. >/dev/null && make 2>&1 | grep -c "format-truncation"   # 预期 0
```

- [ ] **Step 4: 恢复 CRLF 并提交**

```bash
cd /d/personal/cd-ipc-ssn
perl -pi -e 's/\r?\n/\r\n/g' src/transports/ssn_transport.c
git add src/transports/ssn_transport.c
git commit -m "fix(transport): IPv6 地址 snprintf 截断警告（超长地址返回错误）"
```

---

### Task 4: 批次 A 合并 + 批次 B 分支（Task 1-3 完成后）

**Files:**
- 无文件修改（git 操作）

**Interfaces:**
- Consumes: Task 1-3 提交
- Produces: main 含批次 A；`fix/231-batch-b` 从最新 main 切出

- [ ] **Step 1: 批次 A 全量验证**

```bash
cd /d/personal/cd-ipc-ssn
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh 2>&1 | tail -2"   # 7 套件
wsl bash /mnt/d/personal/cd-ipc-ssn/test/verify_examples.sh 2>&1 | tail -1             # 15 示例
```

- [ ] **Step 2: 合并批次 A 并切批次 B**

```bash
cd /d/personal/cd-ipc-ssn
git checkout main && git merge fix/231-batch-a --no-edit
git branch -d fix/231-batch-a
git checkout -b fix/231-batch-b
```

---

### Task 5: 批次 B — UDP 服务端握手限制评估与标注（issue ①）

**Files:**
- Modify: `src/transports/ssn_transport_udp.c`（头注释）、`docs/03-设计/架构设计/传输层设计.md`（UDP 限制标注）、`examples/protocols/03_udp/README.md`

**Interfaces:**
- Consumes: 无
- Produces: UDP 限制声明（框架行为边界明确）

- [ ] **Step 1: 确认限制本质**

读 `src/transports/ssn_transport_udp.c` 的 `udp_transport_accept`（约 285 行，返回「UDP does not support accept」）及 `ssn_server` 对 UDP 的处理路径，确认：UDP 传输层不支持 server 模式连接管理（无 accept），`ssn_server_start` 在 UDP 上不可用。

- [ ] **Step 2: 标注三处**

1. `ssn_transport_udp.c` 文件头注释追加：`/* 限制：UDP 为无连接传输，不支持 accept/server 模式握手；仅适用于对等/客户端模式收发 */`
2. `docs/03-设计/架构设计/传输层设计.md` 的 UDP 适配器小节标注：`> ⚠️ 限制（2026-08-06）：UDP 不支持 server 模式握手（无 accept），ssn_server 无法运行于 UDP 之上；适用于 client 模式点对点收发`
3. `examples/protocols/03_udp/README.md` 开头加说明：示例演示 UDP 传输的客户端收发能力；**UDP 服务端握手不受支持**（框架限制），示例的 server 端仅为演示 bind/recv 流程

- [ ] **Step 3: 验证**

```bash
cd /d/personal/cd-ipc-ssn
grep -c "UDP 不支持 server 模式握手\|UDP does not support accept" src/transports/ssn_transport_udp.c docs/03-设计/架构设计/传输层设计.md examples/protocols/03_udp/README.md   # ≥3
wsl bash /mnt/d/personal/cd-ipc-ssn/test/verify_examples.sh 2>&1 | tail -1   # 15 示例（03_udp 构建通过）
```

- [ ] **Step 4: 恢复 CRLF 并提交**

```bash
cd /d/personal/cd-ipc-ssn
perl -pi -e 's/\r?\n/\r\n/g' src/transports/ssn_transport_udp.c docs/03-设计/架构设计/传输层设计.md examples/protocols/03_udp/README.md
git add src/transports/ssn_transport_udp.c docs/03-设计/架构设计/传输层设计.md examples/protocols/03_udp/README.md
git commit -m "docs(udp): 标注 UDP 不支持 server 模式握手限制（源码注释/设计文档/示例 README）"
```

---

### Task 6: 批次 B — 死符号与内部标签清理（issue ⑤⑥）

**Files:**
- Modify: `src/ssn_error.c`（删 `ssn_ecode_version`）、`src/node/ssn_node.h`（113 行标签）、`src/node/ssn_node.c`（struct 定义处）

**Interfaces:**
- Consumes: 无
- Produces: 无死符号、内部命名统一

- [ ] **Step 1: 删除死符号（⑤）**

`src/ssn_error.c` 中删除 `ssn_ecode_version` 函数（约 27-29 行，全库零引用、无头文件声明）及其上方注释块。

- [ ] **Step 2: 内部标签改名（⑥）**

`src/node/ssn_node.h` 约 113 行 `typedef struct ipc_node {` → `typedef struct ssn_node {`；`src/node/ssn_node.c` 中对应 `struct ipc_node {` 定义处同步改名（grep `ipc_node` 确认全部出现点，仅改 struct 标签；类型名 `ssn_node_t` 不变）。

- [ ] **Step 3: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
grep -rn "ssn_ecode_version\|struct ipc_node" src/ | wc -l   # 预期 0
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh 2>&1 | tail -2"   # 7 套件全过
```

- [ ] **Step 4: 恢复 CRLF 并提交**

```bash
cd /d/personal/cd-ipc-ssn
perl -pi -e 's/\r?\n/\r\n/g' src/ssn_error.c src/node/ssn_node.h src/node/ssn_node.c
git add src/ssn_error.c src/node/ssn_node.h src/node/ssn_node.c
git commit -m "cleanup: 删除无引用死符号 ssn_ecode_version；内部 struct 标签统一 ssn_node"
```

---

### Task 7: 批次 B — superpowers 文档坏链修复（issue ⑧）

**Files:**
- Modify: `docs/superpowers/plans/2026-08-02-docs-refactor.md`、`docs/superpowers/specs/2026-08-02-docs-refactor-design.md`（及检查出的其他坏链文档）

**Interfaces:**
- Consumes: 无
- Produces: superpowers 文档链接全部有效

- [ ] **Step 1: 定位全部坏链**

```bash
cd /mnt/d/personal/cd-ipc-ssn
# 遍历 docs/superpowers 下 .md 的相对链接，检查目标存在（从链接所在文件目录解析）
grep -rn "](\.\./[^)]*\.md\|]([^)#]*\.md)" docs/superpowers --include="*.md" | grep -v "superpowers" | head -30
```
逐一判断：相对路径解析后目标不存在的即为坏链（已知约 25 个，集中在 plans/2026-08-02-docs-refactor.md——其链接按 docs/ 根相对路径书写但实际在 plans/ 子目录）。

- [ ] **Step 2: 修正链接**

将所有坏链改为从链接所在文件位置正确解析的相对路径（如 plans/ 下文档指向 `docs/03-设计/...` 的链接改为 `../../03-设计/...`）。只改路径，不改链接文字。

- [ ] **Step 3: 验证**

```bash
cd /mnt/d/personal/cd-ipc-ssn
# 重跑 Step 1 的检查命令，预期无坏链输出（或全部链接目标存在）
```

- [ ] **Step 4: 恢复 CRLF 并提交**

```bash
cd /d/personal/cd-ipc-ssn
perl -pi -e 's/\r?\n/\r\n/g' docs/superpowers/plans/*.md docs/superpowers/specs/*.md
git add docs/superpowers/
git commit -m "docs(superpowers): 修复规格/计划文档相对链接（25 处）"
```

---

### Task 8: 批次 B 合并 + 批次 C 分支（Task 5-7 完成后）

**Files:**
- 无文件修改（git 操作）

**Interfaces:**
- Consumes: Task 5-7 提交
- Produces: main 含批次 B；`fix/231-batch-c` 从最新 main 切出

- [ ] **Step 1: 批次 B 全量验证**

```bash
cd /d/personal/cd-ipc-ssn
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh 2>&1 | tail -2"
wsl bash /mnt/d/personal/cd-ipc-ssn/test/verify_examples.sh 2>&1 | tail -1
```

- [ ] **Step 2: 合并批次 B 并切批次 C**

```bash
cd /d/personal/cd-ipc-ssn
git checkout main && git merge fix/231-batch-b --no-edit
git branch -d fix/231-batch-b
git checkout -b fix/231-batch-c
```

---

### Task 9: 批次 C — 02 需求分析 / 05 部署手册补写（issue ⑦）

**Files:**
- Create: `docs/02-需求分析/需求分析.md`、`docs/05-部署手册/部署手册.md`
- Modify: `docs/README.md`（总索引 02/05 节点由「待补写」改为真实链接）

**Interfaces:**
- Consumes: 现有文档体系（白皮书/设计/使用指南为内容依据）
- Produces: 完整需求分析与部署手册

- [ ] **Step 1: 撰写需求分析（4 章）**

`docs/02-需求分析/需求分析.md`：
1. **框架定位与目标**：SSN 为轻量级 IPC/分布式通信框架（v2.3.1）；目标场景（边缘计算、进程间通信、设备互联）；设计原则（分层、轻量、中文文档）
2. **功能需求**：逐项列出（每项含：需求编号 FR-xx、描述、优先级 P0/P1）：
   - FR-01 节点抽象（server/client 双角色、生命周期、统计）
   - FR-02 RPC 请求/应答（方法注册、超时、回调）
   - FR-03 发布/订阅（主题订阅、发布分发、退订）
   - FR-04 点对点消息（定向收发）
   - FR-05 多传输（Unix/TCP/UDP、工厂、地址解析）
   - FR-06 节点发现（组播/目录服务、心跳）
   - FR-07 QoS 基础（可靠性/优先级/带宽配置模型）
   - FR-08 自动重连客户端（ssn_cliauto 状态机）
   - FR-09 版本管理与错误码体系（SSN_ECODE_*）
3. **非功能需求**：NFR-01 性能（无锁/低拷贝路径、批量处理）；NFR-02 线程安全（引用计数、细粒度锁、回调线程安全）；NFR-03 可移植性（POSIX、C99）；NFR-04 可维护性（命名规范、文档体系、TDD 流程）
4. **DDS 对标需求映射**：三阶段对应需求（阶段 1：DR-01 域概念/DR-02 主题抽象/DR-03 发布者订阅者抽象；阶段 2：DR-04 QoS 策略子集；阶段 3：DR-05 发现增强/DR-06 内容过滤），标注目标版本

- [ ] **Step 2: 撰写部署手册（4 章）**

`docs/05-部署手册/部署手册.md`：
1. **环境要求**：Linux/POSIX、CMake ≥ 3.12、GCC ≥ 4.8（Clang ≥ 3.0）、pthread；Windows 用户使用 WSL
2. **构建**：库构建（cmake + make，产物 libssn_transport.so）、测试构建与运行（run_tests.sh）、示例构建（verify_examples.sh）、示例运行（rpath 已内置，可直接执行）
3. **安装与集成**：`cmake --install`（lib/include）、头文件清单（ssn_client.h/ssn_server.h/ssn_node.h/ssn_transport.h 等）、链接 `-lssn_transport -lpthread`、集成步骤（含第一个应用示例引用使用指南）
4. **常见问题**：运行时找不到 .so（rpath/LD_LIBRARY_PATH）、WSL 构建路径、UDP server 握手限制（指向传输层设计文档）

- [ ] **Step 3: 更新总索引**

`docs/README.md`：02 节点「待补写（见 README）」改为 `[需求分析](02-需求分析/需求分析.md)`；05 节点同样改部署手册链接；删除两个占位 README.md 中不再需要的「待补写」说明（或保留占位文件但改为指向正文）。

- [ ] **Step 4: 恢复 CRLF 并验证**

```bash
cd /d/personal/cd-ipc-ssn
perl -pi -e 's/\r?\n/\r\n/g' docs/02-需求分析/需求分析.md docs/05-部署手册/部署手册.md docs/README.md
grep -c "^## " docs/02-需求分析/需求分析.md          # 预期 4
grep -c "^## " docs/05-部署手册/部署手册.md          # 预期 4
grep -c "需求分析" docs/README.md                     # ≥1（索引已更新）
# 链接检查：docs/README.md 中 02/05 链接目标存在
[ -f docs/02-需求分析/需求分析.md ] && [ -f docs/05-部署手册/部署手册.md ] && echo "OK"
```

- [ ] **Step 5: 提交**

```bash
cd /d/personal/cd-ipc-ssn
git add docs/02-需求分析/ docs/05-部署手册/ docs/README.md
git commit -m "docs: 补写需求分析与部署手册（完整规格）并更新总索引"
```

---

### Task 10: 批次 C 合并 + 发版 2.3.1

**Files:**
- Modify: `VERSION`、`src/version/ssn_version.h`、`CMakeLists.txt`、`CHANGELOG.md`、`docs/01-白皮书/架构白皮书.md`（版本历史表）

**Interfaces:**
- Consumes: Task 0-9 全部交付
- Produces: 2.3.1 发布版本 + tag

- [ ] **Step 1: 批次 C 验证与合并**

```bash
cd /d/personal/cd-ipc-ssn
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh 2>&1 | tail -2"
git checkout main && git merge fix/231-batch-c --no-edit
git branch -d fix/231-batch-c
```

- [ ] **Step 2: 同步版本号（4 处 + 白皮书）**

1. `VERSION`：`2.3.1`
2. `src/version/ssn_version.h`：`SSN_VERSION_PATCH 0` → `1`、`SSN_VERSION_STRING "2.3.0"` → `"2.3.1"`
3. `CMakeLists.txt`：`set(VERSION_PATCH 0)` → `1`（VERSION_STRING 由变量拼接自动更新）
4. `CHANGELOG.md` 顶部新增：

```markdown
## [2.3.1] - 2026-08-06

### Fixed
- `ssn_node_destroy` 对 ACTIVE 节点直接销毁时的持锁自锁（回归测试）
- IPv6 地址 snprintf 截断警告（超长地址返回错误）
- examples：error_handling/timeout 超时测试断言改为基于回调结果
- UDP 传输标注「不支持 server 模式握手」限制（源码/设计文档/示例 README）

### Changed
- 删除无引用死符号 `ssn_ecode_version`；`struct ipc_node` 内部标签统一为 `ssn_node`
- 补写 02 需求分析（含 DDS 对标需求映射）与 05 部署手册；修复 superpowers 文档坏链
```

5. `docs/01-白皮书/架构白皮书.md` 版本历史表追加：`| 2.3.1 | 2026-08-06 | 稳定化：node 自锁/IPv6 警告修复、UDP 限制标注、需求分析与部署手册补写 |`

- [ ] **Step 3: 全量验证门禁**

```bash
cd /d/personal/cd-ipc-ssn && rm -rf build && mkdir build && cd build && cmake .. >/dev/null && make -j4 2>&1 | grep -c "format-truncation"   # 预期 0
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh 2>&1 | tail -2"   # 7 套件
wsl bash /mnt/d/personal/cd-ipc-ssn/test/verify_examples.sh 2>&1 | tail -1             # 15 示例
grep -c "2.3.1" VERSION src/version/ssn_version.h CMakeLists.txt CHANGELOG.md          # ≥4
```

- [ ] **Step 4: 发版提交与打 tag**

```bash
cd /d/personal/cd-ipc-ssn
git add VERSION src/version/ssn_version.h CMakeLists.txt CHANGELOG.md docs/01-白皮书/架构白皮书.md
git commit -m "release: 2.3.1（稳定化修复批次 A/B/C）"
git tag v2.3.1
git push origin main --tags
```

---

## Self-Review（实施前确认清单）

- [ ] 规格第 3 节 8 项 issue ↔ Task 1-9（②Task1、③Task2、④Task3、①Task5、⑤⑥Task6、⑧Task7、⑦Task9）
- [ ] 规格批次分组（A:②③④、B:①⑤⑥⑧、C:⑦）↔ Task 1-3/5-7/9 + Task 4/8/10 的分支切换
- [ ] 规格第 5 节发版细节 ↔ Task 10（4 处版本同步 + 白皮书 + tag）
- [ ] 规格第 6 节交付范围 ↔ Task 0-10（含合并 PR #1）
- [ ] 全局约束（CRLF、TDD、全量验证门禁）↔ 各任务步骤
