// multi_peer.cpp - ssn::Node 多 Peer 拓扑与广播示例（旅程 3）
//
// 三 Node 拓扑：hub 节点同时监听两个 TCP 地址，leafA/leafB 各连其一；
// hub 广播一条消息（全部 Peer 收到），叶子再定向回复 hub——演示多监听、
// 多 Peer 注册、broadcast 与定向 send 的区别。
// 构建与运行：make run（单进程自演示，无需外部服务端）
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ssn/node/Node.hpp>

namespace {

constexpr const char* kHubAddrA = "tcp://127.0.0.1:19503";
constexpr const char* kHubAddrB = "tcp://127.0.0.1:19504";

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

// 叶子节点工厂：连接 hub 指定地址，收到的广播打印到 stdout
std::unique_ptr<ssn::Node> make_leaf(const char* name, const char* hub_addr,
                                     ssn::PeerId& out_peer) {
    auto created = ssn::Node::create(ssn::NodeConfig{});
    if (!created) { return nullptr; }
    auto node = std::make_unique<ssn::Node>(std::move(created.value()));
    node->setEventHandler([name](const ssn::NodeEvent& event) {
        if (event.type == ssn::NodeEventType::MessageReceived && !event.message.empty()) {
            const std::string text(reinterpret_cast<const char*>(event.message.data()),
                                   event.message.size());
            std::printf("[%s] 收到广播: %s\n", name, text.c_str());
        }
    });
    if (!node->startBackground()) { return nullptr; }
    auto peer = node->connect(ssn::ListenAddress{hub_addr},
                              ssn::ConnectOptions{std::chrono::milliseconds(3000)});
    if (!peer || !wait_connected(*node, peer.value())) { return nullptr; }
    out_peer = peer.value();
    return node;
}

}  // namespace

int main() {
    // 1) hub：多地址监听（同一 Node 可 listen 多次），后台驱动
    auto hub_created = ssn::Node::create(ssn::NodeConfig{});
    if (!hub_created) {
        std::fprintf(stderr, "FAIL: Node::create（hub）\n");
        return 1;
    }
    auto hub = std::make_unique<ssn::Node>(std::move(hub_created.value()));
    if (!hub->listen(ssn::ListenAddress{kHubAddrA}) ||
        !hub->listen(ssn::ListenAddress{kHubAddrB})) {
        std::fprintf(stderr, "FAIL: hub 多地址监听\n");
        return 1;
    }
    hub->setEventHandler([](const ssn::NodeEvent& event) {
        if (event.type != ssn::NodeEventType::MessageReceived || event.message.empty()) {
            return;
        }
        const std::string text(reinterpret_cast<const char*>(event.message.data()),
                               event.message.size());
        std::printf("[hub] 收到叶子回复: %s\n", text.c_str());
    });
    if (!hub->startBackground()) {
        std::fprintf(stderr, "FAIL: startBackground（hub）\n");
        return 1;
    }
    std::printf("[hub] 监听 %s 与 %s\n", kHubAddrA, kHubAddrB);

    // 2) 两片叶子各连一个监听地址
    ssn::PeerId peer_a, peer_b;
    auto leaf_a = make_leaf("leafA", kHubAddrA, peer_a);
    auto leaf_b = make_leaf("leafB", kHubAddrB, peer_b);
    if (!leaf_a || !leaf_b) {
        std::fprintf(stderr, "FAIL: 叶子节点建立失败\n");
        return 1;
    }

    // 3) hub 广播（所有 Peer 各收一份）→ 叶子分别定向回复
    const std::string news = "广播：会议 10 点开始";
    if (!hub->broadcast(to_byte_view(news))) {
        std::fprintf(stderr, "FAIL: broadcast\n");
        return 1;
    }
    std::printf("[hub] 已广播给 %zu 个 Peer: %s\n", hub->peers().size(), news.c_str());
    const std::string reply_a = "leafA 收到", reply_b = "leafB 收到";
    (void)leaf_a->send(peer_a, to_byte_view(reply_a));
    (void)leaf_b->send(peer_b, to_byte_view(reply_b));

    // 4) 等待消息流转完成（2 条广播 + 2 条回复；演示程序用固定窗口收尾）
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    (void)leaf_a->stop();
    (void)leaf_a->waitStopped();
    (void)leaf_b->stop();
    (void)leaf_b->waitStopped();
    (void)hub->stop();
    (void)hub->waitStopped();
    std::printf("OK: 多 Peer 拓扑演示完成\n");
    return 0;
}
