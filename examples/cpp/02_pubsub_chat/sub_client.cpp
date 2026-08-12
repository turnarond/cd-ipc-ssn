// ============================================================================
// 聊天客户端示例 —— 展示 SsnClient 的类型安全调用（Call<Req,Resp>）与订阅
//
// 教学要点：
//   1. Call<Req, Resp>：与 01_echo 的 callJson 互补——传 C++ 对象、收 C++
//      对象，编译期类型检查（Req/Resp 反序列化失败会抛异常）；
//   2. subscribe：注册主题处理器，收到消息时框架在内部线程调用回调。
//      回调执行期间持有节点锁：不得调用本客户端的 callJson / subscribe /
//      disconnect（会自锁死锁），只允许拷贝数据、打印或设置标志，需快速返回；
//   3. 主线程轮询接收计数：收满 3 条消息后优雅退出。
// ============================================================================
#include "ssn/framework/SsnClient.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

// DTO 必须与 pub_server 中的定义保持一致（JSON 字段名即接口契约；
// 侵入式宏必须写在结构体内部）
struct JoinReq {
    std::string nickname;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(JoinReq, nickname)
};

struct JoinResp {
    std::string welcome;
    int member_count;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(JoinResp, welcome, member_count)
};

int main() {
    ssn::SsnClient cli;
    if (!cli.connect("tcp://127.0.0.1:18881")) {
        std::cerr << "连接失败：请确认 pub_server 已启动" << std::endl;
        return 1;
    }

    // 类型安全调用：请求传 C++ 对象，应答反序列化进 resp（编译期类型检查）
    JoinResp resp;
    if (cli.Call<JoinReq, JoinResp>("/chat/join", JoinReq{"小明"}, resp)) {
        std::cout << "加入结果: " << resp.welcome
                  << "（当前成员 " << resp.member_count << " 人）" << std::endl;
    } else {
        std::cerr << "调用 /chat/join 失败" << std::endl;
        cli.disconnect();
        return 1;
    }

    // 订阅 /chat 主题；回调在内部线程执行，须快速返回（见文件头注释的锁约束）。
    // subscribe 是同步握手（默认 5 秒超时），务必检查返回值
    std::atomic<int> received{0};   // 已接收消息数（回调在别的线程，须用原子类型）
    bool subscribed = cli.subscribe("/chat", [&](const std::string& topic, const nlohmann::json& data) {
        std::cout << "[" << topic << "] " << data.at("text").get<std::string>() << std::endl;
        ++received;
    });
    if (!subscribed) {
        std::cerr << "订阅失败：请确认 pub_server 已启动" << std::endl;
        cli.disconnect();
        return 1;
    }

    // 收满 3 条消息后退出（每 0.5 秒检查一次，最多等 10 秒，防无限等待）
    for (int i = 0; i < 20 && received < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (received < 3) {
        std::cerr << "未收到足够的聊天消息，请确认 pub_server 已启动" << std::endl;
        cli.disconnect();
        return 1;
    }

    cli.disconnect();
    return 0;
}
