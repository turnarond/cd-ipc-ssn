// 任务 7：后台驱动、跨线程命令与停止语义（TDD 红—绿—重构）
// 设计依据：docs/03-设计/方案设计/2026-09-14-C++17多Peer-Node设计.md §8/§11——
// 外部 poll 与后台线程行为一致、回调安全重入、危险自等待被拒、停止竞争安全。
// 说明：计划所列“跨线程命令队列”接口在实际演进中由互斥锁 + 原子状态 + 驱动
// 唤醒方案承载（任务 3–6 落地），本套件按行为契约验收。
#include "ssn/node/Node.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

static std::atomic<int> g_passed{0};
static std::atomic<int> g_failed{0};

#define CHECK(cond)                                                    \
    do {                                                               \
        if (cond) {                                                    \
            ++g_passed;                                                \
        } else {                                                       \
            ++g_failed;                                                \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                              \
    } while (0)

namespace {

ssn::Node make_node(const ssn::NodeConfig& config) {
    auto made = ssn::Node::create(config);
    CHECK(made.ok());
    return std::move(made).value();
}

// 事件序列记录：类型 + 对端方向 + 消息字节（回调内立即拷贝）
struct EventRecord final {
    ssn::NodeEventType type;
    ssn::PeerDirection direction;
    std::vector<std::byte> message;
};

bool same_sequence(const std::vector<EventRecord>& lhs,
                   const std::vector<EventRecord>& rhs) {
    if (lhs.size() != rhs.size()) { return false; }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].type != rhs[i].type || lhs[i].direction != rhs[i].direction ||
            lhs[i].message != rhs[i].message) {
            return false;
        }
    }
    return true;
}

// 条件等待：外部模式以 poll(10ms) 驱动，后台模式以 sleep 等待
template <typename Pred>
bool run_until(ssn::Node& a, ssn::Node& b, bool background, Pred&& done,
               std::chrono::milliseconds budget) {
    const auto began = std::chrono::steady_clock::now();
    while (!done()) {
        if (std::chrono::steady_clock::now() - began > budget) { return false; }
        if (background) {
            std::this_thread::sleep_for(2ms);
        } else {
            (void)a.poll(10ms);
            (void)b.poll(10ms);
        }
    }
    return true;
}

// 步骤 1：同一场景分别在外部 poll 与后台模式执行，
// 比较事件序列中的类型、对端方向与消息内容
std::vector<EventRecord> run_consistency_scenario(std::uint16_t port,
                                                  bool background) {
    auto a = make_node(ssn::NodeConfig{});
    auto b = make_node(ssn::NodeConfig{});

    std::vector<EventRecord> a_events;
    std::vector<EventRecord> b_events;
    std::atomic<bool> replied{false};
    a.setEventHandler([&](const ssn::NodeEvent& event) {
        auto info = a.peerInfo(event.peer);
        EventRecord rec{event.type,
                        info.ok() ? info.value().direction
                                  : ssn::PeerDirection::Inbound,
                        {}};
        if (event.message.size() > 0) {
            rec.message.assign(event.message.data(),
                               event.message.data() + event.message.size());
        }
        if (event.type == ssn::NodeEventType::MessageReceived) {
            const std::vector<std::byte> reply{std::byte{0x03}};
            (void)a.send(event.peer,
                         ssn::ByteView{reply.data(), reply.size()});
            replied.store(true);
        }
        a_events.push_back(std::move(rec));
    });
    b.setEventHandler([&](const ssn::NodeEvent& event) {
        auto info = b.peerInfo(event.peer);
        EventRecord rec{event.type,
                        info.ok() ? info.value().direction
                                  : ssn::PeerDirection::Outbound,
                        {}};
        if (event.message.size() > 0) {
            rec.message.assign(event.message.data(),
                               event.message.data() + event.message.size());
        }
        b_events.push_back(std::move(rec));
    });

    // listen 必须在进入 Running 前完成（Created 态约束）
    CHECK(a.listen(ssn::ListenAddress{
        ("tcp://127.0.0.1:" + std::to_string(port)).c_str()}).ok());

    if (background) {
        CHECK(a.startBackground().ok());
        CHECK(b.startBackground().ok());
    } else {
        CHECK(a.poll(0ms).ok());
        CHECK(b.poll(0ms).ok());
    }

    auto peer_a = b.connect(ssn::ListenAddress{
        ("tcp://127.0.0.1:" + std::to_string(port)).c_str()});
    CHECK(peer_a.ok());

    bool established = false;
    CHECK(run_until(a, b, background,
                    [&] {
                        for (const auto& p : a.peers()) {
                            if (p.state == ssn::PeerState::Connected) {
                                established = true;
                            }
                        }
                        auto info = b.peerInfo(peer_a.value());
                        return established && info.ok() &&
                               info.value().state == ssn::PeerState::Connected;
                    },
                    2s));

    const std::vector<std::byte> msg1{std::byte{0x01}, std::byte{0x02}};
    CHECK(b.send(peer_a.value(),
                 ssn::ByteView{msg1.data(), msg1.size()}).ok());

    // 等待回复送达（b 收到 {0x03}）
    bool got_reply = false;
    CHECK(run_until(a, b, background,
                    [&] {
                        for (const auto& e : b_events) {
                            if (e.type == ssn::NodeEventType::MessageReceived &&
                                e.message.size() == 1 &&
                                *e.message.data() == std::byte{0x03}) {
                                got_reply = true;
                            }
                        }
                        return got_reply && replied.load();
                    },
                    2s));

    // b 主动断开；等待两端 PeerDisconnected 事件各一条
    CHECK(b.disconnect(peer_a.value()).ok());
    bool both_disconnected = false;
    CHECK(run_until(a, b, background,
                    [&] {
                        std::size_t a_disc = 0;
                        std::size_t b_disc = 0;
                        for (const auto& e : a_events) {
                            if (e.type == ssn::NodeEventType::PeerDisconnected) {
                                ++a_disc;
                            }
                        }
                        for (const auto& e : b_events) {
                            if (e.type == ssn::NodeEventType::PeerDisconnected) {
                                ++b_disc;
                            }
                        }
                        both_disconnected = (a_disc >= 1 && b_disc >= 1);
                        return both_disconnected;
                    },
                    2s));

    CHECK(a.stop().ok());
    CHECK(b.stop().ok());
    CHECK(a.waitStopped().ok());
    CHECK(b.waitStopped().ok());
    return b_events;
}

