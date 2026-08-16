// 测试：ServiceTask 线程池基类——activate/svc/wait/requestShutdown 与生命周期集成
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

// Issue #5-1 回归：activate 边界态——非法线程数必须拒绝且不置 running_
void test_activate_boundary() {
    CountingTask t;
    CHECK(!t.activate(0));               // 0 线程：拒绝
    CHECK(!t.isRunning());
    CHECK(!t.activate(-1));              // 负线程数：拒绝
    CHECK(!t.isRunning());
    CHECK(t.threadCount() == 0);
    CHECK(t.activate(1));                // 边界失败后仍可正常激活
    CHECK(t.isRunning());
    t.requestShutdown();
    t.wait();
}

// Issue #5-1 回归：activate 后不手动 stop 直接析构——析构函数必须回收线程，
// 否则 joinable 线程 → std::terminate（本测试若崩溃即红）
void test_destructor_reclaims_threads() {
    {
        CountingTask t;
        CHECK(t.activate(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        // 作用域结束即析构：不调用 requestShutdown/wait，析构必须兜底回收
    }
    // 到达此处即证明析构未 terminate、线程已回收
    CHECK(true);
}

}  // namespace

int main() {
    test_activate_and_run();
    test_lifecycle_integration();
    test_multithread_ticks();
    test_activate_boundary();
    test_destructor_reclaims_threads();
    std::printf("C++ test results: %d/%d passed\n", g_cpp_passed, g_cpp_passed + g_cpp_failed);
    return g_cpp_failed == 0 ? 0 : 1;
}
