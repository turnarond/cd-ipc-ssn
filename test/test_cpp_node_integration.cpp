// 任务 5：多监听、多 Peer 与双向原始消息集成测试
//
// 拓扑（实施计划任务 5 步骤 1）：
//   node_a 监听 tcp://127.0.0.1:19101 + unix:///tmp/ssn-test-a
//   node_b 监听 tcp://127.0.0.1:19102 + unix:///tmp/ssn-test-b，主动连接 a(tcp) + a(unix)
//   node_c 主动连接 a(tcp) + b(tcp) + b(unix)
//
// 验证（步骤 2）：
//   b 通过出站 Peer 向 a 发送 {0x01,0x02}；a 回调中向事件携带的 Peer 回发 {0x03}；
//   a 广播 {0x04}，b、c 各收到一次，消息顺序保持发送顺序。
#include "ssn/node/Node.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;

std::atomic<int> passed{0};
std::atomic<int> failed{0};
#define CHECK(cond) do { if (cond) { ++passed; } else { ++failed; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

ssn::Node make_node() {
    auto result = ssn::Node::create({});
    CHECK(result.ok());
    return std::move(result).value();
}

// 事件收集器：线程安全地记录事件
struct EventLog {
    void push(const ssn::NodeEvent& event) {
        std::lock_guard<std::mutex> lock(mutex);
        entries.push_back({event.type, event.peer,
                           std::vector<std::byte>(event.message.data(),
                                                  event.message.data() + event.message.size())});
    }
    struct Entry {
        ssn::NodeEventType type;
        ssn::PeerId peer;
        std::vector<std::byte> payload;
    };
    std::vector<Entry> entries;
    mutable std::mutex mutex;

    std::size_t count_of(ssn::NodeEventType type) const {
        std::lock_guard<std::mutex> lock(mutex);
        std::size_t n = 0;
        for (const auto& e : entries) { if (e.type == type) ++n; }
        return n;
    }
    bool has_message(const std::vector<std::byte>& expected) const {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& e : entries) {
            if (e.type == ssn::NodeEventType::MessageReceived && e.payload == expected) {
                return true;
            }
        }
        return false;
    }
};

