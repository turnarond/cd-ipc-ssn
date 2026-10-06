// node_lifecycle.cpp - ssn::Node RAII 生命周期示例（旅程 1）
//
// 演示多 Peer 节点的完整生命周期：创建 → 监听 → 后台事件线程自驱动 →
// 出站连接 → 消息收发 → 优雅停止 → RAII 兜底析构。
// 构建与运行：make run（单进程自演示，无需外部服务端）
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <ssn/node/Node.hpp>

namespace {

constexpr const char* kHubAddr = "tcp://127.0.0.1:19501";

// connect() 的 InProgress 路径会立即返回 Connecting 态的 PeerId，此时直接
// send 会被拒绝（Peer 未在连接态）——须轮询等待握手完成（PeerState::Connected）
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

}  // namespace

int main() {
    // 1) 中心节点：创建 → 监听 → 后台事件线程驱动（listen 必须处于 Created 态）
    auto hub_created = ssn::Node::create(ssn::NodeConfig{});
    if (!hub_created) {
        std::fprintf(stderr, "FAIL: Node::create（hub）\n");
        return 1;
    }
    auto hub = std::make_unique<ssn::Node>(std::move(hub_created.value()));
    if (!hub->listen(ssn::ListenAddress{kHubAddr})) {
        std::fprintf(stderr, "FAIL: listen %s\n", kHubAddr);
        return 1;
    }
    hub->setEventHandler([](const ssn::NodeEvent& event) {
        if (event.type == ssn::NodeEventType::PeerConnected) {
            std::printf("[hub] peer 已连入\n");
        } else if (event.type == ssn::NodeEventType::MessageReceived) {
            std::string text(reinterpret_cast<const char*>(event.message.data()),
                             event.message.size());
            std::printf("[hub] 收到: %s\n", text.c_str());
        }
    });
    if (!hub->startBackground()) {
        std::fprintf(stderr, "FAIL: startBackground（hub）\n");
        return 1;
    }
    std::printf("[hub] 监听 %s（后台事件线程自驱动）\n", kHubAddr);

    // 2) 客户端节点：连接 → 等待握手完成 → 发送一条消息
    auto cli_created = ssn::Node::create(ssn::NodeConfig{});
    if (!cli_created) {
        std::fprintf(stderr, "FAIL: Node::create（client）\n");
        return 1;
    }
    auto cli = std::make_unique<ssn::Node>(std::move(cli_created.value()));
    if (!cli->startBackground()) {
        std::fprintf(stderr, "FAIL: startBackground（client）\n");
        return 1;
    }
    auto peer = cli->connect(ssn::ListenAddress{kHubAddr},
                             ssn::ConnectOptions{std::chrono::milliseconds(3000)});
    if (!peer) {
        std::fprintf(stderr, "FAIL: connect\n");
        return 1;
    }
    if (!wait_connected(*cli, peer.value())) {
        std::fprintf(stderr, "FAIL: 连接握手未完成\n");
        return 1;
    }
    std::printf("[client] 已连接 hub\n");

    const std::string hello = "你好，SSN";
    if (!cli->send(peer.value(), ssn::ByteView{
             reinterpret_cast<const std::byte*>(hello.data()), hello.size()})) {
        std::fprintf(stderr, "FAIL: send\n");
        return 1;
    }
    std::printf("[client] 已发送: %s\n", hello.c_str());

    // 给事件线程一点时间完成投递与打印（演示程序，非严格同步）
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // 3) 优雅停止：stop 停事件线程 → waitStopped 等待收尾完成
    std::printf("[client] stop + waitStopped\n");
    (void)cli->stop();
    (void)cli->waitStopped();
    std::printf("[hub] stop + waitStopped\n");
    (void)hub->stop();
    (void)hub->waitStopped();

    // 4) RAII 兜底：即使上面遗漏了 stop/waitStopped，unique_ptr 析构也会
    //    安全回收节点资源（不得在事件回调内析构仍在运行的 Node）
    cli.reset();
    hub.reset();
    std::printf("OK: 生命周期演示完成\n");
    return 0;
}
