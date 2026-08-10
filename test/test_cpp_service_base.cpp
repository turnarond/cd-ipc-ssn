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
