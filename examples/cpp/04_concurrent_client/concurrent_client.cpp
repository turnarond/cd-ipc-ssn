// ============================================================================
// 并发客户端示例 —— 单 client 串行化 vs 多 client 真并行
//
// 教学要点：
//   1. SsnClient 是「单 in-flight」语义：同一 client 的并发 callJson 被内部
//      互斥锁串行化（后到者排队）。双线程 × 单 client 调两次 /slow（各 500ms）
//      → 总耗时 ≈ 2×500ms；
//   2. 需要真并行时用独立 client（或独立进程）：双 client × 双线程 → 总耗时
//      ≈ 500ms；
//   3. /echo_id 校验并发正确性：串行化下应答与请求严格一一配对（回显 id 一致、
//      序号互不重复）；
//   4. 耗时对比用 steady_clock 实测输出，直观理解「排队 vs 并行」。
// ============================================================================
#include "ssn/framework/SsnClient.hpp"

#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

namespace {

constexpr const char* SERVER_ADDR = "tcp://127.0.0.1:18883";

double elapsed_ms(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - t0).count();
}

// 演示 1：单 client + 双线程 → 串行化（总耗时 ≈ 2×500ms = 1000ms）
void demo_serialized() {
    ssn::SsnClient cli;
    if (!cli.connect(SERVER_ADDR)) {
        std::cerr << "连接失败：请确认 concurrent_server 已启动" << std::endl;
        exit(1);
    }

    nlohmann::json resp_a, resp_b;
    bool ok_a = false, ok_b = false;
    auto t0 = std::chrono::steady_clock::now();
    std::thread ta([&] { ok_a = cli.callJson("/slow", nlohmann::json::object(), resp_a, 3000); });
    std::thread tb([&] { ok_b = cli.callJson("/slow", nlohmann::json::object(), resp_b, 3000); });
    ta.join();
    tb.join();
    std::cout << "[串行化] 单 client × 双线程两次 /slow："
              << static_cast<int>(elapsed_ms(t0)) << "ms"
              << "（≈2×500ms，第二次调用在队列中等待第一次完成）"
              << "，成功=" << (ok_a && ok_b ? "是" : "否") << std::endl;
    cli.disconnect();
}

// 演示 2：双 client + 双线程 → 请求同时到达，但服务端单线程串行处理
//（实测 ≈ 2×500ms，与演示 1 几乎相同——排队位置不同：演示 1 是 client 端
// 单 in-flight 排队，本演示是服务端 poll 线程排队；两次调用都真实到达了
// 服务端，只是 handler 被服务端逐个执行）
void demo_parallel() {
    ssn::SsnClient cli_a, cli_b;
    if (!cli_a.connect(SERVER_ADDR) || !cli_b.connect(SERVER_ADDR)) {
        std::cerr << "连接失败：请确认 concurrent_server 已启动" << std::endl;
        exit(1);
    }

    nlohmann::json resp_a, resp_b;
    bool ok_a = false, ok_b = false;
    auto t0 = std::chrono::steady_clock::now();
    std::thread ta([&] { ok_a = cli_a.callJson("/slow", nlohmann::json::object(), resp_a, 3000); });
    std::thread tb([&] { ok_b = cli_b.callJson("/slow", nlohmann::json::object(), resp_b, 3000); });
    ta.join();
    tb.join();
    std::cout << "[双 client] 双 client × 双线程两次 /slow："
              << static_cast<int>(elapsed_ms(t0)) << "ms"
              << "（请求同时到达，服务端串行处理 handler，仍 ≈2×500ms）"
              << "，成功=" << (ok_a && ok_b ? "是" : "否") << std::endl;
    cli_a.disconnect();
    cli_b.disconnect();
}

// 演示 3：并发正确性——单 client 双线程并发 /echo_id，应答与请求一一配对
void demo_serialized_correctness() {
    ssn::SsnClient cli;
    if (!cli.connect(SERVER_ADDR)) {
        std::cerr << "连接失败：请确认 concurrent_server 已启动" << std::endl;
        exit(1);
    }

    nlohmann::json resp_a, resp_b;
    bool ok_a = false, ok_b = false;
    int seq_a = -1, seq_b = -1;
    std::thread ta([&] {
        ok_a = cli.callJson("/echo_id", {{"id", 1001}}, resp_a, 3000);
        if (ok_a) { seq_a = resp_a.at("seq").get<int>(); }
    });
    std::thread tb([&] {
        ok_b = cli.callJson("/echo_id", {{"id", 1002}}, resp_b, 3000);
        if (ok_b) { seq_b = resp_b.at("seq").get<int>(); }
    });
    ta.join();
    tb.join();

    // 正确性：各自收到自己的应答（回显 id 匹配）、序号互不重复（无错配/覆盖）
    bool id_ok = ok_a && ok_b &&
                 resp_a.at("id").get<int>() == 1001 &&
                 resp_b.at("id").get<int>() == 1002;
    bool seq_ok = seq_a != seq_b && seq_a > 0 && seq_b > 0;
    std::cout << "[正确性] 并发 /echo_id 应答配对：id 匹配=" << (id_ok ? "是" : "否")
              << "，序号互不重复=" << (seq_ok ? "是" : "否")
              << "（seq=" << seq_a << "/" << seq_b << "）" << std::endl;
    cli.disconnect();
}

}  // namespace

int main() {
    demo_serialized();            // 单 client 双线程：串行化排队
    demo_parallel();              // 双 client 双线程：真并行
    demo_serialized_correctness();  // 并发下应答配对正确性
    return 0;
}
