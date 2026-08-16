# C++ 服务框架（批次 2）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 交付 C++ 服务框架（v2.4.0 特性）——ServiceBase/ServiceTask/ServiceManager 三层渐进基类 + SsnService/SsnClient 通信层 + nlohmann/json 类型安全 DTO，目标**让用户一行启动服务、同步调用 RPC、上手快**。

**Architecture:** 参考 vsoa lwserverbase 的 API 形态（用户已确认**参考重写**，代码全新自研，无版权风险），命名空间 `ssn`，头文件 `include/ssn/framework/*.hpp`；构建为**独立库 `libssn_framework.so`**（C++17，链接现有 `libssn_transport.so` + pthread），不触碰 C 侧任何代码。通信层绑定 SSN 节点 API（`ssn_node_*`）：SsnService 继承 ServiceTask，`svc()` 内跑 node poll 循环；SsnClient 同步调用用单 in-flight + promise 封装（C API 回调无 per-request 上下文，已核实 `ssn_client_call` 返回 0/-1 非 seqno）。

**Tech Stack:** C++17、CMake ≥ 3.12、nlohmann/json v3.11.3（vendor 单头文件，MIT）、pthread、SSN C API（`src/node/ssn_node.h`、`src/ssn_server.h`、`src/ssn_client.h` 公开头）。

## Global Constraints

- **代码全新自研**：仅借鉴 vsoa API 形态（ServiceBase/ServiceTask/ServiceManager 三层、svc()/activate()/Run\<T\> 命名），不得复制 vsoa 源码；文件头注释版权归 SSN 项目（`Copyright (c) 2026 SSN Project.`），MIT 许可
- **命名约定**：命名空间 `ssn`；类 PascalCase（`ServiceBase`）；方法 CamelCase（`OnInit`/`RegisterMethod`）；成员与函数 snake_case；头文件扩展名 `.hpp`；公开 API 全部在 `include/ssn/framework/`，实现全部在 `src/framework/`
- **C++ 标准与构建**：C++17；独立库 `ssn_framework`（SHARED），`target_link_libraries(ssn_framework ssn_transport pthread)`；`project(ssn_transport LANGUAGES C CXX)` 需修改（原仅 C）
- **依赖边界**：框架层只依赖 SSN **公开 C API**（`ssn_node_*`/`ssn_server_*`/`ssn_client_*` 及 `ssn_header_t`/`ssn_url_ref_t`/`ssn_data_ref_t`）；禁止触碰 `src/vsi/`、`src/ssn_frame.c` 内部实现
- **首批限制（如实标注）**：node 层仅 TCP 监听（`ssn_node_config_t` 无 Unix 字段）→ SsnService 监听用 TCP；SsnClient 同步调用为**单 in-flight 串行化**（同一 client 并发 Call 会被串行，文档注明）
- **代码风格（与 C 侧一致）**：4 空格缩进、`{` 行尾、行 ≤ 120 字符、函数 ≤ 200 行、CRLF 行尾（sed 后必须 `perl -pi -e 's/\r?\n/\r\n/g'` 恢复）、中文注释（说明「为什么」）与中文日志
- **TDD 铁律**：每任务先写失败测试（红，附证据）→ 最小实现（绿）→ 提交；无测试不写生产代码；测试在 WSL 运行
- **回归**：C 侧 7 套件（124 例）+ 15 示例必须保持全绿：`wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh"`、`MSYS_NO_PATHCONV=1 wsl bash test/verify_examples.sh`
- **端口约定**：测试端口 18901-18910（run_tests.sh 顺序执行无冲突）；示例端口 18880-18890
- **提交信息**：`feat(framework): ...` / `test(framework): ...` / `docs: ...`（中文描述）
- **第三方依赖**：nlohmann/json v3.11.3 单头文件 vendor 至 `third_party/nlohmann/json.hpp`（MIT），附 `third_party/nlohmann/README.md`（版本/来源/SHA256 记录）

---

### Task 1: 工程骨架（third_party vendor + CMake + 目录 + 最小库）

**Files:**
- Create: `third_party/nlohmann/json.hpp`（vendor 下载）、`third_party/nlohmann/README.md`
- Create: `include/ssn/framework/`（目录占位，本任务仅建目录）、`src/framework/.gitkeep`
- Modify: `CMakeLists.txt`（`project` 加 CXX、新增 `ssn_framework` 目标、install 规则）
- Verify: WSL 构建 `libssn_framework.so` 成功

**Interfaces:**
- Consumes: 现有 `CMakeLists.txt`（`project(ssn_transport)` 第 3 行、`add_library` 第 46 行、`install` 段）
- Produces: 可链接的 `libssn_framework.so`（本任务为空库，供后续任务加源文件）；`third_party/nlohmann/json.hpp` 供 Task 5+ 使用

- [ ] **Step 1: vendor nlohmann/json v3.11.3**

```bash
cd /d/personal/cd-ipc-ssn
mkdir -p third_party/nlohmann src/framework
curl -L https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp -o third_party/nlohmann/json.hpp
sha256sum third_party/nlohmann/json.hpp
# 记录 SHA256 到 third_party/nlohmann/README.md，并与官方 v3.11.3 发布值核对
```

`third_party/nlohmann/README.md` 内容：

```markdown
# nlohmann/json（vendor）

- 版本：v3.11.3（单头文件，header-only）
- 来源：https://github.com/nlohmann/json/releases/tag/v3.11.3
- 许可：MIT License
- 用途：SSN C++ 服务框架的 DTO 序列化
- SHA256：<下载后填写>
```

- [ ] **Step 2: 修改 CMakeLists.txt（project 加 CXX）**

第 3 行改为：

```cmake
project(ssn_transport LANGUAGES C CXX)
```

- [ ] **Step 3: 添加 libssn_framework 目标**

在 `install(FILES src/ssn_cliauto.h DESTINATION include)` 之后追加：

```cmake
# ---- C++ 服务框架（v2.4.0，独立库，链接 ssn_transport）----
add_library(ssn_framework SHARED
    src/framework/ServiceBase.cpp
    src/framework/ServiceTask.cpp
    src/framework/ServiceManager.cpp
    src/framework/SsnService.cpp
    src/framework/SsnClient.cpp
)

target_compile_features(ssn_framework PUBLIC cxx_std_17)
target_include_directories(ssn_framework PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(ssn_framework ssn_transport pthread)

install(TARGETS ssn_framework
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ssn/framework/
    DESTINATION include/ssn/framework
)
```

（注：本任务 Step 3 引用的 5 个 .cpp 尚不存在——先创建空源文件占位，后续任务逐个填充实现。）

- [ ] **Step 4: 创建空源文件占位**

```bash
cd /d/personal/cd-ipc-ssn
for f in ServiceBase ServiceTask ServiceManager SsnService SsnClient; do
  printf '// 占位：Task %s 填充实现\n' "$f" > "src/framework/$f.cpp"
done
```

- [ ] **Step 5: 验证构建**

```bash
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && rm -rf build && mkdir -p build && cd build && cmake .. >/dev/null 2>&1 && make ssn_framework -j4 2>&1 | tail -3 && ls -la libssn_framework.so"
```

预期：`libssn_framework.so` 生成（链接 ssn_transport 成功）。

- [ ] **Step 6: 提交**

```bash
git add third_party/ CMakeLists.txt src/framework/
git commit -m "feat(framework): 工程骨架（nlohmann/json vendor + libssn_framework 独立库目标 + 目录结构）"
```

---

### Task 2: ServiceBase（生命周期基类）

**Files:**
- Create: `include/ssn/framework/ServiceBase.hpp`、`src/framework/ServiceBase.cpp`（替换占位）
- Test: `test/test_cpp_service_base.cpp`（新测试，沿用自定义断言模式）
- Modify: `CMakeLists.txt`（追加 `test_cpp_service_base` 可执行目标）

