// 任务 4：私有非阻塞传输与聚合轮询后端
//
// 覆盖两项后端契约：
//   1. connect() 必须立即返回有效 PeerId，连接失败经后续 poll() 以
//      ConnectFailed 事件上报（而非在 connect() 内阻塞等待）。
//   2. poll() 阻塞等待期间，其他线程的 stop() 必须唤醒它（WakeupHandle），
//      poll 在 500ms 内返回且 Node 最终进入 Stopped。
#include "ssn/node/Node.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
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

void test_connect_returns_immediately_and_reports_failure() {
    auto node = make_node();
    CHECK(node.poll(0ms).ok()); // 首次 poll 固定外部驱动模式

    std::vector<ssn::NodeEvent> events;
    node.setEventHandler([&](const ssn::NodeEvent& event) { events.push_back(event); });

    const auto began = std::chrono::steady_clock::now();
    // 127.0.0.1:1 无监听者，内核立即回 RST：连接必失败，但失败不得阻塞 connect()。
    auto peer = node.connect(ssn::ListenAddress{"tcp://127.0.0.1:1"});
    const auto elapsed = std::chrono::steady_clock::now() - began;

    CHECK(peer.ok());
    CHECK(elapsed < 20ms);
    if (!peer.ok()) { return; } // 未实现后端时不再取 value()（取失败值会抛异常）
    CHECK(peer.value().valid());

    bool reported = false;
    for (int round = 0; round < 200 && !reported; ++round) {
        node.poll(10ms);
        for (const auto& event : events) {
            if (event.type == ssn::NodeEventType::PeerDisconnected ||
                event.type == ssn::NodeEventType::Error) {
                if (event.peer == peer.value() &&
                    event.status.code() == ssn::ErrorCode::ConnectFailed) {
                    reported = true;
                }
            }
        }
    }
    CHECK(reported);
    CHECK(node.peers().empty());
}

void test_stop_wakes_blocked_poll() {
    auto node = make_node();
    CHECK(node.poll(0ms).ok());

    auto polling = std::async(std::launch::async, [&] { return node.poll(5s); });
    std::this_thread::sleep_for(50ms);
    CHECK(node.stop().ok());
    CHECK(polling.wait_for(500ms) == std::future_status::ready);
    CHECK(polling.get().ok());
    CHECK(node.waitStopped().ok());
}
}

int main() {
    test_connect_returns_immediately_and_reports_failure();
    test_stop_wakes_blocked_poll();
    std::printf("C++ node backend results: %d/%d passed\n",
                passed.load(), passed.load() + failed.load());
    return failed.load() == 0 ? 0 : 1;
}