void test_external_and_background_produce_same_sequence() {
    const auto external = run_consistency_scenario(19331, false);
    const auto background = run_consistency_scenario(19332, true);

    CHECK(external.size() == 3); // PeerConnected / MessageReceived / PeerDisconnected
    CHECK(background.size() == 3);
    CHECK(same_sequence(external, background));
    if (!same_sequence(external, background)) {
        std::printf("  external=%zu background=%zu\n", external.size(),
                    background.size());
        for (std::size_t i = 0; i < external.size() && i < background.size();
             ++i) {
            std::printf("  [%zu] type %d/%d dir %d/%d msg %zu/%zu\n", i,
                        static_cast<int>(external[i].type),
                        static_cast<int>(background[i].type),
                        static_cast<int>(external[i].direction),
                        static_cast<int>(background[i].direction),
                        external[i].message.size(),
                        background[i].message.size());
        }
    }
}

// 步骤 2：MessageReceived 回调中调用 send() 与 disconnect()——
// 无死锁、回复送达、随后只产生一次 PeerDisconnected。
// 注：I4 语义 disconnect 立即关闭并丢弃未发队列，故“同一次回调内先回复
// 后断开”必丢回复；本测试拆为两次回调分别重入 send 与 disconnect。
void test_callback_reentry_send_and_disconnect_are_safe() {
    auto hub = make_node(ssn::NodeConfig{});
    auto client = make_node(ssn::NodeConfig{});

    std::atomic<int> hub_disconnects{0};
    std::atomic<int> client_disconnects{0};
    std::atomic<bool> reply_delivered{false};

    hub.setEventHandler([&](const ssn::NodeEvent& event) {
        if (event.type == ssn::NodeEventType::PeerDisconnected) {
            ++hub_disconnects;
            return;
        }
        if (event.type != ssn::NodeEventType::MessageReceived) { return; }
        const std::vector<std::byte> reply{std::byte{0x03}};
        // 第一次回调（msg1）重入 send 回复；第二次回调（msg2）重入 disconnect
        CHECK(hub.send(event.peer,
                       ssn::ByteView{reply.data(), reply.size()}).ok());
        if (event.message.size() == 1 &&
            *event.message.data() == std::byte{0x02}) {
            CHECK(hub.disconnect(event.peer).ok());
        }
    });
    client.setEventHandler([&](const ssn::NodeEvent& event) {
        if (event.type == ssn::NodeEventType::MessageReceived) {
            if (event.message.size() == 1 &&
                *event.message.data() == std::byte{0x03}) {
                reply_delivered.store(true);
                // 收到回复后发 msg2，触发 hub 回调内的 disconnect 重入
                const std::vector<std::byte> msg2{std::byte{0x02}};
                CHECK(client.send(event.peer,
                                  ssn::ByteView{msg2.data(), msg2.size()}).ok());
            }
        }
        if (event.type == ssn::NodeEventType::PeerDisconnected) {
            ++client_disconnects;
        }
    });

    CHECK(hub.listen(ssn::ListenAddress{"tcp://127.0.0.1:19333"}).ok());
    CHECK(hub.startBackground().ok());
    CHECK(client.startBackground().ok());
    auto peer = client.connect(ssn::ListenAddress{"tcp://127.0.0.1:19333"});
    CHECK(peer.ok());

    bool established = false;
    const auto began = std::chrono::steady_clock::now();
    while (!established && std::chrono::steady_clock::now() - began < 2s) {
        std::this_thread::sleep_for(2ms);
        auto info = client.peerInfo(peer.value());
        established = info.ok() &&
                      info.value().state == ssn::PeerState::Connected;
    }
    CHECK(established);

    const std::vector<std::byte> msg{std::byte{0x01}};
    CHECK(client.send(peer.value(), ssn::ByteView{msg.data(), msg.size()}).ok());

    const auto reply_began = std::chrono::steady_clock::now();
    while (!reply_delivered.load() &&
           std::chrono::steady_clock::now() - reply_began < 2s) {
        std::this_thread::sleep_for(2ms);
    }
    CHECK(reply_delivered.load());

    // 等待断开结论事件：两端各恰好一次
    const auto disc_began = std::chrono::steady_clock::now();
    while ((hub_disconnects.load() < 1 || client_disconnects.load() < 1) &&
           std::chrono::steady_clock::now() - disc_began < 2s) {
        std::this_thread::sleep_for(2ms);
    }
    CHECK(client_disconnects.load() == 1);
    CHECK(hub_disconnects.load() == 1);

    CHECK(hub.stop().ok());
    CHECK(client.stop().ok());
    CHECK(hub.waitStopped().ok());
    CHECK(client.waitStopped().ok());
}

