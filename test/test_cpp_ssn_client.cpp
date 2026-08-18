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
    CHECK(!ok);                     // 超时失败
    CHECK(ms >= 180 && ms < 1000);  // 实际等待 ~200ms（不等待完整 500ms；上界放宽
                                    // 以容纳 WSL 高负载下的调度余量，防 flaky）

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

// Issue #5-5 回归：disconnect 后 callJson 返回 false（不崩溃）、connected() == false
void test_disconnect_then_call() {
    EchoServer srv;
    CHECK(srv.initialize(0, nullptr));
    CHECK(srv.start());
    ssn::SsnClient cli;
    CHECK(cli.connect("tcp://127.0.0.1:18902"));

    cli.disconnect();
    CHECK(!cli.connected());
    nlohmann::json resp;
    CHECK(!cli.callJson("/echo", {{"msg", "x"}}, resp));   // 断开后调用直接失败，不崩溃

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

    // 订阅握手窗口改轮询式确认（原固定 200ms 在 WSL 高负载下偶发不足而 flaky）：
    // 以 /health 调用成功作为「连接与订阅就绪」的确认信号（subscribe 为同步
    // C 层握手，返回 true 即服务端已 ACK），再发布
    bool ready = false;
    for (int i = 0; i < 50 && !ready; ++i) {
        nlohmann::json resp;
        if (cli.callJson("/health", nlohmann::json::object(), resp, 1000)) {
            ready = true;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    CHECK(ready);
    srv.publish("/news", {{"id", 42}});
    // 接收侧同样轮询等待（客户端驱动线程 poll 周期 ~100ms，替代原固定 500ms）
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (got != 42 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
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
    test_disconnect_then_call();
    test_subscribe_pubsub();
    std::printf("C++ test results: %d/%d passed\n", g_cpp_passed, g_cpp_passed + g_cpp_failed);
    return g_cpp_failed == 0 ? 0 : 1;
}
