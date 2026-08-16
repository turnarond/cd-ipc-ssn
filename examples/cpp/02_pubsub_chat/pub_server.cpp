/*
 * Copyright (c) 2026 SSN Project.
 * All rights reserved.
 *
 * 聊天服务端示例（类型安全方法 + 周期发布）
 */
// ============================================================================
// 聊天服务端示例 —— 展示 SSN C++ 框架「类型安全方法 + 周期发布」
//
// 教学要点（与 01_echo 的 JSON 层 API 互补，本示例使用类型安全层）：
//   1. RegisterMethod<Req, Resp>：用 DTO 结构体描述请求/应答，handler
//      收到反序列化好的 C++ 对象、返回对象自动序列化——编译期类型检查；
//   2. publish：在独立线程中周期发布 PubSub 主题消息。publish 在 svc 线程
//      之外调用，不持有节点锁，与 RPC 分发无冲突（但不得在 handleRpc
//      handler 内调用——那会自锁死锁）；
//   3. 发布线程生命周期：成员 std::thread + 停止标志，OnShutdown 中
//      「先置标志 → join 回收 → 再走基类清理」，避免 detach 的失控风险，
//      也保证节点销毁前发布线程已退出。
// ============================================================================
#include "ssn/framework/ServiceManager.hpp"
#include "ssn/framework/SsnService.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

// ---- DTO：请求/应答数据结构。NLOHMANN_DEFINE_TYPE_INTRUSIVE 在结构体内部
// ---- 注入 to_json/from_json（侵入式宏，必须写在类体内），成员名即 JSON
// ---- 字段名（两端一致即接口契约）
struct JoinReq {                        // 加入聊天室的请求
    std::string nickname;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(JoinReq, nickname)
};

struct JoinResp {                       // 加入聊天室的应答
    std::string welcome;
    int member_count;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(JoinResp, welcome, member_count)
};

// 聊天服务：一个类型安全 RPC 方法（/chat/join）+ 周期性发布聊天消息（/chat）
class ChatServer : public ssn::SsnService {
public:
    ChatServer() {
        listenTcp("127.0.0.1", 18881);

        // 类型安全方法注册：URL + DTO 化 handler。
        // 请求字段缺失/类型不符、或 handler 抛异常时，框架自动按错误码 1003 应答
        RegisterMethod<JoinReq, JoinResp>("/chat/join", [this](const JoinReq& req) {
            // 返回 DTO 对象即可，框架自动序列化为 JSON 应答
            return JoinResp{"欢迎 " + req.nickname + " 加入聊天室！", ++joined_};
        });
    }

    // 失败路径兜底：若 start() 失败（Run 返回 1 直接析构，不触发 OnShutdown），
    // 已启动的发布线程在此回收，避免 joinable 析构 → std::terminate（技术债 #11）。
    // 注意：析构须在 public 区（Run<ChatServer> 栈上实例化需要可访问析构）。
    ~ChatServer() override {
        stop_pub_ = true;
        if (pub_thread_.joinable()) {
            pub_thread_.join();
        }
    }

protected:
    // 服务初始化完成后启动发布线程（独立线程：publish 不持有节点锁，安全）
    bool OnInit(int argc, char** argv) override {
        if (!ssn::SsnService::OnInit(argc, argv)) {
            return false;
        }
        pub_thread_ = std::thread([this] { publishLoop(); });
        return true;
    }

    // 优雅停止：顺序不可颠倒——基类清理会销毁节点，发布线程必须先退出
    void OnShutdown() override {
        stop_pub_ = true;                  // ① 置停止标志，发布循环据此退出
        if (pub_thread_.joinable()) {
            pub_thread_.join();            // ② 等待发布线程结束（至多一个发布周期）
        }
        ssn::SsnService::OnShutdown();     // ③ 基类清理：停 svc 线程并销毁节点
    }

private:
    // 发布循环：每 1 秒向 /chat 主题发布一条消息
    void publishLoop() {
        std::this_thread::sleep_for(std::chrono::seconds(1));   // 等客户端完成订阅
        int seq = 0;
        while (isRunning() && !stop_pub_) {
            publish("/chat", {{"text", "第 " + std::to_string(++seq) + " 条消息"}});
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::thread pub_thread_;                 // 发布线程（成员变量：生命周期可控，不用 detach）
    std::atomic<bool> stop_pub_{false};      // 发布循环停止标志
    std::atomic<int> joined_{0};             // 已加入成员数（原子计数，跨线程安全）
};

int main(int argc, char** argv) {
    return ssn::ServiceManager::Run<ChatServer>(argc, argv);
}
