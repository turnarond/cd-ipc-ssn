// duplex_message.cpp - ssn::Node 回调内双向回复示例（旅程 2）
//
// 演示 MessageReceived 回调中的重入 send：应答方在事件回调里直接回发消息
// （回调不持 Node 内部锁，重入 send 安全），请求方在另一节点收到回复——
// 同一进程内完成一问一答。
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

constexpr const char* kResponderAddr = "tcp://127.0.0.1:19502";

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

// std::string → MESSAGE 帧字节视图
ssn::ByteView to_byte_view(const std::string& text) {
    return ssn::ByteView{reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

}  // namespace

int main() {
    // 1) 应答方：收到消息后在回调内直接回发（重入 send 安全，不持内部锁）
    auto rsp_created = ssn::Node::create(ssn::NodeConfig{});
    if (!rsp_created) {
        std::fprintf(stderr, "FAIL: Node::create（responder）\n");
        return 1;
    }
    auto responder = std::make_unique<ssn::Node>(std::move(rsp_created.value()));
    if (!responder->listen(ssn::ListenAddress{kResponderAddr})) {
        std::fprintf(stderr, "FAIL: listen %s\n", kResponderAddr);
        return 1;
    }
    // 捕获裸指针（responder 由 unique_ptr 持有至 main 结尾，回调期间存活有保证）
    responder->setEventHandler([raw = responder.get()](const ssn::NodeEvent& event) {
        if (event.type != ssn::NodeEventType::MessageReceived || event.message.empty()) {
            return;
        }
        const std::string request(reinterpret_cast<const char*>(event.message.data()),
                                  event.message.size());
        std::printf("[responder] 收到: %s → 回调内直接回发\n", request.c_str());
        // MessageView 只在回调期间有效——需要跨出回调使用须 copy()；
        // 本示例在回调内即时组回复，无需拷贝
        const std::string reply = "回复: " + request;
        (void)raw->send(event.peer, to_byte_view(reply));
    });
    if (!responder->startBackground()) {
        std::fprintf(stderr, "FAIL: startBackground（responder）\n");
        return 1;
    }

    // 2) 请求方：连接 → 发问 → 事件线程收回复
    std::atomic<int> replies{0};
    auto req_created = ssn::Node::create(ssn::NodeConfig{});
    if (!req_created) {
        std::fprintf(stderr, "FAIL: Node::create（requester）\n");
        return 1;
    }
    auto requester = std::make_unique<ssn::Node>(std::move(req_created.value()));
    requester->setEventHandler([&replies](const ssn::NodeEvent& event) {
        if (event.type == ssn::NodeEventType::MessageReceived && !event.message.empty()) {
            const std::string text(reinterpret_cast<const char*>(event.message.data()),
                                   event.message.size());
            std::printf("[requester] 收到回复: %s\n", text.c_str());
            replies.fetch_add(1);
        }
    });
    if (!requester->startBackground()) {
        std::fprintf(stderr, "FAIL: startBackground（requester）\n");
        return 1;
    }
    auto peer = requester->connect(ssn::ListenAddress{kResponderAddr},
                                   ssn::ConnectOptions{std::chrono::milliseconds(3000)});
    if (!peer || !wait_connected(*requester, peer.value())) {
        std::fprintf(stderr, "FAIL: 连接未建立\n");
        return 1;
    }

    // 3) 连发两问，回调内回复原路返回
    for (int i = 1; i <= 2; ++i) {
        const std::string ask = "ping-" + std::to_string(i);
        if (!requester->send(peer.value(), to_byte_view(ask))) {
            std::fprintf(stderr, "FAIL: send %s\n", ask.c_str());
            return 1;
        }
        std::printf("[requester] 已发送: %s\n", ask.c_str());
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (replies.load() < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (replies.load() < 2) {
        std::fprintf(stderr, "FAIL: 回复不完整（%d/2）\n", replies.load());
        return 1;
    }

    (void)requester->stop();
    (void)requester->waitStopped();
    (void)responder->stop();
    (void)responder->waitStopped();
    std::printf("OK: 双向消息演示完成（回调内重入 send 安全）\n");
    return 0;
}