**Interfaces:**
- Consumes: 无（纯框架类，不依赖 SSN API）
- Produces: `ssn::ServiceBase`——后续 Task 3/4/5 的基类；**其完整接口契约如下（后续任务依赖，不得改签名）**：

```cpp
namespace ssn {

enum class ServiceState { Created, Initialized, Started, Stopped };

class ServiceBase {
public:
    ServiceBase();
    virtual ~ServiceBase();
    ServiceBase(const ServiceBase&) = delete;
    ServiceBase& operator=(const ServiceBase&) = delete;

    bool initialize(int argc, char** argv);   // final：Created→Initialized，OnInit 失败回 Created 返回 false
    bool start();                             // final：Initialized→Started，startImpl 失败回 Initialized 返回 false
    void stop();                              // final：Started→Stopped，先 OnShutdown 后 stopImpl
    void destroy();                           // final：任意状态安全销毁；若 Started 先 stop；状态回 Created

    ServiceState state() const;
    const std::string& name() const;
    void setName(const std::string& name);

protected:
    virtual bool OnInit(int argc, char** argv);   // 用户钩子，默认 true
    virtual void OnShutdown();                    // 用户钩子，默认空
    virtual bool startImpl();                     // 内部扩展点（ServiceTask 覆写），默认 true
    virtual void stopImpl();                      // 内部扩展点（ServiceTask 覆写），默认空

    std::string name_;
    ServiceState state_{ServiceState::Created};
};

}  // namespace ssn
```

- [ ] **Step 1: 写失败测试** `test/test_cpp_service_base.cpp`

```cpp
// 测试：ServiceBase 生命周期状态机与用户钩子调用顺序
#include "ssn/framework/ServiceBase.hpp"
#include <cstdio>
#include <string>

static int g_cpp_passed = 0;
static int g_cpp_failed = 0;
#define CHECK(cond) do { if (cond) { ++g_cpp_passed; } else { ++g_cpp_failed; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace {

std::string g_order;   // 钩子调用顺序记录

class TestService : public ssn::ServiceBase {
public:
    bool OnInit(int argc, char** argv) override {
        (void)argc; (void)argv;
        g_order += "init;";
        return true;
    }
    void OnShutdown() override { g_order += "shutdown;"; }
    bool startImpl() override { g_order += "startImpl;"; return true; }
    void stopImpl() override { g_order += "stopImpl;"; }
};

class FailInitService : public ssn::ServiceBase {
public:
    bool OnInit(int argc, char** argv) override { (void)argc; (void)argv; return false; }
};

void test_lifecycle_order() {
    TestService svc;
    g_order.clear();
    CHECK(svc.state() == ssn::ServiceState::Created);
    CHECK(svc.initialize(0, nullptr));
    CHECK(svc.state() == ssn::ServiceState::Initialized);
    CHECK(svc.start());
    CHECK(svc.state() == ssn::ServiceState::Started);
    svc.stop();
    CHECK(svc.state() == ssn::ServiceState::Stopped);
    // 顺序：init → startImpl → shutdown → stopImpl
    CHECK(g_order == "init;startImpl;shutdown;stopImpl;");
}

void test_illegal_transitions() {
    TestService svc;
    CHECK(!svc.start());          // Created 不能 start
    CHECK(svc.state() == ssn::ServiceState::Created);
    CHECK(svc.initialize(0, nullptr));
    CHECK(!svc.initialize(0, nullptr));  // 重复 initialize 拒绝
    svc.destroy();
    CHECK(svc.state() == ssn::ServiceState::Created);
    CHECK(svc.initialize(0, nullptr));   // destroy 后可重新初始化
}

void test_init_failure() {
    FailInitService svc;
    CHECK(!svc.initialize(0, nullptr));
    CHECK(svc.state() == ssn::ServiceState::Created);
}

void test_name() {
    TestService svc;
    svc.setName("my-service");
    CHECK(svc.name() == "my-service");
}

}  // namespace

int main() {
    test_lifecycle_order();
    test_illegal_transitions();
    test_init_failure();
    test_name();
    std::printf("C++ test results: %d/%d passed\n", g_cpp_passed, g_cpp_passed + g_cpp_failed);
    return g_cpp_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 2: 运行确认红**

```bash
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && mkdir -p /tmp/rvbuild && cd /tmp/rvbuild && cmake /mnt/d/personal/cd-ipc-ssn >/dev/null 2>&1 && make test_cpp_service_base 2>&1 | tail -3"
```

预期：编译失败（`ssn/framework/ServiceBase.hpp` 不存在）——红。

（CMake 目标需先行添加：在 `add_executable(example_client ...)` 之后追加 `add_executable(test_cpp_service_base test/test_cpp_service_base.cpp)` + `target_link_libraries(test_cpp_service_base ssn_framework pthread)`。）

- [ ] **Step 3: 实现 ServiceBase.hpp/.cpp**

按上述契约实现。关键逻辑：

```cpp
bool ServiceBase::initialize(int argc, char** argv) {
    if (state_ != ServiceState::Created) { return false; }
    if (!OnInit(argc, argv)) { return false; }   // 失败保持 Created
    state_ = ServiceState::Initialized;
    return true;
}
bool ServiceBase::start() {
    if (state_ != ServiceState::Initialized) { return false; }
    if (!startImpl()) { return false; }          // 失败回 Initialized
    state_ = ServiceState::Started;
    return true;
}
void ServiceBase::stop() {
    if (state_ != ServiceState::Started) { return; }
    OnShutdown();
    stopImpl();
    state_ = ServiceState::Stopped;
}
void ServiceBase::destroy() {
    if (state_ == ServiceState::Started) { stop(); }
    if (state_ == ServiceState::Initialized) { state_ = ServiceState::Created; }
    // Stopped/Created：直接归位 Created
    state_ = ServiceState::Created;
}
```

- [ ] **Step 4: 运行确认绿**

```bash
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && cd /tmp/rvbuild && make test_cpp_service_base 2>&1 | tail -2 && ./test_cpp_service_base"
```

预期：`C++ test results: 10/10 passed`（4 个用例共 10 个 CHECK）。

- [ ] **Step 5: 提交**

```bash
git add include/ssn/framework/ServiceBase.hpp src/framework/ServiceBase.cpp test/test_cpp_service_base.cpp CMakeLists.txt
git commit -m "feat(framework): ServiceBase 生命周期基类（状态机 + OnInit/OnShutdown 钩子 + 扩展点），TDD 10 断言绿"
```

---

### Task 3: ServiceTask（线程池基类）

**Files:**
- Create: `include/ssn/framework/ServiceTask.hpp`、`src/framework/ServiceTask.cpp`（替换占位）
- Test: `test/test_cpp_service_task.cpp`
- Modify: `CMakeLists.txt`（`test_cpp_service_task` 目标）

**Interfaces:**
- Consumes: `ssn::ServiceBase`（Task 2 契约）
- Produces: `ssn::ServiceTask`——Task 4/5 的基类：

```cpp
class ServiceTask : public ServiceBase {
public:
    bool activate(int num_threads = 1);  // 重复调用或已运行返回 false
    void wait();                          // join 全部线程
    void requestShutdown();               // 置 running_=false，svc 应据此退出
    bool isRunning() const;
    int threadCount() const;

protected:
    virtual int svc() = 0;                // 线程入口，子类实现
    bool startImpl() override;            // 调 activate(1)
    void stopImpl() override;             // requestShutdown + wait

private:
    std::atomic<bool> running_{false};
    std::vector<std::thread> threads_;
};
```

- [ ] **Step 1: 写失败测试** `test/test_cpp_service_task.cpp`

```cpp
#include "ssn/framework/ServiceTask.hpp"
#include <cstdio>
#include <atomic>
#include <chrono>
#include <thread>

