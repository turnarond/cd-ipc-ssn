// ============================================================================
// echo 服务端示例 —— SSN C++ 框架「最小可运行」服务（约 40 行）
//
// 教学要点：
//   1. 一行启动：ServiceManager::Run<EchoService>() 自动完成
//      「初始化 → 启动 → 等待 Ctrl+C → 优雅停止」的完整生命周期；
//   2. 方法注册（JSON 层）：registerJson 把「URL 路径」映射到处理函数，
//      框架负责 JSON 编解码、请求分发与应答回传，业务代码只需
//      「输入 JSON → 输出 JSON」；
//   3. 内置端点：服务自带 /urls、/health、/version 三个管理接口
//      （可在 README 中查看体验方法）。
// ============================================================================
#include "ssn/framework/ServiceManager.hpp"
#include "ssn/framework/SsnService.hpp"

#include <iostream>
#include <nlohmann/json.hpp>

// 服务定义：继承 SsnService，构造函数中完成「监听 + 注册方法」配置
class EchoService : public ssn::SsnService {
public:
    EchoService() {
        // 监听配置：TCP 地址 127.0.0.1:18880（必须在服务初始化前调用）
        listenTcp("127.0.0.1", 18880);

        // 注册 RPC 方法：URL 即服务接口名；handler 收到请求 JSON，返回应答 JSON
        registerJson("/echo", [](const nlohmann::json& req) -> nlohmann::json {
            return req;   // echo 语义：请求是什么，应答就是什么
        });
    }

    // 初始化钩子：基类 OnInit 完成后服务端已真实监听，此时打印启动信息
    bool OnInit(int argc, char** argv) override {
        if (!ssn::SsnService::OnInit(argc, argv)) {
            return false;
        }
        std::cout << "Echo 服务已启动，监听 tcp://" << listenHost() << ":"
                  << listenPort() << "（Ctrl+C 优雅退出）" << std::endl;
        return true;
    }
};

int main(int argc, char** argv) {
    // 一行启动：完整生命周期由 ServiceManager 编排，收到信号后自动优雅退出
    return ssn::ServiceManager::Run<EchoService>(argc, argv);
}
