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

class FailStartService : public ssn::ServiceBase {
public:
    bool OnInit(int argc, char** argv) override { (void)argc; (void)argv; g_order += "init;"; return true; }
    bool startImpl() override { g_order += "startImpl;"; return false; }
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

// destroy 于 Started 态：先自动 stop（OnShutdown/stopImpl 均被调用）再归位 Created
// （Task 2-M4 硬化用例：destroy 时若 Started 先 stop 且钩子被调）
void test_destroy_from_started() {
    TestService svc;
    g_order.clear();
    CHECK(svc.initialize(0, nullptr));
    CHECK(svc.start());
    CHECK(svc.state() == ssn::ServiceState::Started);
    svc.destroy();
    CHECK(svc.state() == ssn::ServiceState::Created);
    CHECK(g_order == "init;startImpl;shutdown;stopImpl;");   // 钩子全量按序调用
    CHECK(svc.initialize(0, nullptr));                       // 销毁后可重新初始化
}

// startImpl 失败：start() 返回 false，状态回 Initialized，停机钩子不被调用
// （Task 2-M4 硬化用例）
void test_start_impl_failure() {
    FailStartService svc;
    g_order.clear();
    CHECK(svc.initialize(0, nullptr));
    CHECK(svc.state() == ssn::ServiceState::Initialized);
    CHECK(!svc.start());
    CHECK(svc.state() == ssn::ServiceState::Initialized);    // 失败回 Initialized
    CHECK(g_order == "init;startImpl;");                     // OnShutdown/stopImpl 未被调用
    svc.destroy();                                           // Initialized 态销毁：直接归位
    CHECK(svc.state() == ssn::ServiceState::Created);
}

// stop 于非 Started 态为 no-op：状态不变，钩子不被调用（Task 2-M4 硬化用例）
void test_stop_noop_not_started() {
    TestService svc;
    g_order.clear();
    svc.stop();                                              // Created 态 stop
    CHECK(svc.state() == ssn::ServiceState::Created);
    CHECK(svc.initialize(0, nullptr));
    svc.stop();                                              // Initialized 态 stop
    CHECK(svc.state() == ssn::ServiceState::Initialized);
    CHECK(g_order == "init;");                               // stop 未触发任何钩子
    svc.destroy();
    CHECK(svc.state() == ssn::ServiceState::Created);
}

// 重复 initialize→destroy→initialize：销毁后重新初始化不泄漏、状态正确
// （Task 6-M4 用例；Task 5 Minor-1 修复在 SsnService 层——节点回收，
// 此用例覆盖基类生命周期通用路径，SsnService 侧回归见 test_cpp_ssn_service.cpp）
void test_reinit_lifecycle() {
    TestService svc;
    g_order.clear();
    CHECK(svc.initialize(0, nullptr));
    CHECK(svc.state() == ssn::ServiceState::Initialized);
    svc.destroy();                                           // 未 start 直接销毁：归位 Created
    CHECK(svc.state() == ssn::ServiceState::Created);
    CHECK(svc.initialize(0, nullptr));                       // 重新初始化：钩子再次运行
    CHECK(svc.state() == ssn::ServiceState::Initialized);
    CHECK(svc.start());
    svc.destroy();                                           // Started 态销毁：完整停机序列
    CHECK(svc.state() == ssn::ServiceState::Created);
    CHECK(g_order == "init;init;startImpl;shutdown;stopImpl;");
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
    test_destroy_from_started();
    test_start_impl_failure();
    test_stop_noop_not_started();
    test_reinit_lifecycle();
    std::printf("C++ test results: %d/%d passed\n", g_cpp_passed, g_cpp_passed + g_cpp_failed);
    return g_cpp_failed == 0 ? 0 : 1;
}
