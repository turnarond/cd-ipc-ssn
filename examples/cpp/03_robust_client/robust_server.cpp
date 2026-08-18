// ============================================================================
// 稳健客户端示例 —— 服务端（与 robust_client 配对）
//
// 教学要点：
//   1. 服务端注册三个方法演示客户端的三态分类：
//        /echo  正常回显（客户端「成功」分支）；
//        /slow  睡眠 500ms 慢处理（客户端配短超时 → 「超时」分支）；
//        /boom  抛出异常（框架应答错误码 1003 → 「服务端错误」分支）；
//   2. 服务端本身也是「稳健」示范：框架捕获 handler 异常（1003）、
//      内置 /health 端点供客户端探测健康状态（重连场景的判活信号）。
// ============================================================================
#include "ssn/framework/ServiceManager.hpp"
#include "ssn/framework/SsnService.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

class RobustServer : public ssn::SsnService {
public:
    RobustServer() {
        listenTcp("127.0.0.1", 18882);
        registerJson("/echo", [](const nlohmann::json& req) -> nlohmann::json {
            return req;
        });
        registerJson("/slow", [](const nlohmann::json&) -> nlohmann::json {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return {{"done", true}};
        });
        registerJson("/boom", [](const nlohmann::json&) -> nlohmann::json {
            throw std::runtime_error("稳健示例：handler 抛异常（框架应答 1003）");
        });
    }
};

int main(int argc, char** argv) {
    std::cout << "稳健服务端启动，监听 tcp://127.0.0.1:18882（Ctrl+C 优雅退出）"
              << std::endl;
    return ssn::ServiceManager::Run<RobustServer>(argc, argv);
}