// 步骤 3：回调中 waitStopped() 必须立即返回 WouldDeadlock，
// poll() 必须返回 InvalidState（后台与外部两种模式）
void test_dangerous_self_wait_rejected_in_callback() {
    // 后台模式
    {
        auto node = make_node(ssn::NodeConfig{});
        std::atomic<bool> checked{false};
        ssn::ErrorCode wait_code = ssn::ErrorCode::Ok;
        ssn::ErrorCode poll_code = ssn::ErrorCode::Ok;
        node.setEventHandler([&](const ssn::NodeEvent&) {
            wait_code = node.waitStopped().code();
            poll_code = node.poll(0ms).code();
            checked.store(true);
        });
        // listen 必须在进入 Running 前完成（Created 态约束）
        CHECK(node.listen(ssn::ListenAddress{"tcp://127.0.0.1:19334"}).ok());
        CHECK(node.startBackground().ok());
        auto self_peer =
            node.connect(ssn::ListenAddress{"tcp://127.0.0.1:19334"});
        CHECK(self_peer.ok());
        const auto began = std::chrono::steady_clock::now();
        while (!checked.load() &&
               std::chrono::steady_clock::now() - began < 2s) {
            std::this_thread::sleep_for(2ms);
        }
        CHECK(checked.load());
        CHECK(wait_code == ssn::ErrorCode::WouldDeadlock);
        CHECK(poll_code == ssn::ErrorCode::InvalidState);
        CHECK(node.stop().ok());
        CHECK(node.waitStopped().ok());
    }
    // 外部模式
    {
        auto node = make_node(ssn::NodeConfig{});
        std::atomic<bool> checked{false};
        ssn::ErrorCode wait_code = ssn::ErrorCode::Ok;
        ssn::ErrorCode poll_code = ssn::ErrorCode::Ok;
        node.setEventHandler([&](const ssn::NodeEvent&) {
            wait_code = node.waitStopped().code();
            poll_code = node.poll(0ms).code();
            checked.store(true);
        });
        CHECK(node.listen(ssn::ListenAddress{"tcp://127.0.0.1:19335"}).ok());
        CHECK(node.poll(0ms).ok());
        auto self_peer =
            node.connect(ssn::ListenAddress{"tcp://127.0.0.1:19335"});
        CHECK(self_peer.ok());
        const auto began = std::chrono::steady_clock::now();
        while (!checked.load() &&
               std::chrono::steady_clock::now() - began < 2s) {
            (void)node.poll(10ms);
        }
        CHECK(checked.load());
        CHECK(wait_code == ssn::ErrorCode::WouldDeadlock);
        CHECK(poll_code == ssn::ErrorCode::InvalidState);
        CHECK(node.stop().ok());
        CHECK(node.waitStopped().ok());
    }
}

