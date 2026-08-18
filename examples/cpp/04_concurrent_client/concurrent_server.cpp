// ============================================================================
// 并发客户端示例 —— 服务端（与 concurrent_client 配对）
//
// 教学要点：
//   1. /slow 睡眠 500ms——并发耗时对比实验的「标尺」：串行化总耗时 ≈ 2×500ms，
//      真并行总耗时 ≈ 500ms；
//   2. /echo_id 回显请求 id + 服务端自增序号——并发正确性验证：应答与请求
//      一一配对（同一 client 单 in-flight 串行化下不会错配）。
// ============================================================================
#include "ssn/framework/ServiceManager.hpp"
#include "ssn/framework/SsnService.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

class ConcurrentServer : public ssn::SsnService {
public:
    ConcurrentServer() {
        listenTcp("127.0.0.1", 18883);
        registerJson("/slow", [](const nlohmann::json&) -> nlohmann::json {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return {{"done", true}};
        });
        registerJson("/echo_id", [](const nlohmann::json& req) -> nlohmann::json {
            return {{"id", req.at("id").get<int>()}, {"seq", ++seq_counter_}};
        });
    }

private:
    static std::atomic<int> seq_counter_;
};
std::atomic<int> ConcurrentServer::seq_counter_{0};

int main(int argc, char** argv) {
    std::cout << "并发服务端启动，监听 tcp://127.0.0.1:18883（Ctrl+C 优雅退出）"
              << std::endl;
    return ssn::ServiceManager::Run<ConcurrentServer>(argc, argv);
}
