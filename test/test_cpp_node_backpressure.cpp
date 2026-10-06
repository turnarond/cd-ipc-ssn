// 任务 6：发送队列、背压与慢 Peer 隔离（TDD 红—绿—重构）
// 设计依据：docs/03-设计/方案设计/2026-09-14-C++17多Peer-Node设计.md §9——
// 单 Peer 队列上限、Node 总队列上限、高低水位单次触发、慢 Peer 只阻塞自身。
// 全部用外部驱动模式：poll 时机由测试掌控，发送积压确定性地滞留在用户态队列。
#include "ssn/node/Node.hpp"
#include "ssn_frame.h" // SSN_HEADER_SIZE / SSN_MAX_PAYLOAD_SIZE

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

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

// 裸 TCP 对端：accept 后可选择不读（制造慢 Peer）或精确读（控制排空节奏）
struct RawTcpPeer final {
    int listener = -1;
    int conn = -1;

    explicit RawTcpPeer(std::uint16_t port) {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        CHECK(listener >= 0);
        const int yes = 1;
        CHECK(::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes,
                           sizeof(yes)) == 0);
        const int flags = ::fcntl(listener, F_GETFL, 0);
        CHECK(flags >= 0 && ::fcntl(listener, F_SETFL, flags | O_NONBLOCK) >= 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK(::bind(listener, reinterpret_cast<sockaddr*>(&addr),
                     sizeof(addr)) == 0);
        CHECK(::listen(listener, 1) == 0);
    }

    bool accept_conn() {
        for (int i = 0; i < 500 && conn < 0; ++i) {
            conn = ::accept(listener, nullptr, nullptr);
            if (conn >= 0) {
                const timeval tv{2, 0}; // 读超时兜底，防测试进程挂死
                ::setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                return true;
            }
            usleep(2000);
        }
        return false;
    }

    ~RawTcpPeer() {
        if (conn >= 0) { ::close(conn); }
        if (listener >= 0) { ::close(listener); }
    }
};