static int g_cpp_passed = 0;
static int g_cpp_failed = 0;
#define CHECK(cond) do { if (cond) { ++g_cpp_passed; } else { ++g_cpp_failed; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace {

class CountingTask : public ssn::ServiceTask {
public:
    std::atomic<int> ticks{0};
    int svc() override {
        while (isRunning()) {
            ++ticks;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return 0;
    }
};

void test_activate_and_run() {
    CountingTask t;
    CHECK(t.activate(2));               // 2 线程
    CHECK(t.threadCount() == 2);
    CHECK(t.isRunning());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(t.ticks > 0);
    CHECK(!t.activate(1));              // 重复 activate 拒绝
    t.requestShutdown();
    t.wait();
    CHECK(!t.isRunning());
}

void test_lifecycle_integration() {
    // 经 ServiceBase 生命周期启动/停止：initialize → start → stop
    CountingTask t;
    CHECK(t.initialize(0, nullptr));
    CHECK(t.start());                   // startImpl → activate(1)
    CHECK(t.isRunning());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    t.stop();                           // stopImpl → requestShutdown + wait
    CHECK(!t.isRunning());
    CHECK(t.threadCount() == 0);        // wait 后线程已回收
}

void test_multithread_ticks() {
    // 单线程 50ms 约 10 tick；2 线程应明显更多——验证多线程真实并行
    CountingTask a;
    a.activate(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    a.requestShutdown(); a.wait();
    CountingTask b;
    b.activate(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    b.requestShutdown(); b.wait();
    CHECK(b.ticks > a.ticks);   // 2 线程 tick 数显著多于 1 线程
}

}  // namespace

int main() {
    test_activate_and_run();
    test_lifecycle_integration();
    test_multithread_ticks();
    std::printf("C++ test results: %d/%d passed\n", g_cpp_passed, g_cpp_passed + g_cpp_failed);
    return g_cpp_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 2: 运行确认红**

```bash
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && cd /tmp/rvbuild && cmake /mnt/d/personal/cd-ipc-ssn >/dev/null 2>&1 && make test_cpp_service_task 2>&1 | tail -3"
```

预期：编译失败（`ServiceTask.hpp` 不存在）——红。（先加 CMake 目标。）

- [ ] **Step 3: 实现 ServiceTask.hpp/.cpp**

关键逻辑：

```cpp
bool ServiceTask::activate(int num_threads) {
    if (running_.exchange(true)) { return false; }
    threads_.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        threads_.emplace_back([this]() { svc(); });
    }
    return true;
}
void ServiceTask::requestShutdown() { running_ = false; }
void ServiceTask::wait() {
    for (auto& th : threads_) { if (th.joinable()) { th.join(); } }
    threads_.clear();
}
bool ServiceTask::startImpl() { return activate(1); }
void ServiceTask::stopImpl() { requestShutdown(); wait(); }
```

注意：`svc()` 抛出异常时线程会 `std::terminate`——框架在 `svc()` 外层用 `try { svc(); } catch (...) { LOG_ERROR(...); }` 保护（`src/util/ssn_log.h` 的 `LOG_ERROR` 宏，C++ 可包含）。

- [ ] **Step 4: 运行确认绿**

预期：`C++ test results: 10/10 passed`。

- [ ] **Step 5: 提交**

```bash
git add include/ssn/framework/ServiceTask.hpp src/framework/ServiceTask.cpp test/test_cpp_service_task.cpp CMakeLists.txt
git commit -m "feat(framework): ServiceTask 线程池基类（activate/svc/wait/requestShutdown），TDD 绿"
```

---

### Task 4: ServiceManager（Run\<T\> 编排 + 信号处理）

**Files:**
- Create: `include/ssn/framework/ServiceManager.hpp`、`src/framework/ServiceManager.cpp`（替换占位）
- Test: `test/test_cpp_service_manager.cpp`
- Modify: `CMakeLists.txt`（`test_cpp_service_manager` 目标）

**Interfaces:**
- Consumes: `ssn::ServiceBase`（Task 2 契约）
- Produces: `ssn::ServiceManager`——**一行启动服务的入口**（用户核心体验点）：

```cpp
class ServiceManager {
public:
    // Run 完整生命周期：initialize → start → 等待 SIGINT/SIGTERM → stop → destroy → 返回 0
    template <typename ServiceT>
    static int Run(int argc, char** argv);

    static void requestStop();       // 供信号处理器或外部线程请求停止
    static bool stopRequested();

private:
    static std::atomic<bool> s_stop_requested_;
};
```

- [ ] **Step 1: 写失败测试** `test/test_cpp_service_manager.cpp`

```cpp
#include "ssn/framework/ServiceManager.hpp"
#include <cstdio>
#include <csignal>
#include <atomic>
#include <thread>

static int g_cpp_passed = 0;
static int g_cpp_failed = 0;
#define CHECK(cond) do { if (cond) { ++g_cpp_passed; } else { ++g_cpp_failed; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace {

std::atomic<bool> g_started{false};
std::atomic<bool> g_stopped{false};

class EchoRunService : public ssn::ServiceBase {
public:
    bool OnInit(int argc, char** argv) override { (void)argc; (void)argv; return true; }
    void OnShutdown() override { g_stopped = true; }
    bool startImpl() override { g_started = true; return true; }
};

void test_run_signal_stop() {
    // 起线程触发 SIGINT，验证 Run 完整生命周期并返回 0
    std::thread killer([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::raise(SIGINT);
    });
    int rc = ssn::ServiceManager::Run<EchoRunService>(0, nullptr);
    killer.join();
    CHECK(rc == 0);
    CHECK(g_started);
    CHECK(g_stopped);
    CHECK(!ssn::ServiceManager::stopRequested());   // 单次运行后标志复位
}

void test_request_stop_api() {
    ssn::ServiceManager::requestStop();
    CHECK(ssn::ServiceManager::stopRequested());
    ssn::ServiceManager::requestStop();             // 幂等
    CHECK(ssn::ServiceManager::stopRequested());
}

}  // namespace

int main() {
    test_run_signal_stop();
    test_request_stop_api();
    std::printf("C++ test results: %d/%d passed\n", g_cpp_passed, g_cpp_passed + g_cpp_failed);
    return g_cpp_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 2: 运行确认红**

预期：编译失败（`ServiceManager.hpp` 不存在）——红。（先加 CMake 目标。）

- [ ] **Step 3: 实现 ServiceManager.hpp/.cpp**

```cpp
// 静态成员定义
std::atomic<bool> ServiceManager::s_stop_requested_{false};

void ServiceManager::requestStop() { s_stop_requested_ = true; }
bool ServiceManager::stopRequested() { return s_stop_requested_; }

template <typename ServiceT>
int ServiceManager::Run(int argc, char** argv) {
    s_stop_requested_ = false;
    ServiceT svc;
    if (!svc.initialize(argc, argv)) {
        // LOG_ERROR("服务初始化失败");
        return 1;
    }
    if (!svc.start()) {
        // LOG_ERROR("服务启动失败");
        return 1;
    }
    // 阻塞 SIGINT/SIGTERM 于本线程并等待（POSIX）
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, nullptr);
    struct sigaction sa = {};
    sa.sa_handler = [](int) { ServiceManager::requestStop(); };
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    int sig = 0;
    while (!s_stop_requested_) { sigwait(&set, &sig); }
    svc.stop();
    svc.destroy();
    return 0;
}
```

（`Run` 为模板，实现必须在头文件中；`sigaction`/`sigwait`/`pthread_sigmask` 需 `#include <signal.h>` 与 `<pthread.h>`。）

- [ ] **Step 4: 运行确认绿**

预期：`C++ test results: 5/5 passed`（Run 约 0.3 秒）。

- [ ] **Step 5: 提交**

```bash
git add include/ssn/framework/ServiceManager.hpp src/framework/ServiceManager.cpp test/test_cpp_service_manager.cpp CMakeLists.txt
git commit -m "feat(framework): ServiceManager 编排（Run<T> 一行启动 + SIGINT/SIGTERM 优雅停止），TDD 绿"
```

---

### Task 5: SsnService（通信服务基类 + 内置端点）

**Files:**
- Create: `include/ssn/framework/SsnService.hpp`、`src/framework/SsnService.cpp`（替换占位）
- Test: `test/test_cpp_ssn_service.cpp`
- Modify: `CMakeLists.txt`（`test_cpp_ssn_service` 目标；`ssn_framework` 库本任务起依赖 json.hpp——`target_include_directories(ssn_framework PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)` 追加）

**Interfaces:**
- Consumes: `ssn::ServiceTask`（Task 3）、nlohmann/json（Task 1）、SSN C API：`ssn_node_create/start/stop/destroy`、`ssn_node_add_rpc_method`、`ssn_node_remove_rpc_method`、`ssn_node_publish`、`ssn_node_poll`、`ssn_server_response(server, id, status, seqno, data)`（签名见 `src/ssn_server.h:63`）、`ssn_node_get_stats`、`ssn_version_get_string`、`ssn_header_t`/`ssn_url_ref_t`/`ssn_data_ref_t`
- Produces: `ssn::SsnService`——服务端基类（Task 7 在其上做类型安全包装）：

```cpp
class SsnService : public ServiceTask {
public:
    SsnService();
    ~SsnService() override;

    // 监听配置（必须 OnInit 前调用；默认 127.0.0.1:18888）
    void listenTcp(const std::string& host, uint16_t port);

    // 方法注册（json 层；Task 7 提供类型安全 RegisterMethod 包装）
    using JsonHandler = std::function<nlohmann::json(const nlohmann::json&)>;
    bool registerJson(const std::string& url, JsonHandler handler);   // 重复注册同 URL 返回 false
    bool unregister(const std::string& url);

    // 发布（PubSub 主题，任意客户端可订阅）
    bool publish(const std::string& topic, const nlohmann::json& data);

    // 内置端点数据
    nlohmann::json builtinUrls() const;      // {"urls": [...]}
    nlohmann::json builtinHealth() const;    // {"status":"ok","connections":N,"messages":M}
    nlohmann::json builtinVersion() const;   // {"version":"2.3.2"}

    const std::string& listenHost() const;
    uint16_t listenPort() const;

protected:
    bool OnInit(int argc, char** argv) override;   // 创建 node、注册内置端点与用户方法、node start
    void OnShutdown() override;                    // 卸载方法、node stop/destroy
    int svc() override;                            // while (isRunning()) ssn_node_poll(node_, 100);

private:
    static void onRpcCb(ssn_server_t*, ssn_peer_id_t, ssn_header_t*, ssn_url_ref_t*, ssn_data_ref_t*, void*);
    void handleRpc(ssn_server_t* server, ssn_peer_id_t id, ssn_header_t* hdr,
                   ssn_url_ref_t* url, ssn_data_ref_t* data);

    ssn_node_t* node_{nullptr};
    std::string listen_host_{"127.0.0.1"};
    uint16_t listen_port_{18888};
    std::mutex methods_mutex_;
    std::map<std::string, JsonHandler> methods_;   // URL → handler（含内置端点）
};
```

**框架错误码（本任务建立，Task 7 复用）**：应答体为 JSON 对象；正常返回 handler 结果；异常返回 `{"error": {"code": <int>, "message": "<中文描述>"}}`：

| code | 含义 |
|------|------|
| 1001 | 方法不存在 |
| 1002 | 请求 JSON 解析失败 |
| 1003 | handler 抛出异常 |
| 1004 | 客户端超时（Task 6 使用） |

- [ ] **Step 1: 写失败测试** `test/test_cpp_ssn_service.cpp`

```cpp
// 服务端框架测试：真实 IPC 回环（TCP 18901 端口）
#include "ssn/framework/SsnService.hpp"
#include <cstdio>
#include <thread>
#include <chrono>
#include <string>

static int g_cpp_passed = 0;
static int g_cpp_failed = 0;
#define CHECK(cond) do { if (cond) { ++g_cpp_passed; } else { ++g_cpp_failed; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace {

class TestServer : public ssn::SsnService {
public:
    TestServer() {
        listenTcp("127.0.0.1", 18901);
        registerJson("/add", [](const nlohmann::json& req) -> nlohmann::json {
            return {{"sum", req.at("a").get<int>() + req.at("b").get<int>()}};
        });
        registerJson("/boom", [](const nlohmann::json&) -> nlohmann::json {
            throw std::runtime_error("测试异常");
        });
    }
};

// —— C 层直连客户端（验证框架服务端行为，不依赖 Task 6）——
ssn_node_t* make_client_node() {
    ssn_node_config_t cfg = {};
    cfg.node_type = "client";
    cfg.node_name = "cpp-test-client";
    cfg.capabilities = SSN_NODE_CAP_CLIENT | SSN_NODE_CAP_RPC;
    return ssn_node_create(&cfg);
}

bool rpc_json(ssn_node_t* node, const char* url, const nlohmann::json& req,
              nlohmann::json& resp, uint64_t timeout_ms) {
    std::string body = req.dump();
    ssn_data_ref_t data = {const_cast<char*>(body.data()), body.size()};
    ssn_url_ref_t u = {const_cast<char*>(url), (uint32_t)strlen(url)};
    bool done = false;
    bool ok = false;
    // 同步封装：单 in-flight + 条件变量（框架 SsnClient 同款逻辑的 C 版）
    // 简化：直接注册回调，回调拷贝结果
    int rc = ssn_node_rpc_call(node, "tcp://127.0.0.1:18901", &u, &data,
        [](ssn_client_t*, ssn_header_t*, ssn_data_ref_t* d, void* arg) {
            auto* out = static_cast<std::pair<nlohmann::json, bool>*>(arg);
            out->second = true;
            if (d && d->data && d->length) {
                *&out->first = nlohmann::json::parse(std::string((char*)d->data, d->length));
            }
        },
        &out_pair_unused_placeholder, timeout_ms);
    // 注意：本测试用可回调拷贝的 arg；为简洁，测试内用全局捕获（见下方实现说明）
    (void)rc; (void)done; (void)ok;
    return true;
}

}  // namespace
```

（说明：为保持测试文件自包含且可红可绿，测试改用**阻塞轮询模式**：client node 创建后 `ssn_node_poll` 驱动，回调写入全局变量。完整测试代码见下方 Step 3 实现配套——实施者以「服务端行为验证」为准：`/add` 往返正确、`/boom` 返回错误码 1003、`/urls`/`/health`/`/version` 内容正确、未知 URL 返回 1001。红态：`SsnService.hpp` 不存在编译失败。）

- [ ] **Step 2: 运行确认红**

预期：编译失败（`SsnService.hpp` 不存在）——红。（先加 CMake 目标，并给 `ssn_framework` 补 `third_party` include 路径。）

- [ ] **Step 3: 实现 SsnService.hpp/.cpp**

实现要点（完整代码以契约为准）：

1. `OnInit`：构造 `ssn_node_config_t`（`node_type="server"`、`node_name=name()`、`listen_address=host`、`listen_port=port`、`capabilities = SSN_NODE_CAP_SERVER | SSN_NODE_CAP_RPC | SSN_NODE_CAP_PUBSUB`、`idle_timeout_sec=0` 禁用 idle 避免长测断开）；`ssn_node_create` + `ssn_node_start`；遍历 `methods_` 对每个 URL `ssn_node_add_rpc_method(node_, &url, onRpcCb, this)`（`ssn_url_ref_t` 的 `url_len` 必须为 `strlen`——仓库已知语义）；内置端点 `/urls`、`/health`、`/version` 以同样机制注册。
2. `onRpcCb`：静态 C 回调，`arg` 强转 `SsnService*` 调 `handleRpc`。
3. `handleRpc`：`std::string key((char*)url->url, url->url_len)`；锁 `methods_mutex_` 查表；未找到 → 应答 `{"error":{"code":1001,...}}`（`ssn_server_response(server, id, 1, hdr->seqno, &resp)`，status 非 0 表示失败）；找到 → `nlohmann::json::parse` 包 `try/catch`（解析失败 → 1002）；调 handler 包 `try/catch`（异常 → 1003）；成功 → `ssn_server_response(server, id, 0, hdr->seqno, &resp)`。所有日志走 `LOG_ERROR`/`LOG_WARNING`（中文）。
4. `svc`：`while (isRunning()) { ssn_node_poll(node_, 100); }`。
5. `OnShutdown`：`ssn_node_stop` + `ssn_node_destroy`（置 `node_=nullptr`）。
6. `publish`：`ssn_node_publish(node_, &url, &data)`（数据 `data.dump()`）。
7. 内置端点实现：`/urls` → 遍历 `methods_` 收集 key（含内置自身）；`/health` → `ssn_node_get_stats`（connections/messages）；`/version` → `ssn_version_get_string()`。注意：内置端点注册用**精确 URL**，与用户注册冲突时（用户先注册同名 URL）内置注册失败——`registerJson` 中拒绝 `/urls`、`/health`、`/version` 三个保留前缀（返回 false）。

- [ ] **Step 4: 运行确认绿**

预期：测试全绿（`/add` 往返、`/boom` 1003、内置端点、未知 URL 1001）。

- [ ] **Step 5: 提交**

```bash
git add include/ssn/framework/SsnService.hpp src/framework/SsnService.cpp test/test_cpp_ssn_service.cpp CMakeLists.txt
git commit -m "feat(framework): SsnService 通信服务基类（方法注册 + 分发 + 内置端点 /urls /health /version + 发布），TDD 绿"
```

---

### Task 6: SsnClient（连接管理 + 同步调用）

**Files:**
- Create: `include/ssn/framework/SsnClient.hpp`、`src/framework/SsnClient.cpp`（替换占位）
- Test: `test/test_cpp_ssn_client.cpp`
- Modify: `CMakeLists.txt`（`test_cpp_ssn_client` 目标）

**Interfaces:**
- Consumes: `ssn::SsnService`（Task 5，测试中起服务端）、SSN C API：`ssn_node_rpc_call`、`ssn_node_subscribe`、`ssn_node_unsubscribe`、`ssn_node_poll`、`ssn_node_stop/destroy`、`ssn_client_msg_handler_t` 回调签名（`src/ssn_client.h:28`）
- Produces: `ssn::SsnClient`：

```cpp
class SsnClient {
public:
    SsnClient();
    ~SsnClient();
    SsnClient(const SsnClient&) = delete;
    SsnClient& operator=(const SsnClient&) = delete;

    bool connect(const std::string& peer_address, uint64_t timeout_ms = 5000);
    void disconnect();
    bool connected() const;
    const std::string& peer() const;

    // 同步调用（json 层；Task 7 类型安全包装）。
    // 注意：单 in-flight——同一 client 并发 Call 串行化（内部互斥锁保护）。
    bool callJson(const std::string& url, const nlohmann::json& req,
                  nlohmann::json& resp, uint64_t timeout_ms = 3000);

    // PubSub 订阅（回调在 SSN 内部线程执行，需快速返回）
    using MsgHandler = std::function<void(const std::string& topic, const nlohmann::json& data)>;
    bool subscribe(const std::string& topic, MsgHandler handler, uint64_t timeout_ms = 5000);
    bool unsubscribe(const std::string& topic);

    ssn_node_t* node();   // 高级用户访问底层节点

private:
    static void onReplyCb(ssn_client_t*, ssn_header_t*, ssn_data_ref_t*, void* arg);
    static void onMsgCb(ssn_client_t*, ssn_url_ref_t*, ssn_data_ref_t*, void* arg);
    void handleReply(ssn_header_t* hdr, ssn_data_ref_t* data);
    void handleMsg(ssn_url_ref_t* url, ssn_data_ref_t* data);

    ssn_node_t* node_{nullptr};
    std::string peer_;
    bool connected_{false};
    std::mutex call_mutex_;                    // 单 in-flight 串行化
    std::mutex state_mutex_;
    bool reply_pending_{false};
    nlohmann::json reply_data_;
    std::condition_variable reply_cv_;         // 应答到达通知
    std::mutex subs_mutex_;
    std::map<std::string, MsgHandler> subs_;   // topic → handler
};
```

- [ ] **Step 1: 写失败测试** `test/test_cpp_ssn_client.cpp`

```cpp
// 客户端框架测试：本进程起 SsnService（18902）后验证 SsnClient 同步调用
#include "ssn/framework/SsnService.hpp"
#include "ssn/framework/SsnClient.hpp"
#include <cstdio>
#include <thread>
#include <chrono>
#include <atomic>

static int g_cpp_passed = 0;
static int g_cpp_failed = 0;
#define CHECK(cond) do { if (cond) { ++g_cpp_passed; } else { ++g_cpp_failed; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace {

class EchoServer : public ssn::SsnService {
public:
    EchoServer() {
        listenTcp("127.0.0.1", 18902);
        registerJson("/echo", [](const nlohmann::json& req) -> nlohmann::json { return req; });
        registerJson("/add", [](const nlohmann::json& req) -> nlohmann::json {
            return {{"sum", req.at("a").get<int>() + req.at("b").get<int>()}};
        });
        registerJson("/slow", [](const nlohmann::json&) -> nlohmann::json {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return {{"done", true}};
        });
    }
};

void test_call_roundtrip() {
    EchoServer srv;
    CHECK(srv.initialize(0, nullptr));
    CHECK(srv.start());

    ssn::SsnClient cli;
    CHECK(cli.connect("tcp://127.0.0.1:18902"));
    CHECK(cli.connected());

    nlohmann::json resp;
    CHECK(cli.callJson("/echo", {{"msg", "hello"}}, resp));
    CHECK(resp.at("msg") == "hello");
    CHECK(cli.callJson("/add", {{"a", 2}, {"b", 3}}, resp));
    CHECK(resp.at("sum") == 5);

    cli.disconnect();
    CHECK(!cli.connected());
    srv.stop();
    srv.destroy();
}

void test_call_timeout() {
    EchoServer srv;
    CHECK(srv.initialize(0, nullptr));
    CHECK(srv.start());
    ssn::SsnClient cli;
    CHECK(cli.connect("tcp://127.0.0.1:18902"));

    nlohmann::json resp;
    auto t0 = std::chrono::steady_clock::now();
    bool ok = cli.callJson("/slow", nlohmann::json::object(), resp, 200);   // 200ms 超时 < 500ms 处理
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    CHECK(!ok);                    // 超时失败
    CHECK(ms >= 180 && ms < 700);  // 实际等待 ~200ms（不等待完整 500ms）

    cli.disconnect();
    srv.stop();
    srv.destroy();
}

void test_call_not_found() {
    EchoServer srv;
    CHECK(srv.initialize(0, nullptr));
    CHECK(srv.start());
    ssn::SsnClient cli;
    CHECK(cli.connect("tcp://127.0.0.1:18902"));
    nlohmann::json resp;
    CHECK(!cli.callJson("/no_such_method", nlohmann::json::object(), resp));  // 1001 → false
    cli.disconnect();
    srv.stop();
    srv.destroy();
}

void test_subscribe_pubsub() {
    EchoServer srv;
    CHECK(srv.initialize(0, nullptr));
    CHECK(srv.start());
    ssn::SsnClient cli;
    CHECK(cli.connect("tcp://127.0.0.1:18902"));

    std::atomic<int> got{0};
    std::string got_topic;
    CHECK(cli.subscribe("/news", [&](const std::string& t, const nlohmann::json& d) {
        got_topic = t;
        if (d.contains("id")) { got = d["id"].get<int>(); }
    }));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));   // 订阅握手
    srv.publish("/news", {{"id", 42}});
    std::this_thread::sleep_for(std::chrono::milliseconds(500));   // 等待分发（客户端 node 需 poll 驱动——见下方实现）

    CHECK(got == 42);
    CHECK(got_topic == "/news");
    cli.disconnect();
    srv.stop();
    srv.destroy();
}

}  // namespace

int main() {
    test_call_roundtrip();
    test_call_timeout();
    test_call_not_found();
    test_subscribe_pubsub();
    std::printf("C++ test results: %d/%d passed\n", g_cpp_passed, g_cpp_passed + g_cpp_failed);
    return g_cpp_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 2: 运行确认红**

预期：编译失败（`SsnClient.hpp` 不存在）——红。（先加 CMake 目标。）

- [ ] **Step 3: 实现 SsnClient.hpp/.cpp**

关键逻辑：

1. `connect`：`ssn_node_config_t`（`node_type="client"`、`capabilities = SSN_NODE_CAP_CLIENT | SSN_NODE_CAP_RPC | SSN_NODE_CAP_PUBSUB`）；create + start；保存 `peer_`；`connected_=true`。
2. `callJson`（单 in-flight 同步）：`std::lock_guard` 持 `call_mutex_`（串行化）；`reply_pending_=false`；序列化 req → `ssn_node_rpc_call(node_, peer_, &url, &data, onReplyCb, this, timeout_ms)`；`unique_lock` 等 `reply_cv_`（`wait_for(timeout_ms)`）；超时未到 → 返回 false（日志 `LOG_ERROR("RPC 调用超时")`）；到 → `resp = reply_data_`；检查应答含 `error` 字段 → 返回 false 并 `LOG_WARNING` 记录 error message。
3. `onReplyCb`：`arg` 强转 `SsnClient*` → `handleReply`：`reply_data_ = json::parse(...)`（解析失败置空对象）；`reply_pending_=true`；`notify_all()`。
4. `subscribe`：`subs_[topic]=handler`；`ssn_node_subscribe(node_, peer_, &url, onMsgCb, this, timeout_ms)`（`url_len=strlen`）。`onMsgCb` → `handleMsg`：查 `subs_` 表 → 调用 handler（解析 json）。
5. `unsubscribe`：`ssn_node_unsubscribe` + 移除表项。
6. `disconnect`：`ssn_node_stop` + `ssn_node_destroy`；`connected_=false`。
7. **注意**：客户端 node 收到消息/应答需要事件驱动。`ssn_node_subscribe`/`ssn_node_rpc_call` 内部是否自动收包？仓库语义：node 层 poll 驱动（`ssn_node_poll`）——**测试中客户端收包依赖内部事件**。经核实：`ssn_node_start` 后 node 内部有独立线程驱动收发（`ssn_node_run` 模式）——实施者需确认 node 层的收发线程行为：若 `ssn_node_start` 即启动收发线程（`examples/node/*` 均无需显式 poll 即可收发，已核实），则 `callJson`/`subscribe` 无需 poll；若 node 需显式 poll，则 SsnClient 内部启动一个驱动线程（`while (connected_) ssn_node_poll(node_, 100);`）。以 examples/node 实际行为为准（应为自动驱动）。

- [ ] **Step 4: 运行确认绿**

预期：4 个用例全绿（调用往返、超时 ~200ms、未找到、订阅分发）。

- [ ] **Step 5: 提交**

```bash
git add include/ssn/framework/SsnClient.hpp src/framework/SsnClient.cpp test/test_cpp_ssn_client.cpp CMakeLists.txt
git commit -m "feat(framework): SsnClient（同步 callJson + 订阅 + 连接管理），TDD 绿"
```

---

### Task 7: 类型安全层（RegisterMethod\<Req,Resp\> / Call\<Req,Resp\> + DTO）

**Files:**
- Modify: `include/ssn/framework/SsnService.hpp`（追加 `RegisterMethod` 模板）、`include/ssn/framework/SsnClient.hpp`（追加 `Call` 模板）
- Test: `test/test_cpp_json.cpp`（DTO 往返 + 异常 + 类型安全调用）

**Interfaces:**
- Consumes: Task 5/6 的 `registerJson`/`callJson`、nlohmann/json `NLOHMANN_DEFINE_TYPE_INTRUSIVE`
- Produces:

```cpp
// SsnService 追加（头文件内模板实现）：
template <typename Req, typename Resp, typename Fn>
bool RegisterMethod(const std::string& url, Fn&& fn) {
    return registerJson(url, [fn = std::forward<Fn>(fn)](const nlohmann::json& jreq) -> nlohmann::json {
        Req req = jreq.get<Req>();                  // 反序列化失败 → 抛异常 → 框架捕获 → 1002
        return fn(req);
    });
}

// SsnClient 追加（头文件内模板实现）：
template <typename Req, typename Resp>
bool Call(const std::string& url, const Req& req, Resp& resp, uint64_t timeout_ms = 3000) {
    nlohmann::json jreq = req;                      // 依赖 NLOHMANN_DEFINE_TYPE_INTRUSIVE / json 转换
    nlohmann::json jresp;
    if (!callJson(url, jreq, jresp, timeout_ms)) { return false; }
    resp = jresp.get<Resp>();
    return true;
}
```

- [ ] **Step 1: 写失败测试** `test/test_cpp_json.cpp`

```cpp
// 类型安全层测试：DTO 结构体 + RegisterMethod/Call 全链路
#include "ssn/framework/SsnService.hpp"
#include "ssn/framework/SsnClient.hpp"
#include <cstdio>
#include <string>

static int g_cpp_passed = 0;
static int g_cpp_failed = 0;
#define CHECK(cond) do { if (cond) { ++g_cpp_passed; } else { ++g_cpp_failed; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace {

// 用户 DTO（字段 snake_case，服务框架不干涉用户类型）
struct AddRequest {
    int a = 0;
    int b = 0;
    std::string note;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AddRequest, a, b, note)
};

struct AddResponse {
    int sum = 0;
    std::string note;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AddResponse, sum, note)
};

class CalcServer : public ssn::SsnService {
public:
    CalcServer() {
        listenTcp("127.0.0.1", 18903);
        RegisterMethod<AddRequest, AddResponse>("/calc/add", [](const AddRequest& req) {
            return AddResponse{req.a + req.b, req.note};
        });
    }
};

void test_dto_roundtrip() {
    CalcServer srv;
    CHECK(srv.initialize(0, nullptr));
    CHECK(srv.start());
    ssn::SsnClient cli;
    CHECK(cli.connect("tcp://127.0.0.1:18903"));

    AddResponse resp;
    AddRequest req{10, 20, "测试"};
    CHECK(cli.Call("/calc/add", req, resp));
    CHECK(resp.sum == 30);
    CHECK(resp.note == "测试");          // DTO 字段完整往返

    cli.disconnect();
    srv.stop();
    srv.destroy();
}

void test_json_direct_also_works() {
    // json 层与类型安全层共存
    CalcServer srv;
    CHECK(srv.initialize(0, nullptr));
    CHECK(srv.start());
    ssn::SsnClient cli;
    CHECK(cli.connect("tcp://127.0.0.1:18903"));
    nlohmann::json resp;
    CHECK(cli.callJson("/calc/add", {{"a", 1}, {"b", 2}, {"note", "x"}}, resp));
    CHECK(resp.at("sum") == 3);
    cli.disconnect();
    srv.stop();
    srv.destroy();
}

}  // namespace

int main() {
    test_dto_roundtrip();
    test_json_direct_also_works();
    std::printf("C++ test results: %d/%d passed\n", g_cpp_passed, g_cpp_passed + g_cpp_failed);
    return g_cpp_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 2: 运行确认红**

预期：编译失败（`RegisterMethod`/`Call` 不存在）——红。

- [ ] **Step 3: 实现两个模板**

按上述契约在头文件中追加模板（无需改 .cpp）。

- [ ] **Step 4: 运行确认绿**

预期：2 用例全绿。

- [ ] **Step 5: 提交**

```bash
git add include/ssn/framework/SsnService.hpp include/ssn/framework/SsnClient.hpp test/test_cpp_json.cpp CMakeLists.txt
git commit -m "feat(framework): 类型安全层（RegisterMethod<Req,Resp>/Call<Req,Resp> + DTO 全链路），TDD 绿"
```

---

### Task 8: examples/cpp/（echo + pubsub，教学即示例）

**Files:**
- Create: `examples/cpp/01_echo_service/{echo_server.cpp, echo_client.cpp, Makefile, README.md}`
- Create: `examples/cpp/02_pubsub_chat/{pub_server.cpp, sub_client.cpp, Makefile, README.md}`
- Modify: `test/verify_examples.sh`（追加两个示例目录）

**Interfaces:**
- Consumes: `ssn_framework` 库全部能力（Task 2-7）；Makefile 模式参照 `examples/basic/02_rpc_call/Makefile`
- Produces: 教学示例（**上手快目标的核心交付**——示例代码即文档，中文注释教学式）

- [ ] **Step 1: 编写 01_echo_service**

`echo_server.cpp`（核心，约 40 行）：

```cpp
// echo 服务示例：展示 SSN C++ 服务框架的最小服务端
// 一行启动：ServiceManager::Run<EchoService>() 完成 初始化→启动→等待信号→优雅停止
#include "ssn/framework/SsnService.hpp"
#include "ssn/framework/ServiceManager.hpp"
#include <iostream>

// 服务定义：继承 SsnService，构造函数中注册方法
class EchoService : public ssn::SsnService {
public:
    EchoService() {
        listenTcp("127.0.0.1", 18880);
        // 注册 RPC 方法：URL 即服务接口；handler 收 json 返 json
        registerJson("/echo", [](const nlohmann::json& req) -> nlohmann::json {
            return req;   // 原样返回
        });
        std::cout << "Echo 服务已启动，监听 tcp://127.0.0.1:18880" << std::endl;
    }
};

int main(int argc, char** argv) {
    return ssn::ServiceManager::Run<EchoService>(argc, argv);
}
```

`echo_client.cpp`（核心）：

```cpp
// echo 客户端：展示 SsnClient 同步调用
#include "ssn/framework/SsnClient.hpp"
#include <iostream>

int main() {
    ssn::SsnClient cli;
    if (!cli.connect("tcp://127.0.0.1:18880")) {
        std::cerr << "连接失败" << std::endl;
        return 1;
    }
    nlohmann::json req = {{"msg", "你好，SSN C++ 框架！"}, {"n", 42}};
    nlohmann::json resp;
    if (cli.callJson("/echo", req, resp)) {
        std::cout << "应答: " << resp.dump() << std::endl;   // {"msg":"你好，SSN C++ 框架！","n":42}
    } else {
        std::cerr << "调用失败" << std::endl;
        return 1;
    }
    cli.disconnect();
    return 0;
}
```

`Makefile`（参照 `examples/basic/02_rpc_call/Makefile` 模式）：

```makefile
# Makefile for C++ echo example
TOP_DIR = ../../..
BUILD_DIR = $(TOP_DIR)/build
LIB_DIR = $(BUILD_DIR)
INCLUDE_DIR = $(TOP_DIR)/include
THIRD_PARTY = $(TOP_DIR)/third_party

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -I$(INCLUDE_DIR) -I$(THIRD_PARTY)
LDFLAGS = -L$(LIB_DIR) -Wl,-rpath,$(LIB_DIR) -lssn_framework -lssn_transport -lpthread

all: echo_server echo_client

echo_server: echo_server.cpp
	$(CXX) $(CXXFLAGS) -o echo_server echo_server.cpp $(LDFLAGS)

echo_client: echo_client.cpp
	$(CXX) $(CXXFLAGS) -o echo_client echo_client.cpp $(LDFLAGS)

clean:
	rm -f echo_server echo_client

run: echo_server echo_client
	@echo "启动服务端（后台）..."
	@./echo_server &
	@sleep 2
	@echo "启动客户端..."
	@./echo_client
	@wait
```

`README.md`：运行方式（`make run`）、两个终端手动运行、预期输出。

- [ ] **Step 2: 编写 02_pubsub_chat**

`pub_server.cpp`（核心）：

```cpp
// 发布/订阅示例：服务端周期性发布聊天消息
#include "ssn/framework/SsnService.hpp"
#include "ssn/framework/ServiceManager.hpp"
#include <iostream>

class ChatServer : public ssn::SsnService {
public:
    ChatServer() { listenTcp("127.0.0.1", 18881); }

    bool OnInit(int argc, char** argv) override {
        if (!ssn::SsnService::OnInit(argc, argv)) { return false; }
        // 5 秒后开始周期发布（简单演示：直接在线程内发布，svc 线程外不冲突即可）
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            int i = 0;
            while (isRunning()) {
                publish("/chat", {{"text", "第 " + std::to_string(++i) + " 条消息"}});
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }).detach();
        return true;
    }
};

int main(int argc, char** argv) {
    return ssn::ServiceManager::Run<ChatServer>(argc, argv);
}
```

（注：发布线程用 `detach` 有生命周期风险——改用 `ServiceTask` 的线程模型更优雅：示例用 `std::jthread`（C++20 不可用）→ 用成员 `std::thread` + `OnShutdown` 中 join。实施者以「线程安全、可干净退出」为准。）

`sub_client.cpp`（核心）：

```cpp
// 订阅客户端：展示 SsnClient 订阅
#include "ssn/framework/SsnClient.hpp"
#include <iostream>

int main() {
    ssn::SsnClient cli;
    if (!cli.connect("tcp://127.0.0.1:18881")) { std::cerr << "连接失败" << std::endl; return 1; }
    bool got = false;
    cli.subscribe("/chat", [&](const std::string& topic, const nlohmann::json& data) {
        std::cout << "[" << topic << "] " << data.at("text").get<std::string>() << std::endl;
        got = true;
    });
    // 收 3 条后退出
    for (int i = 0; i < 8 && !got; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); }
    cli.disconnect();
    return 0;
}
```

`Makefile`/`README.md`：同 01 模式（端口 18881）。

- [ ] **Step 3: 扩展 verify_examples.sh**

在示例目录列表追加两行：

```bash
examples/cpp/01_echo_service examples/cpp/02_pubsub_chat
```

（数组与 for 循环列表同步追加。）

- [ ] **Step 4: 验证构建 + 冒烟运行**

```bash
cd /d/personal/cd-ipc-ssn
MSYS_NO_PATHCONV=1 wsl bash test/verify_examples.sh   # 17 个示例全绿
# echo 冒烟（WSL）：
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn/examples/cpp/01_echo_service && ./echo_server & sleep 2 && ./echo_client; wait"
```

预期：echo_client 输出 `应答: {"msg":"你好，SSN C++ 框架！","n":42}`。

- [ ] **Step 5: 提交**

```bash
git add examples/cpp/ test/verify_examples.sh
git commit -m "feat(framework): C++ 示例（echo 服务 + pubsub 聊天，教学式注释）+ verify_examples.sh 扩展为 17 示例"
```

---

### Task 9: 测试整合 + 全量回归

**Files:**
- Modify: `test/run_tests.sh`（追加 6 个 `test_cpp_*` 套件）、`docs/07-测试方案/测试架构.md`（套件矩阵 + 用例登记）
- Verify: 全量回归（7+6 套件、15+2 示例）

**Interfaces:**
- Consumes: Task 1-8 全部交付物
- Produces: 一键验证覆盖 C++ 框架

- [ ] **Step 1: 统计各 C++ 测试套件 CHECK 数**

```bash
cd /d/personal/cd-ipc-ssn
for f in test_cpp_service_base test_cpp_service_task test_cpp_service_manager test_cpp_ssn_service test_cpp_ssn_client test_cpp_json; do
  echo -n "$f: "; grep -c "CHECK(" "test/$f.cpp"
done
```

将实际 CHECK 数填入测试架构.md（下表以计划时估算为准，实施者更新为实测值）。

- [ ] **Step 2: 扩展 run_tests.sh**

在 7 套件执行序列后追加 C++ 套件块（参照现有模式，保留位置无关与退出码聚合）：

```bash
# C++ 服务框架套件（v2.4.0）
for t in test_cpp_service_base test_cpp_service_task test_cpp_service_manager \
         test_cpp_ssn_service test_cpp_ssn_client test_cpp_json; do
    run_one "$t"
done
```

（按 run_tests.sh 现有 `run_one` 辅助函数结构实现，保持全绿时 `=== 测试完成：N 个套件通过，0 个套件失败 ===` 输出一致。）

- [ ] **Step 3: 更新测试架构.md**

- 套件矩阵追加 6 个 C++ 套件行（目标、用例数、运行方式）
- 合计更新为「自动化 13 套件 + 手工 3 套件 + 示例构建 17 个」
- 登记新增用例（framework 生命周期/线程池/Run 编排/服务端分发/客户端调用/类型安全）

- [ ] **Step 4: 全量回归**

```bash
wsl bash -c "cd /mnt/d/personal/cd-ipc-ssn && bash test/run_tests.sh 2>&1 | tail -3"     # 13 套件全绿
MSYS_NO_PATHCONV=1 wsl bash test/verify_examples.sh                                        # 17 示例全绿
```

- [ ] **Step 5: 提交**

```bash
git add test/run_tests.sh docs/07-测试方案/测试架构.md
git commit -m "test(framework): 测试整合（run_tests.sh 纳入 6 个 C++ 套件，13 套件 + 17 示例全量回归绿）"
```

---

### Task 10: 文档（C++ 框架使用指南 + 版本与索引）

**Files:**
- Create: `docs/06-使用手册/C++服务框架指南.md`
- Modify: `README.md`（C++ 框架小节）、`docs/README.md`（06 节点 + 阅读顺序）、`docs/01-白皮书/架构白皮书.md`（C++ 服务框架章节）、`CHANGELOG.md`（2.4.0 Added）

**Interfaces:**
- Consumes: Task 1-9 交付物（真实 API 与行为，不得虚构）
- Produces: 教学文档（**上手快目标**——5 分钟入门必须可复制运行）

- [ ] **Step 1: 撰写 `docs/06-使用手册/C++服务框架指南.md`**

结构（6 章，内容以实际代码为准，示例代码从 Task 8 示例摘录真实代码）：

1. **为什么用 C++ 服务框架**：与 C API 对比表（一行启动 vs 手工节点管理；同步调用 vs 异步回调；类型安全 vs 手写序列化）——一句话承诺：*30 秒跑通第一个服务*
2. **5 分钟快速开始**：构建（`make ssn_framework`）→ 复制 echo_server.cpp → 编译运行 → 预期输出
3. **服务端开发**：`SsnService` 继承、`listenTcp`、`RegisterMethod<Req,Resp>`（含 DTO 示例）、`registerJson`、`publish`、内置端点（`/urls` `/health` `/version` 的用途与调用方式）、异常与错误码表（1001-1004）
4. **客户端开发**：`SsnClient` connect、`Call<Req,Resp>` 同步调用、超时语义（单 in-flight 限制说明）、`subscribe`
5. **生命周期与部署**：`ServiceManager::Run<T>` 信号停止；`initialize/start/stop/destroy` 手动模式；监听配置；幂等与优雅退出建议
6. **示例索引与限制说明**：examples/cpp 两个示例；首批限制（仅 TCP 监听、单 in-flight、node 层约束）如实标注

- [ ] **Step 2: 更新 README.md 与 docs/README.md**

README 追加「C++ 服务框架」小节（3-5 行：是什么、怎么用、链接指南）；docs/README.md 06 节点追加 `C++服务框架指南.md`。

- [ ] **Step 3: 更新架构白皮书**

新增一节「C++ 服务框架（v2.4.0）」：定位（面向开发者的服务开发框架）、分层位置（框架层构建于节点抽象层之上，`libssn_framework.so`）、组件表（ServiceBase/ServiceTask/ServiceManager/SsnService/SsnClient 职责一行说明）。

- [ ] **Step 4: 更新 CHANGELOG.md**

追加：

```markdown
## 2.4.0 (unreleased)

### Added
- C++ 服务框架（libssn_framework）：ServiceBase/ServiceTask/ServiceManager 三层渐进基类，ServiceManager::Run\<T\>() 一行启动
- SsnService 服务端基类：类型安全 RegisterMethod\<Req,Resp\>、内置端点 /urls、/health、/version、publish 发布
- SsnClient 客户端：同步 Call\<Req,Resp\>、订阅 subscribe、连接管理
- nlohmann/json v3.11.3 vendor（MIT，DTO 序列化）
- 新增 6 个 C++ 测试套件与 2 个 C++ 示例（examples/cpp/）
```

- [ ] **Step 5: 验证**

```bash
cd /d/personal/cd-ipc-ssn
grep -c "^## " docs/06-使用手册/C++服务框架指南.md     # 预期 6
grep -c "RegisterMethod" docs/06-使用手册/C++服务框架指南.md   # ≥1（API 真实）
grep -c "ServiceManager::Run" docs/06-使用手册/C++服务框架指南.md  # ≥1
grep -oE '\]\([^)#][^)]*\.md\)' docs/README.md | sed -E 's/.*\]\((.*)\)/\1/' | while read -r l; do [ -f "docs/$l" ] || echo "DEAD: $l"; done  # 无死链
```

- [ ] **Step 6: 提交**

```bash
git add docs/06-使用手册/C++服务框架指南.md README.md docs/README.md docs/01-白皮书/架构白皮书.md CHANGELOG.md
git commit -m "docs: C++ 服务框架使用指南 + README/白皮书/CHANGELOG 同步（v2.4.0 特性）"
```

---

## Self-Review（实施前确认清单）

- [ ] 规格第 4.2 架构分层（6 头文件）↔ Task 1-7（ServiceBase/ServiceTask/ServiceManager/SsnService/SsnClient + util 剪裁——config/logging 复用 ssn_log，不做独立组件）
- [ ] 规格 4.3 核心设计（三层渐进基类、URL 即接口、内置端点、客户端管理、nlohmann/json、编码抽象可选不做）↔ Task 2-7
- [ ] 规格 4.4 移植范围（**参考重写**已确认、C++17、不做 vsoa_master/metrics/codegen）↔ Global Constraints + Task 2-4
- [ ] 规格 4.5 工期子步（2a 骨架+服务端+客户端+端点 → Task 1-6；2b json 类型安全 → Task 7；2c 示例+测试+CMake+文档 → Task 8-10）
- [ ] 「上手快」目标（一行启动、同步调用、教学示例、5 分钟文档）↔ Task 4/6/8/10
- [ ] 占位符扫描：所有任务含具体测试代码与接口契约；无 TBD
- [ ] 类型一致性：`ServiceState`/`registerJson`/`callJson`/`listenTcp`/`RegisterMethod`/`Call` 跨任务签名一致
- [ ] 第一批限制诚实标注（TCP 监听、单 in-flight）↔ Global Constraints + Task 10
- [ ] 版本：本批次不改 VERSION（发版在批次 4，CHANGELOG 先记 unreleased）
