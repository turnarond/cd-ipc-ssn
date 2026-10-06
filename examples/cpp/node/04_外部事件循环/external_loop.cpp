// external_loop.cpp - ssn::Node 外部事件循环示例（旅程 4）
//
// 外部 poll() 与 startBackground() 是二选一的生命周期约定。本示例：
// 服务节点不调用 startBackground()，由用户主循环周期性调用 poll(10ms)
// 驱动 accept、收包与回复；客户端节点使用后台模式——两种驱动方式在同一
// 进程内各自独立工作。
// 构建与运行：make run（单进程自演示，无需外部服务端）
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <ssn/node/Node.hpp>

namespace {

constexpr const char* kServerAddr = "tcp://127.0.0.1:19505";

bool wait_connected(ssn::Node& node, ssn::PeerId peer) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (std::chrono::steady_clock::now() < deadline) {
        auto info = node.peerInfo(peer);
        if (info.ok() && info.value().state == ssn::PeerState::Connected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

ssn::ByteView to_byte_view(const std::string& text) {
    return ssn::ByteView{reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

}  // namespace

int main() {
    // 1) 服务节点：仅 listen，不 startBackground——事件由外部循环驱动
    auto srv_created = ssn::Node::create(ssn::NodeConfig{});
    if (!srv_created) {
        std::fprintf(stderr, "FAIL: Node::create（server）\n");
        return 1;
    }
    auto server = std::make_unique<ssn::Node>(std::move(srv_created.value()));
    if (!server->listen(ssn::ListenAddress{kServerAddr})) {
        std::fprintf(stderr, "FAIL: listen %s\n", kServerAddr);
        return 1;
    }
    std::atomic<int> served{0};
    // 捕获裸指针（server 由 unique_ptr 持有至 main 结尾，回调期间存活有保证）
    server->setEventHandler([srv = server.get(), &served](const ssn::NodeEvent& event) {
        if (event.type != ssn::NodeEventType::MessageReceived || event.message.empty()) {
            return;
        }
        const std::string request(reinterpret_cast<const char*>(event.message.data()),
                                  event.message.size());
        const std::string reply = "已处理: " + request;
        std::printf("[server] 外部循环处理: %s\n", request.c_str());
        (void)srv->send(event.peer, to_byte_view(reply));   // 回调内重入 send 安全
        served.fetch_add(1);
    });

    // 2) 客户端节点：后台模式（与 server 的外部 poll 模式互不影响）
    std::atomic<int> replies{0};
    auto cli_created = ssn::Node::create(ssn::NodeConfig{});
    if (!cli_created) {
        std::fprintf(stderr, "FAIL: Node::create（client）\n");
        return 1;
    }
    auto client = std::make_unique<ssn::Node>(std::move(cli_created.value()));
    client->setEventHandler([&replies](const ssn::NodeEvent& event) {
        if (event.type == ssn::NodeEventType::MessageReceived && !event.message.empty()) {
            const std::string text(reinterpret_cast<const char*>(event.message.data()),
                                   event.message.size());
            std::printf("[client] 收到: %s\n", text.c_str());
            replies.fetch_add(1);
        }
    });
    if (!client->startBackground()) {
        std::fprintf(stderr, "FAIL: startBackground（client）\n");
        return 1;
    }
    auto peer = client->connect(ssn::ListenAddress{kServerAddr},
                                ssn::ConnectOptions{std::chrono::milliseconds(3000)});
    if (!peer || !wait_connected(*client, peer.value())) {
        std::fprintf(stderr, "FAIL: 连接未建立\n");
        return 1;
    }

    // 3) 用户主循环：poll 驱动 server 的 accept 与收包，同时观察回复数。
    //    注意：同一 Node 生命周期内 poll() 与 startBackground() 不得混用。
    for (int i = 1; i <= 3; ++i) {
        const std::string task = "任务-" + std::to_string(i);
        if (!client->send(peer.value(), to_byte_view(task))) {
            std::fprintf(stderr, "FAIL: send %s\n", task.c_str());
            return 1;
        }
        std::printf("[client] 已发送: %s\n", task.c_str());
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (std::chrono::steady_clock::now() < deadline) {
        (void)server->poll(std::chrono::milliseconds(10));   // 外部事件驱动
        if (served.load() >= 3 && replies.load() >= 3) {
            break;
        }
    }
    if (served.load() < 3 || replies.load() < 3) {
        std::fprintf(stderr, "FAIL: 消息流转不完整（served=%d replies=%d）\n",
                     served.load(), replies.load());
        return 1;
    }

    // 4) 收尾：外部模式以 stop + waitStopped 结束（poll 循环退出后调用）
    (void)client->stop();
    (void)client->waitStopped();
    (void)server->stop();
    (void)server->waitStopped();
    std::printf("OK: 外部事件循环演示完成（poll 与 startBackground 二选一）\n");
    return 0;
}
