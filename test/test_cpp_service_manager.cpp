// 测试：ServiceManager 编排——Run<T> 一行启动完整生命周期 + SIGINT/SIGTERM 优雅停止
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

    // Issue #5-2 回归：Run 返回后必须还原信号状态——SIGINT/SIGTERM 处理器恢复
    // 默认（本测试进程未安装过其他处理器，还原后即 SIG_DFL），且不再被阻塞
    struct sigaction sa = {};
    sigaction(SIGINT, nullptr, &sa);
    CHECK(sa.sa_handler == SIG_DFL);
    sigaction(SIGTERM, nullptr, &sa);
    CHECK(sa.sa_handler == SIG_DFL);
    sigset_t cur;
    sigemptyset(&cur);
    pthread_sigmask(SIG_SETMASK, nullptr, &cur);
    CHECK(!sigismember(&cur, SIGINT));
    CHECK(!sigismember(&cur, SIGTERM));
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