void test_three_node_topology() {
    auto a = make_node();
    auto b = make_node();
    auto c = make_node();

    CHECK(a.listen(ssn::ListenAddress{"tcp://127.0.0.1:19101"}).ok());
    CHECK(a.listen(ssn::ListenAddress{"unix:///tmp/ssn-test-a"}).ok());
    CHECK(b.listen(ssn::ListenAddress{"tcp://127.0.0.1:19102"}).ok());
    CHECK(b.listen(ssn::ListenAddress{"unix:///tmp/ssn-test-b"}).ok());

    EventLog a_log, b_log, c_log;
    a.setEventHandler([&](const ssn::NodeEvent& e) { a_log.push(e); });
    b.setEventHandler([&](const ssn::NodeEvent& e) { b_log.push(e); });
    c.setEventHandler([&](const ssn::NodeEvent& e) { c_log.push(e); });

    CHECK(a.startBackground().ok());
    CHECK(b.startBackground().ok());
    CHECK(c.startBackground().ok());

    // b → a (tcp + unix)
    auto b_to_a_tcp = b.connect(ssn::ListenAddress{"tcp://127.0.0.1:19101"});
    auto b_to_a_unix = b.connect(ssn::ListenAddress{"unix:///tmp/ssn-test-a"});
    CHECK(b_to_a_tcp.ok());
    CHECK(b_to_a_unix.ok());

    // c → a (tcp), c → b (tcp + unix)
    auto c_to_a = c.connect(ssn::ListenAddress{"tcp://127.0.0.1:19101"});
    auto c_to_b_tcp = c.connect(ssn::ListenAddress{"tcp://127.0.0.1:19102"});
    auto c_to_b_unix = c.connect(ssn::ListenAddress{"unix:///tmp/ssn-test-b"});
    CHECK(c_to_a.ok());
    CHECK(c_to_b_tcp.ok());
    CHECK(c_to_b_unix.ok());

    // 等待所有 PeerConnected 事件
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (a_log.count_of(ssn::NodeEventType::PeerConnected) >= 3 && // b_tcp, b_unix, c
            b_log.count_of(ssn::NodeEventType::PeerConnected) >= 4 && // a_tcp, a_unix, c_tcp, c_unix
            c_log.count_of(ssn::NodeEventType::PeerConnected) >= 3) { // a, b_tcp, b_unix
            break;
        }
        std::this_thread::sleep_for(10ms);
    }

    // a：3 个入站（b_tcp, b_unix, c_tcp）
    CHECK(a_log.count_of(ssn::NodeEventType::PeerConnected) == 3);
    // b：2 入站（c_tcp, c_unix）+ 2 出站（a_tcp, a_unix）
    CHECK(b_log.count_of(ssn::NodeEventType::PeerConnected) == 4);
    // c：3 出站（a, b_tcp, b_unix）
    CHECK(c_log.count_of(ssn::NodeEventType::PeerConnected) == 3);

    // Peer 数量
    CHECK(a.peers().size() == 3);
    CHECK(b.peers().size() == 4);
    CHECK(c.peers().size() == 3);

    // 方向断言
    for (const auto& p : a.peers()) {
        CHECK(p.direction == ssn::PeerDirection::Inbound);
    }
    for (const auto& p : c.peers()) {
        CHECK(p.direction == ssn::PeerDirection::Outbound);
    }

    // 双向消息：b → a 发 {0x01,0x02}，a 回调回发 {0x03}
    const std::byte msg_01[] = {std::byte{0x01}, std::byte{0x02}};
    const std::byte msg_03[] = {std::byte{0x03}};

    // a 的回调：收到 MessageReceived 时回发 {0x03}
    a.setEventHandler([&](const ssn::NodeEvent& e) {
        a_log.push(e);
        if (e.type == ssn::NodeEventType::MessageReceived) {
            a.send(e.peer, ssn::ByteView{msg_03, sizeof(msg_03)});
        }
    });

    // b 找到去 a 的出站 Peer 并发送
    CHECK(b.send(b_to_a_tcp.value(), ssn::ByteView{msg_01, sizeof(msg_01)}).ok());

    // 等 a 收到 {0x01,0x02} 且 b 收到 {0x03}
    const auto msg_deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < msg_deadline) {
        if (a_log.has_message({msg_01, msg_01 + 2}) &&
            b_log.has_message({msg_03, msg_03 + 1})) { break; }
        std::this_thread::sleep_for(10ms);
    }
    CHECK(a_log.has_message({msg_01, msg_01 + 2}));
    CHECK(b_log.has_message({msg_03, msg_03 + 1}));

    // 广播：a broadcast {0x04}，b、c 各收一次
    const std::byte msg_04[] = {std::byte{0x04}};
    CHECK(a.broadcast(ssn::ByteView{msg_04, sizeof(msg_04)}).ok());

    const auto bc_deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < bc_deadline) {
        if (b_log.has_message({msg_04, msg_04 + 1}) &&
            c_log.has_message({msg_04, msg_04 + 1})) { break; }
        std::this_thread::sleep_for(10ms);
    }
    CHECK(b_log.has_message({msg_04, msg_04 + 1}));
    CHECK(c_log.has_message({msg_04, msg_04 + 1}));

    CHECK(a.stop().ok());
    CHECK(b.stop().ok());
    CHECK(c.stop().ok());
    CHECK(a.waitStopped().ok());
    CHECK(b.waitStopped().ok());
    CHECK(c.waitStopped().ok());
}

// disconnect 语义：Closing→关闭→PeerDisconnected→回收
void test_disconnect_removes_peer() {
    auto listener = make_node();
    auto connector = make_node();

    CHECK(listener.listen(ssn::ListenAddress{"tcp://127.0.0.1:19111"}).ok());
    CHECK(listener.startBackground().ok());
    CHECK(connector.startBackground().ok());

    auto peer = connector.connect(ssn::ListenAddress{"tcp://127.0.0.1:19111"});
    CHECK(peer.ok());

    // 等连接建立
    std::this_thread::sleep_for(200ms);
    CHECK(connector.peers().size() == 1);
    CHECK(listener.peers().size() == 1);

    // disconnect
    CHECK(connector.disconnect(peer.value()).ok());
    CHECK(connector.peers().empty());
    // send 到已断开的 Peer → NotFound
    const std::byte dummy[] = {std::byte{0x01}};
    CHECK(connector.send(peer.value(), ssn::ByteView{dummy, 1}).code()
          == ssn::ErrorCode::NotFound);

    CHECK(listener.stop().ok());
    CHECK(connector.stop().ok());
    CHECK(listener.waitStopped().ok());
    CHECK(connector.waitStopped().ok());
}

} // namespace

int main() {
    test_three_node_topology();
    test_disconnect_removes_peer();
    std::printf("C++ node integration results: %d/%d passed\n",
                passed.load(), passed.load() + failed.load());
    return failed.load() == 0 ? 0 : 1;
}