namespace {

ssn::Node make_node(const ssn::NodeConfig& config) {
    auto made = ssn::Node::create(config);
    CHECK(made.ok());
    return std::move(made).value();
}

// 外部模式连接裸监听端并轮询到 Connected；轮次随调度变化，循环内不计断言
ssn::PeerId connect_raw_listener(ssn::Node& node, std::uint16_t port,
                                 RawTcpPeer& raw) {
    const std::string addr = "tcp://127.0.0.1:" + std::to_string(port);
    auto peer = node.connect(ssn::ListenAddress{addr.c_str()});
    CHECK(peer.ok());
    CHECK(raw.accept_conn());
    bool connected = false;
    const auto began = std::chrono::steady_clock::now();
    while (!connected && std::chrono::steady_clock::now() - began < 2s) {
        (void)node.poll(10ms);
        auto info = node.peerInfo(peer.value());
        connected = info.ok() && info.value().state == ssn::PeerState::Connected;
    }
    CHECK(connected);
    return peer.value();
}

std::size_t queued_of(const ssn::Node& node, ssn::PeerId peer) {
    auto info = node.peerInfo(peer);
    return info.ok() ? info.value().queued_bytes : 0;
}

// 精确读 total 字节（flush 后数据应全部到达；2s 兜底防挂死）
bool read_exact(int fd, std::size_t total) {
    std::size_t got = 0;
    std::vector<char> buf(4096);
    const auto began = std::chrono::steady_clock::now();
    while (got < total) {
        if (std::chrono::steady_clock::now() - began > 2s) { return false; }
        const ssize_t n = ::read(fd, buf.data(), buf.size());
        if (n > 0) {
            got += static_cast<std::size_t>(n);
        } else if (n < 0 &&
                   (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            usleep(1000);
        } else {
            return false;
        }
    }
    return true;
}

// 步骤 1：硬上限——单 Peer 队列越过 max_peer_queue_bytes 时 send 返回
// QueueFull，且已入队字节不得被静默丢弃或继续增长
void test_hard_cap_returns_queue_full() {
    RawTcpPeer raw(19321);
    ssn::NodeConfig config;
    config.max_peer_queue_bytes = 1024;
    // create 校验 high ≤ 单 Peer 上限：小队列必须同步压低水位
    config.queue_high_watermark_bytes = 768;
    config.queue_low_watermark_bytes = 256;
    auto node = make_node(config);
    CHECK(node.poll(0ms).ok()); // 进入 Running
    const ssn::PeerId peer = connect_raw_listener(node, 19321, raw);

    const std::vector<std::byte> payload800(800, std::byte{0x5A});
    CHECK(node.send(peer,
                    ssn::ByteView{payload800.data(), payload800.size()}).ok());
    CHECK(queued_of(node, peer) == 800 + SSN_HEADER_SIZE);

    // 800 + 400 > 1024：第二次必须 QueueFull，队列保持不变
    const std::vector<std::byte> payload400(400, std::byte{0x5B});
    CHECK(node.send(peer,
                    ssn::ByteView{payload400.data(), payload400.size()}).code() ==
          ssn::ErrorCode::QueueFull);
    CHECK(queued_of(node, peer) == 800 + SSN_HEADER_SIZE);

    // 恰好填满剩余额度（扣除本帧头部）应成功，此后再发 1 字节仍 QueueFull
    const std::size_t rest =
        1024 - (800 + SSN_HEADER_SIZE) - SSN_HEADER_SIZE;
    const std::vector<std::byte> payload_rest(rest, std::byte{0x5C});
    CHECK(node.send(peer,
                    ssn::ByteView{payload_rest.data(), payload_rest.size()}).ok());
    CHECK(queued_of(node, peer) == 1024);
    const std::vector<std::byte> one{std::byte{0x01}};
    CHECK(node.send(peer, ssn::ByteView{one.data(), one.size()}).code() ==
          ssn::ErrorCode::QueueFull);

    CHECK(node.stop().ok());
    CHECK(node.waitStopped().ok());
}

// 步骤 2：高低水位只触发一次——首次越过高水位产生一条 BackpressureOn，
// 持续高位不重复；降至低水位后产生一条 BackpressureOff 并可重新武装
void test_watermark_events_fire_once() {
    RawTcpPeer raw(19322);
    ssn::NodeConfig config;
    config.max_peer_queue_bytes = 4096;
    config.queue_high_watermark_bytes = 768;
    config.queue_low_watermark_bytes = 256;
    auto node = make_node(config);
    CHECK(node.poll(0ms).ok());
    const ssn::PeerId peer = connect_raw_listener(node, 19322, raw);

    std::vector<ssn::NodeEvent> bp_events;
    node.setEventHandler([&](const ssn::NodeEvent& event) {
        if (event.type == ssn::NodeEventType::BackpressureOn ||
            event.type == ssn::NodeEventType::BackpressureOff) {
            bp_events.push_back(event);
        }
    });

    // 三次发送都在不 poll 的窗口内完成：队列持续高位，只允许一条 On
    const std::vector<std::byte> payload800(800, std::byte{0x5A});
    const std::vector<std::byte> payload100(100, std::byte{0x5B});
    CHECK(node.send(peer,
                    ssn::ByteView{payload800.data(), payload800.size()}).ok());
    CHECK(node.send(peer,
                    ssn::ByteView{payload100.data(), payload100.size()}).ok());
    CHECK(node.send(peer,
                    ssn::ByteView{payload100.data(), payload100.size()}).ok());

    // 派发事件并冲刷排空：应恰好一条 On（快照=首帧入队后字节数）+ 一条 Off
    const auto began = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - began < 2s) {
        (void)node.poll(10ms);
        std::size_t ons = 0;
        std::size_t offs = 0;
        for (const auto& e : bp_events) {
            if (e.type == ssn::NodeEventType::BackpressureOn) { ++ons; }
            if (e.type == ssn::NodeEventType::BackpressureOff) { ++offs; }
        }
        if (ons >= 1 && offs >= 1) { break; }
    }
    std::size_t ons = 0;
    std::size_t offs = 0;
    ssn::PeerId on_peer{};
    std::size_t on_queued = 0;
    std::size_t off_queued = 0;
    for (const auto& e : bp_events) {
        if (e.type == ssn::NodeEventType::BackpressureOn) {
            ++ons;
            on_peer = e.peer;
            on_queued = e.queued_bytes;
        }
        if (e.type == ssn::NodeEventType::BackpressureOff) {
            ++offs;
            off_queued = e.queued_bytes;
        }
    }
    CHECK(ons == 1);
    CHECK(offs == 1);
    CHECK(on_peer == peer);
    CHECK(on_queued == 800 + SSN_HEADER_SIZE);
    CHECK(off_queued <= 256);

    // 排空后重新武装：再次越过高水位允许新的一条 On（事件语义按“回合”计）
    const std::size_t bytes_before =
        (800 + SSN_HEADER_SIZE) + 2 * (100 + SSN_HEADER_SIZE);
    CHECK(read_exact(raw.conn, bytes_before));
    CHECK(node.send(peer,
                    ssn::ByteView{payload800.data(), payload800.size()}).ok());
    const auto began2 = std::chrono::steady_clock::now();
    while (ons < 2 && std::chrono::steady_clock::now() - began2 < 2s) {
        (void)node.poll(10ms);
        std::size_t now_ons = 0;
        for (const auto& e : bp_events) {
            if (e.type == ssn::NodeEventType::BackpressureOn) { ++now_ons; }
        }
        ons = now_ons;
    }
    CHECK(ons == 2);

    CHECK(node.stop().ok());
    CHECK(node.waitStopped().ok());
}

// 设计 §9：Node 总发送队列上限——跨 Peer 计入，超限 QueueFull；
// 断开 Peer 后其未发字节一次性归还总计数
void test_total_cap_shared_across_peers_and_reclaimed_on_close() {
    RawTcpPeer raw1(19323);
    RawTcpPeer raw2(19324);
    ssn::NodeConfig config;
    config.max_total_queue_bytes = 4096; // 单 Peer 上限保持默认 1MiB
    auto node = make_node(config);
    CHECK(node.poll(0ms).ok());
    const ssn::PeerId peer1 = connect_raw_listener(node, 19323, raw1);
    const ssn::PeerId peer2 = connect_raw_listener(node, 19324, raw2);

    const std::vector<std::byte> payload1500(1500, std::byte{0x5A});
    const std::vector<std::byte> payload2000(2000, std::byte{0x5B});
    const std::vector<std::byte> payload1000(1000, std::byte{0x5C});
    CHECK(node.send(peer1,
                    ssn::ByteView{payload1500.data(), payload1500.size()}).ok());
    CHECK(node.send(peer2,
                    ssn::ByteView{payload2000.data(), payload2000.size()}).ok());
    CHECK(queued_of(node, peer1) == 1500 + SSN_HEADER_SIZE);
    CHECK(queued_of(node, peer2) == 2000 + SSN_HEADER_SIZE);

    // 1529 + 1029 = 2558 → 总计 4587 > 4096：必须 QueueFull
    CHECK(node.send(peer2,
                    ssn::ByteView{payload1000.data(), payload1000.size()}).code() ==
          ssn::ErrorCode::QueueFull);
    CHECK(queued_of(node, peer2) == 2000 + SSN_HEADER_SIZE);

    // 断开 peer1 归还 1529 字节后，同一笔发送应当成功
    CHECK(node.disconnect(peer1).ok());
    CHECK(node.send(peer2,
                    ssn::ByteView{payload1000.data(), payload1000.size()}).ok());
    CHECK(queued_of(node, peer2) == 2000 + SSN_HEADER_SIZE + 1000 + SSN_HEADER_SIZE);

    CHECK(node.stop().ok());
    CHECK(node.waitStopped().ok());
}

// 步骤 3：慢 Peer 不阻塞健康 Peer——慢 Peer 队列顶满且不读，健康 Peer
// 仍需在 1 秒内完成 100 次小消息往返
void test_slow_peer_does_not_block_healthy_peer() {
    ssn::NodeConfig hub_config; // 默认上限：单 Peer 1MiB / 总 16MiB
    auto hub_made = ssn::Node::create(hub_config);
    CHECK(hub_made.ok());
    ssn::Node hub = std::move(hub_made).value();
    // hub 只监听健康通道；慢通道由 RawTcpPeer 持监听端，hub 作为客户端外连
    CHECK(hub.listen(ssn::ListenAddress{"tcp://127.0.0.1:19326"}).ok());

    auto healthy_made = ssn::Node::create(ssn::NodeConfig{});
    CHECK(healthy_made.ok());
    ssn::Node healthy = std::move(healthy_made).value();

    hub.setEventHandler([&](const ssn::NodeEvent& event) {
        if (event.type == ssn::NodeEventType::MessageReceived) {
            (void)hub.send(event.peer,
                           ssn::ByteView{event.message.data(),
                                         event.message.size()});
        }
    });

    CHECK(hub.startBackground().ok());
    CHECK(healthy.startBackground().ok());

    RawTcpPeer slow_raw(19325); // 慢 Peer：accept 后永不读取
    auto slow_conn = hub.connect(ssn::ListenAddress{"tcp://127.0.0.1:19325"});
    CHECK(slow_conn.ok());
    CHECK(slow_raw.accept_conn());
    auto hub_peer = healthy.connect(ssn::ListenAddress{"tcp://127.0.0.1:19326"});
    CHECK(hub_peer.ok());

    // 等三方连接就绪（轮询快照，不计断言）
    bool ready = false;
    const auto began = std::chrono::steady_clock::now();
    while (!ready && std::chrono::steady_clock::now() - began < 2s) {
        usleep(2000);
        bool hub_ready = false;
        for (const auto& p : hub.peers()) {
            if (p.state == ssn::PeerState::Connected) { hub_ready = true; }
        }
        auto hinfo = healthy.peerInfo(hub_peer.value());
        ready = hub_ready && hinfo.ok() &&
                hinfo.value().state == ssn::PeerState::Connected;
    }
    std::fprintf(stderr, "DBG ready=%d hub_peers=%zu hub_states=", ready,
                 hub.peers().size());
    for (const auto& p : hub.peers()) {
        std::fprintf(stderr, "%d/", static_cast<int>(p.state));
    }
    auto hdbg = healthy.peerInfo(hub_peer.value());
    std::fprintf(stderr, " healthy_ok=%d state=%d\n", hdbg.ok(),
                 hdbg.ok() ? static_cast<int>(hdbg.value().state) : -1);
    CHECK(ready);

    // 顶满慢 Peer 队列：64KiB 帧连发直到 QueueFull 或滞留字节连续 3 次不减少
    const std::vector<std::byte> big(64 * 1024, std::byte{0x5A});
    std::size_t queued = 0;
    std::size_t last = 0;
    int stable = 0;
    for (int i = 0; i < 200; ++i) {
        auto status = hub.send(slow_conn.value(),
                               ssn::ByteView{big.data(), big.size()});
        if (!status.ok()) { break; }
        usleep(5000);
        auto info = hub.peerInfo(slow_conn.value());
        queued = info.ok() ? info.value().queued_bytes : 0;
        if (queued > 0 && queued >= last) {
            if (++stable >= 3) { break; }
        } else {
            stable = 0;
        }
        last = queued;
    }
    CHECK(queued > 0);

    // 健康 Peer 串行往返 100 次；1 秒兜底即计划验收口径
    std::atomic<int> round_trips{0};
    const std::vector<std::byte> req{std::byte{0x01}};
    healthy.setEventHandler([&](const ssn::NodeEvent& event) {
        if (event.type != ssn::NodeEventType::MessageReceived) { return; }
        if (round_trips.fetch_add(1) + 1 < 100) {
            (void)healthy.send(hub_peer.value(),
                               ssn::ByteView{req.data(), req.size()});
        }
    });
    CHECK(healthy.send(hub_peer.value(),
                       ssn::ByteView{req.data(), req.size()}).ok());
    const auto rtt_began = std::chrono::steady_clock::now();
    while (round_trips.load() < 100 &&
           std::chrono::steady_clock::now() - rtt_began < 1s) {
        usleep(1000);
    }
    CHECK(round_trips.load() == 100);

    CHECK(hub.stop().ok());
    CHECK(healthy.stop().ok());
    CHECK(hub.waitStopped().ok());
    CHECK(healthy.waitStopped().ok());
}

}  // namespace

int main() {
    test_hard_cap_returns_queue_full();
    test_watermark_events_fire_once();
    test_total_cap_shared_across_peers_and_reclaimed_on_close();
    test_slow_peer_does_not_block_healthy_peer();
    std::printf("C++ node backpressure results: %d/%d passed\n",
                g_passed.load(), g_passed.load() + g_failed.load());
    return g_failed.load() == 0 ? 0 : 1;
}