// 步骤 4：8 线程并发发送 + 另一线程重复 stop()——所有线程返回、
// Node 最终 Stopped、析构后无回调（以哨兵生命周期探测）
void test_stop_race_with_concurrent_senders_is_contained() {
    auto target = make_node(ssn::NodeConfig{});
    auto peer_node = make_node(ssn::NodeConfig{});

    // 哨兵：handler 闭包持有 shared_ptr；Node 析构后若仍有回调运行，
    // 哨兵存活期会被延长——析构后 expired()==true 即“析构后无回调”
    auto sentinel = std::make_shared<int>(0);
    std::weak_ptr<int> sentinel_watch = sentinel;
    std::atomic<int> callbacks{0};

    target.setEventHandler([&target, sentinel, &callbacks]
                               (const ssn::NodeEvent& event) {
        ++callbacks;
        if (event.type == ssn::NodeEventType::MessageReceived) {
            const std::vector<std::byte> ack{std::byte{0x06}};
            (void)target.send(event.peer,
                              ssn::ByteView{ack.data(), ack.size()});
        }
    });

    CHECK(target.listen(ssn::ListenAddress{"tcp://127.0.0.1:19336"}).ok());
    CHECK(target.startBackground().ok());
    CHECK(peer_node.startBackground().ok());
    auto peer = peer_node.connect(ssn::ListenAddress{"tcp://127.0.0.1:19336"});
    CHECK(peer.ok());

    bool established = false;
    const auto began = std::chrono::steady_clock::now();
    while (!established && std::chrono::steady_clock::now() - began < 2s) {
        std::this_thread::sleep_for(2ms);
        auto info = peer_node.peerInfo(peer.value());
        established = info.ok() &&
                      info.value().state == ssn::PeerState::Connected;
    }
    CHECK(established);

    // 8 线程并发发送；1 线程反复 stop()（幂等）
    constexpr int kSenders = 8;
    constexpr int kSendsPerSender = 50;
    std::vector<std::thread> senders;
    for (int t = 0; t < kSenders; ++t) {
        senders.emplace_back([&peer_node, peer = peer.value()] {
            const std::vector<std::byte> payload{std::byte{0x01},
                                                 std::byte{0x02}};
            for (int i = 0; i < kSendsPerSender; ++i) {
                // 停止后 send 返回 InvalidState 属合法结果，不计失败
                (void)peer_node.send(
                    peer, ssn::ByteView{payload.data(), payload.size()});
                std::this_thread::yield();
            }
        });
    }
    std::thread stopper([&target] {
        for (int i = 0; i < 200; ++i) {
            (void)target.stop();
            std::this_thread::yield();
        }
    });
    for (auto& t : senders) { t.join(); }
    stopper.join();

    CHECK(target.stop().ok());
    CHECK(target.waitStopped().ok());
    CHECK(peer_node.stop().ok());
    CHECK(peer_node.waitStopped().ok());
    CHECK(target.peers().empty()); // 停止冲刷后 Peer 全部回收

    // 析构后无回调：释放 Node 与 handler 持有的哨兵，短暂窗口内不得复燃
    sentinel.reset();
    {
        ssn::Node discard_a = std::move(target);
        ssn::Node discard_b = std::move(peer_node);
    } // 析构点：handler 随 Impl 释放，哨兵应在此归零
    std::this_thread::sleep_for(100ms);
    CHECK(sentinel_watch.expired());
}

}  // namespace

int main() {
    test_external_and_background_produce_same_sequence();
    test_callback_reentry_send_and_disconnect_are_safe();
    test_dangerous_self_wait_rejected_in_callback();
    test_stop_race_with_concurrent_senders_is_contained();
    std::printf("C++ node concurrency results: %d/%d passed\n",
                g_passed.load(), g_passed.load() + g_failed.load());
    return g_failed.load() == 0 ? 0 : 1;
}
