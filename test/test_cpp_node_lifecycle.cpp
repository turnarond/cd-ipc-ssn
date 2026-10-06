#include "ssn/node/Node.hpp"
#include "node/NodeImpl.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/resource.h>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <vector>

#include "ssn_frame.h"

static_assert(!std::is_copy_constructible_v<ssn::Node>);
static_assert(!std::is_copy_assignable_v<ssn::Node>);
static_assert(std::is_nothrow_move_constructible_v<ssn::Node>);
static_assert(std::is_nothrow_move_assignable_v<ssn::Node>);
static_assert(std::is_nothrow_destructible_v<ssn::Node>);
static_assert(sizeof(ssn::Node) == sizeof(void*));

// 测试专用访问口只在此处编译，不进入库、不随安装分发。
namespace ssn::detail {
struct NodeTestAccess {
    static NodeState state(const Node& node) {
        std::lock_guard<std::mutex> lock(node.impl_->mutex);
        return node.impl_->state;
    }
    static Status inject(Node& node, NodeEvent event) {
        return node.impl_->events.push(std::move(event));
    }
    static void emit(Node& node, NodeEventType type, PeerId peer, Status status) {
        node.impl_->emitEvent(type, peer, std::move(status));
    }
    static ErrorCode diagnostic(const Node& node) {
        return node.impl_->callback_error.load();
    }
    static std::string_view address(const Node& node) {
        return node.impl_->listen_address.address.view();
    }
};
}

namespace {
using namespace std::chrono_literals;
using Access = ssn::detail::NodeTestAccess;
std::atomic<int> passed{0};
std::atomic<int> failed{0};
#define CHECK(cond) do { if (cond) { ++passed; } else { ++failed; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

ssn::Node make_node() {
    auto result = ssn::Node::create({});
    CHECK(result.ok());
    return std::move(result).value();
}

void test_configuration_and_created_stop() {
    ssn::NodeConfig invalid;
    invalid.max_peers = 0;
    CHECK(ssn::Node::create(invalid).status().code() == ssn::ErrorCode::InvalidArgument);
    auto node = make_node();
    CHECK(Access::state(node) == ssn::NodeState::Created);
    ssn::ListenAddress address{"unix:///tmp/ssn-memory-only"};
    CHECK(node.listen(address).ok());
    address.address = "changed";
    CHECK(Access::address(node) == "unix:///tmp/ssn-memory-only");
    CHECK(node.peers().empty());
    CHECK(node.peerInfo({0, 1}).status().code() == ssn::ErrorCode::NotFound);
    CHECK(node.connect(address).status().code() == ssn::ErrorCode::InvalidState);
    CHECK(node.send({0, 1}, {}).code() == ssn::ErrorCode::NotFound);
    CHECK(node.disconnect({0, 1}).code() == ssn::ErrorCode::NotFound);
    CHECK(node.broadcast({}).code() == ssn::ErrorCode::InvalidState);
    CHECK(node.stop().ok());
    CHECK(Access::state(node) == ssn::NodeState::Stopped);
    CHECK(node.stop().ok());
    CHECK(node.waitStopped().ok());
    CHECK(node.waitStopped().ok());
    CHECK(node.listen(address).code() == ssn::ErrorCode::InvalidState);
    CHECK(node.startBackground().code() == ssn::ErrorCode::InvalidState);
}

void test_external_mode_and_stop_transition() {
    auto node = make_node();
    CHECK(node.poll(0ms).ok());
    CHECK(Access::state(node) == ssn::NodeState::Running);
    CHECK(node.poll(0ms).ok());
    CHECK(node.startBackground().code() == ssn::ErrorCode::InvalidState);
    CHECK(node.listen({"unix:///tmp/unavailable"}).code() == ssn::ErrorCode::InvalidState);
    CHECK(node.stop().ok());
    CHECK(node.stop().ok());
    CHECK(Access::state(node) == ssn::NodeState::Stopping);
    CHECK(node.poll(0ms).ok());
    CHECK(Access::state(node) == ssn::NodeState::Stopped);
    CHECK(node.waitStopped().ok());
    CHECK(node.waitStopped().ok());
}

// 外部驱动模式下 stop() 之后不再 poll 时，waitStopped() 必须自己完成
// Stopping→Stopped，否则「poll → stop → waitStopped」这一惯用关闭序列会挂死
// （实施计划任务 3 步骤 2 的验收代码即此序列）。
void test_stop_then_wait_without_extra_poll() {
    auto node = make_node();
    CHECK(node.poll(0ms).ok());
    CHECK(node.startBackground().code() == ssn::ErrorCode::InvalidState);
    CHECK(node.stop().ok());
    CHECK(node.stop().ok());
    CHECK(node.waitStopped().ok());
    CHECK(Access::state(node) == ssn::NodeState::Stopped);
}

void test_background_mode_stop_and_wait() {
    auto node = make_node();
    CHECK(node.startBackground().ok());
    CHECK(Access::state(node) == ssn::NodeState::Running);
    CHECK(node.startBackground().code() == ssn::ErrorCode::InvalidState);
    CHECK(node.poll(0ms).code() == ssn::ErrorCode::InvalidState);
    auto other = std::async(std::launch::async, [&] { return node.poll(0ms).code(); });
    CHECK(other.get() == ssn::ErrorCode::InvalidState);
    CHECK(node.stop().ok());
    CHECK(node.stop().ok());
    CHECK(node.waitStopped().ok());
    CHECK(Access::state(node) == ssn::NodeState::Stopped);
    CHECK(node.waitStopped().ok());
    CHECK(node.poll(0ms).code() == ssn::ErrorCode::InvalidState);
}

// 若回调派发持锁，本用例的「替换 handler + 停止」路径会死锁。
void test_event_order_reentry_and_handler_replacement() {
    auto node = make_node();
    std::vector<std::size_t> received;
    node.setEventHandler([&](const ssn::NodeEvent& event) {
        received.push_back(event.queued_bytes);
        CHECK(node.peers().empty());
        node.setEventHandler([&](const ssn::NodeEvent& next) {
            received.push_back(next.queued_bytes + 10);
            CHECK(node.stop().ok());
        });
        ssn::NodeEvent later;
        later.queued_bytes = 3;
        Access::inject(node, std::move(later));
    });
    for (const auto value : {1U, 2U}) {
        ssn::NodeEvent event;
        event.queued_bytes = value;
        Access::inject(node, std::move(event));
    }
    CHECK(node.poll(0ms).ok());
    CHECK((received == std::vector<std::size_t>{1, 12}));
    CHECK(node.poll(0ms).ok());
    CHECK(Access::state(node) == ssn::NodeState::Stopped);
    CHECK(node.waitStopped().ok());
}

void test_callback_exception_is_recorded_and_next_event_runs() {
    auto node = make_node();
    int calls = 0;
    node.setEventHandler([&](const ssn::NodeEvent&) {
        if (++calls == 1) { throw std::runtime_error("user failure"); }
    });
    Access::inject(node, {});
    Access::inject(node, {});
    CHECK(node.poll(0ms).ok());
    CHECK(calls == 2);
    CHECK(Access::diagnostic(node) == ssn::ErrorCode::CallbackError);
    node.setEventHandler({});
    Access::inject(node, {});
    CHECK(node.poll(0ms).ok());
    CHECK(calls == 2);
}

void test_callback_wait_rejects_self_and_stop_wakes_background() {
    auto node = make_node();
    std::promise<void> callback;
    auto completed = callback.get_future();
    node.setEventHandler([&](const ssn::NodeEvent&) {
        CHECK(node.waitStopped().code() == ssn::ErrorCode::WouldDeadlock);
        CHECK(node.poll(0ms).code() == ssn::ErrorCode::InvalidState);
        CHECK(node.stop().ok());
        node.setEventHandler({});
        callback.set_value();
    });
    CHECK(node.startBackground().ok());
    Access::inject(node, {});
    CHECK(completed.wait_for(2s) == std::future_status::ready);
    CHECK(node.stop().ok());
    CHECK(node.waitStopped().ok());
}

void test_external_callback_wait_rejects_self_and_recursive_poll() {
    auto node = make_node();
    node.setEventHandler([&](const ssn::NodeEvent&) {
        CHECK(node.waitStopped().code() == ssn::ErrorCode::WouldDeadlock);
        CHECK(node.poll(0ms).code() == ssn::ErrorCode::InvalidState);
        CHECK(node.stop().ok());
    });
    Access::inject(node, {});
    CHECK(node.poll(0ms).ok());
    CHECK(node.poll(0ms).ok());
    CHECK(node.waitStopped().ok());
}

void test_idle_poll_is_woken_by_stop_and_concurrent_poll_rejected() {
    auto node = make_node();
    CHECK(node.poll(0ms).ok());
    auto polling = std::async(std::launch::async, [&] { return node.poll(5s); });
    // 先等第一次 poll 拿到驱动权，再验证并发 poll 被拒绝。
    std::this_thread::sleep_for(30ms);
    CHECK(node.poll(0ms).code() == ssn::ErrorCode::InvalidState);
    CHECK(node.stop().ok());
    CHECK(polling.wait_for(1s) == std::future_status::ready);
    CHECK(polling.get().ok());
    CHECK(Access::state(node) == ssn::NodeState::Stopped);
}

// 评审 C1 回归：唤醒管道创建失败时 Node::create 必须返回 ResourceLimit，
// 不得产出无法被 stop() 唤醒的节点（后台模式 pselect 长眠挂死）。
// 软上限直接压到 0：rlimit 只约束新 fd 分配、不影响已打开的 stdio/log fd，
// pipe() 必然 EMFILE。不得改用 /proc/self/fd 快照反推空闲槽位——单次快照
// 对「统计之后、pipe 之前有 fd 关闭」的竞态零防御，曾间歇误放行 create。
void test_create_fails_when_wakeup_pipe_unavailable() {
    struct rlimit before {};
    CHECK(getrlimit(RLIMIT_NOFILE, &before) == 0);
    const struct rlimit zero {0, before.rlim_max};
    CHECK(setrlimit(RLIMIT_NOFILE, &zero) == 0);
    auto result = ssn::Node::create({});
    CHECK(setrlimit(RLIMIT_NOFILE, &before) == 0);
    CHECK(!result.ok());
    if (!result.ok()) {
        CHECK(result.status().code() == ssn::ErrorCode::ResourceLimit);
    }
}

// 评审 I5 回归：事件入队失败不得静默——push 返回错误时 emitEvent 必须记入
// callback_error 诊断并留日志（错误模型：以 Status/诊断表达失败，不抛异常）。
void test_emit_event_failure_is_recorded() {
    auto node = make_node();
    CHECK(node.poll(0ms).ok());
    CHECK(node.stop().ok());
    // stop() 已 interrupt 事件队列：此后入队必然失败（InvalidState）
    Access::emit(node, ssn::NodeEventType::PeerConnected, {0, 1}, {});
    CHECK(Access::diagnostic(node) == ssn::ErrorCode::CallbackError);
}

// 评审 I5 回归：事件入队遭遇 bad_alloc（地址空间受限）必须返回 ResourceLimit，
// 不得让 std::bad_alloc 逃逸出 push（未防御时异常逃出 main 触发 terminate）。
void test_event_push_under_memory_pressure_fails_cleanly() {
    auto node = make_node();
    const std::size_t payload_size = 64UL << 20; // 64 MiB：余量外必然分配失败
    std::vector<std::byte> payload(payload_size);

    std::ifstream statm("/proc/self/statm");
    unsigned long pages = 0;
    statm >> pages;
    CHECK(pages > 0);
    const long page_size = sysconf(_SC_PAGESIZE);
    CHECK(page_size > 0);
    struct rlimit before {};
    CHECK(getrlimit(RLIMIT_AS, &before) == 0);
    // 上限 = 当前用量 + 32 MiB 余量：64 MiB 消息拷贝必然分配失败
    const struct rlimit lowered {
        static_cast<rlim_t>(pages) * static_cast<rlim_t>(page_size) + (32UL << 20),
        before.rlim_max };
    CHECK(setrlimit(RLIMIT_AS, &lowered) == 0);
    ssn::NodeEvent event;
    event.type = ssn::NodeEventType::MessageReceived;
    event.message = ssn::MessageView{payload.data(), payload.size()};
    const ssn::Status status = Access::inject(node, std::move(event));
    CHECK(setrlimit(RLIMIT_AS, &before) == 0);
    CHECK(status.code() == ssn::ErrorCode::ResourceLimit);
}

// 评审 I3 回归：listen 必须真实 bind+listen——非法地址、端口占用必须
// 返回真实系统错误；多地址共存；Running 后拒绝新监听。
void test_listen_reports_real_errors() {
    auto node = make_node();
    // 非法地址 → AddressError
    CHECK(node.listen(ssn::ListenAddress{"garbage://x"}).code()
          == ssn::ErrorCode::AddressError);
    // 首个 TCP 地址成功
    CHECK(node.listen(ssn::ListenAddress{"tcp://127.0.0.1:19301"}).ok());
    // 端口被占用 → 失败（不假成功）：bind 失败被 tcp_transport_listen 返 false，
    // createListener 映射为 IoError（具体码不重要，关键是 !ok）。
    auto node2 = make_node();
    CHECK(!node2.listen(ssn::ListenAddress{"tcp://127.0.0.1:19301"}).ok());
    // 多地址共存：同一 Node 再听一个端口
    CHECK(node.listen(ssn::ListenAddress{"tcp://127.0.0.1:19302"}).ok());
    // Running 后拒绝新监听
    CHECK(node.poll(0ms).ok());
    CHECK(node.listen(ssn::ListenAddress{"tcp://127.0.0.1:19303"}).code()
          == ssn::ErrorCode::InvalidState);
}

// 评审 I3+I2 回归：accept 入站 Peer 必须触发 PeerConnected 事件且方向为
// Inbound，句柄移交 PeerSession 后 peers() 可见。
void test_accept_inbound_peer_emits_event() {
    auto listener_node = make_node();
    CHECK(listener_node.listen(ssn::ListenAddress{"tcp://127.0.0.1:19311"}).ok());
    CHECK(listener_node.poll(0ms).ok()); // 进入 Running

    std::vector<ssn::NodeEvent> events;
    listener_node.setEventHandler(
        [&](const ssn::NodeEvent& event) { events.push_back(event); });

    auto connector = make_node();
    CHECK(connector.poll(0ms).ok());
    auto peer = connector.connect(ssn::ListenAddress{"tcp://127.0.0.1:19311"});
    CHECK(peer.ok());

    // 轮询 listener 直到 accept 完成
    bool accepted = false;
    for (int i = 0; i < 200 && !accepted; ++i) {
        listener_node.poll(10ms);
        for (const auto& event : events) {
            if (event.type == ssn::NodeEventType::PeerConnected) { accepted = true; }
        }
    }
    CHECK(accepted);
    const auto peers = listener_node.peers();
    CHECK(peers.size() == 1);
    if (!peers.empty()) {
        CHECK(peers[0].direction == ssn::PeerDirection::Inbound);
        CHECK(peers[0].state == ssn::PeerState::Connected);
    }
}

// 评审 I1 回归：connect 必须尊重 ConnectOptions::timeout——对黑洞地址
// （RFC 5737 保留段 192.0.2.1：SYN 不响应也不 RST）的非阻塞连接在 deadline
// 到期后必须清扫 pending Peer 并派发 Error(Timeout) 事件。未实现时该 Peer
// 永久挂在 pending_connects，轮询到 2s 上限也收不到任何结论事件。
void test_connect_timeout_sweeps_pending_peer() {
    auto node = make_node();
    bool got_error = false;
    ssn::ErrorCode error_code = ssn::ErrorCode::Ok;
    ssn::PeerId error_peer{};
    node.setEventHandler([&](const ssn::NodeEvent& event) {
        if (event.type == ssn::NodeEventType::Error) {
            got_error = true;
            error_code = event.status.code();
            error_peer = event.peer;
        }
    });
    CHECK(node.poll(0ms).ok());

    ssn::ConnectOptions options;
    options.timeout = 100ms;
    auto connected = node.connect(ssn::ListenAddress{"tcp://192.0.2.1:80"}, options);
    CHECK(connected.ok());
    const ssn::PeerId id = connected.value();

    const auto began = std::chrono::steady_clock::now();
    // poll 轮次随调度变化，循环内不计入断言数（失败时下方 got_error 断言兜底）
    while (!got_error && std::chrono::steady_clock::now() - began < 2s) {
        (void)node.poll(50ms);
    }
    CHECK(got_error);
    CHECK(error_code == ssn::ErrorCode::Timeout);
    CHECK(error_peer == id);
    CHECK(node.peerInfo(id).status().code() == ssn::ErrorCode::NotFound);
    CHECK(node.stop().ok());
    CHECK(node.waitStopped().ok());
}

// 评审 I1 回归：timeout ≤ 0 表示不超时——pending Peer 必须被保留，
// 不得被某个内部默认 deadline 提前清扫（短窗口观察仍为 Connecting）。
void test_connect_without_timeout_keeps_pending_peer() {
    auto node = make_node();
    CHECK(node.poll(0ms).ok());

    ssn::ConnectOptions options;
    options.timeout = 0ms;
    auto connected = node.connect(ssn::ListenAddress{"tcp://192.0.2.1:80"}, options);
    CHECK(connected.ok());
    const ssn::PeerId id = connected.value();

    for (int i = 0; i < 6; ++i) { CHECK(node.poll(50ms).ok()); }
    auto info = node.peerInfo(id);
    CHECK(info.ok());
    if (info.ok()) { CHECK(info.value().state == ssn::PeerState::Connecting); }
    CHECK(node.stop().ok());
    CHECK(node.waitStopped().ok());
}

// 评审 I7 回归：后台模式下在事件回调线程上析构 Node（契约违例）不得
// terminate/死锁——防御策略为检测后 stop + worker detach，Impl 所有权移交
// 事件线程，由其收尾时延迟回收（未防御时 worker_ 仍 joinable，~thread 触发
// std::terminate）。
void test_background_callback_self_destroy_is_contained() {
    auto node = std::make_unique<ssn::Node>(make_node());
    std::promise<void> destroyed;
    auto done = destroyed.get_future();
    node->setEventHandler([&](const ssn::NodeEvent&) {
        node.reset(); // 事件线程自析构
        destroyed.set_value();
    });
    CHECK(node->startBackground().ok());
    Access::inject(*node, {});
    CHECK(done.wait_for(2s) == std::future_status::ready);
    CHECK(node == nullptr);
    // 给事件线程收尾（延迟回收 Impl）留出时间；进程存活即防御生效
    std::this_thread::sleep_for(200ms);
}

// 评审 I7 回归（外部驱动）：回调内析构不得死锁——Impl 由本轮 poll 收尾时回收
// （未防御时 ~Impl 等待 !dispatching_，而 dispatching_ 只能由本线程清除，挂死）。
void test_external_callback_self_destroy_is_contained() {
    auto node = std::make_unique<ssn::Node>(make_node());
    node->setEventHandler([&](const ssn::NodeEvent&) { node.reset(); });
    Access::inject(*node, {});
    CHECK(node->poll(0ms).ok());
    CHECK(node == nullptr);
}

// 裸 TCP 对端：精确控制接收节奏（读 / 不读），用于不依赖第二个 Node 即可
// 制造与解除发送积压。accept 前不读任何字节。
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
                // 读超时 1s：冲刷失败路径下 reader 不得无限阻塞测试进程
                const timeval tv{1, 0};
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

// 等待后台 Node 与裸对端建立连接，随后发送大帧直到出现「真实发送积压」：
// 单纯一次 queued_bytes>0 可能只是 worker 尚未 flush 的瞬态（WSL2 loopback
// 内核缓冲可达十 MB 级），必须连续 3 次采样滞留字节不减少，才能确认对端
// 不接收导致的持续 EAGAIN 积压。
std::size_t push_until_backlog(ssn::Node& node, ssn::PeerId id,
                               std::size_t payload_size,
                               std::size_t& queued_out) {
    const std::vector<std::byte> payload(payload_size, std::byte{0x5A});
    std::size_t frames = 0;
    queued_out = 0;
    std::size_t last_queued = 0;
    int stable = 0;
    for (int i = 0; i < 512; ++i) {
        auto status = node.send(id, ssn::ByteView{payload.data(), payload.size()});
        if (!status.ok()) { break; } // 触达队列上限即停止
        ++frames;
        std::this_thread::sleep_for(10ms); // 给后台事件线程一轮 flush
        std::size_t queued = 0;
        if (auto info = node.peerInfo(id); info.ok()) {
            queued = info.value().queued_bytes;
        }
        if (queued > 0 && queued >= last_queued) {
            if (++stable >= 3) { queued_out = queued; break; }
        } else {
            stable = 0;
        }
        last_queued = queued;
    }
    return frames;
}

void wait_connected(ssn::Node& node, const std::atomic<int>& connected) {
    for (int i = 0; i < 500 && connected.load() == 0; ++i) { usleep(2000); }
    CHECK(connected.load() == 1);
    (void)node;
}

// 评审 I6 回归：stop() 时已入队但尚未派发的事件必须全部送达回调，
// interrupt 只表示停止接收新事件，不得清空滞留批次（max_events_per_poll=1
// 保证每个事件独占一轮派发，首个回调门控期间其余事件确定性滞留队列）。
void test_stop_dispatches_queued_events_instead_of_dropping() {
    ssn::NodeConfig config;
    config.max_events_per_poll = 1;
    auto made = ssn::Node::create(config);
    CHECK(made.ok());
    auto node = std::move(made).value();

    std::atomic<int> received{0};
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    bool gate_open = false;
    node.setEventHandler([&](const ssn::NodeEvent&) {
        if (received.fetch_add(1) == 0) {
            std::unique_lock<std::mutex> lock(gate_mutex);
            gate_cv.wait_for(lock, 1s, [&] { return gate_open; });
        }
    });

    CHECK(node.startBackground().ok());
    for (int i = 0; i < 10; ++i) { Access::inject(node, {}); }
    std::this_thread::sleep_for(50ms); // 等事件线程进入首个回调门控
    CHECK(node.stop().ok());
    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        gate_open = true;
    }
    gate_cv.notify_all();
    CHECK(node.waitStopped().ok());
    CHECK(received.load() == 10);
}

// 评审 I6 回归：shutdown_timeout 内对端排空接收缓冲时，Node 必须把滞留
// 发送帧尽力冲刷完毕再关闭——对端收全全部帧字节（未实现冲刷时 stop 立即
// 关闭，滞留帧随连接关闭丢弃，对端读不全）。
void test_stop_flushes_pending_tx_within_shutdown_timeout() {
    RawTcpPeer peer(19312);
    ssn::NodeConfig config;
    config.shutdown_timeout = 3000ms;
    config.max_peer_queue_bytes = 64 * 1024 * 1024;
    config.max_total_queue_bytes = 64 * 1024 * 1024;
    auto made = ssn::Node::create(config);
    CHECK(made.ok());
    auto node = std::move(made).value();

    std::atomic<int> connected{0};
    node.setEventHandler([&](const ssn::NodeEvent& event) {
        if (event.type == ssn::NodeEventType::PeerConnected) { connected.fetch_add(1); }
    });
    CHECK(node.startBackground().ok());
    auto connected_peer = node.connect(ssn::ListenAddress{"tcp://127.0.0.1:19312"});
    CHECK(connected_peer.ok());
    CHECK(peer.accept_conn());
    wait_connected(node, connected);

    std::size_t queued = 0;
    const std::size_t frames = push_until_backlog(
        node, connected_peer.value(), SSN_MAX_PAYLOAD_SIZE, queued);
    CHECK(queued > 0);
    const std::size_t total = frames * (SSN_HEADER_SIZE + SSN_MAX_PAYLOAD_SIZE);

    std::atomic<std::size_t> got{0};
    std::thread reader([&] {
        char buf[32768];
        while (got.load() < total) {
            const ssize_t n = ::read(peer.conn, buf, sizeof(buf));
            if (n <= 0) { break; } // EOF/RST/超时：冲刷失败即退出
            got.fetch_add(static_cast<std::size_t>(n));
        }
    });

    CHECK(node.stop().ok());
    CHECK(node.waitStopped().ok());
    reader.join();
    CHECK(got.load() == total);
}

// 评审 I6 回归：对端不接收、滞留帧无法在 shutdown_timeout 内送出时，
// waitStopped() 必须在期限附近强关返回，既不提前丢弃（无冲刷窗口），
// 也不无限等待（当前实现立即返回，耗时下界断言可锁定该缺陷）。
void test_stop_force_closes_when_shutdown_timeout_expires() {
    RawTcpPeer peer(19313);
    ssn::NodeConfig config;
    config.shutdown_timeout = 300ms;
    config.max_peer_queue_bytes = 64 * 1024 * 1024;
    config.max_total_queue_bytes = 64 * 1024 * 1024;
    auto made = ssn::Node::create(config);
    CHECK(made.ok());
    auto node = std::move(made).value();

    std::atomic<int> connected{0};
    node.setEventHandler([&](const ssn::NodeEvent& event) {
        if (event.type == ssn::NodeEventType::PeerConnected) { connected.fetch_add(1); }
    });
    CHECK(node.startBackground().ok());
    auto connected_peer = node.connect(ssn::ListenAddress{"tcp://127.0.0.1:19313"});
    CHECK(connected_peer.ok());
    CHECK(peer.accept_conn());
    wait_connected(node, connected);

    std::size_t queued = 0;
    push_until_backlog(node, connected_peer.value(), SSN_MAX_PAYLOAD_SIZE, queued);
    CHECK(queued > 0);

    const auto began = std::chrono::steady_clock::now();
    CHECK(node.stop().ok());
    CHECK(node.waitStopped().ok());
    const auto elapsed = std::chrono::steady_clock::now() - began;
    CHECK(elapsed >= 200ms);
    CHECK(elapsed < 2000ms);
}

void test_moves_preserve_running_node_and_destructors_join() {
    const auto began = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) {
        auto first = make_node();
        CHECK(first.startBackground().ok());
        auto second = std::move(first);
        CHECK(first.poll(0ms).code() == ssn::ErrorCode::InvalidState);
        CHECK(first.stop().ok());
        auto third = make_node();
        CHECK(third.startBackground().ok());
        third = std::move(second);
        CHECK(second.waitStopped().ok());
    }
    CHECK(std::chrono::steady_clock::now() - began < 2s);
    { auto external = make_node(); CHECK(external.poll(0ms).ok()); }
}
}

int main() {
    test_configuration_and_created_stop();
    test_external_mode_and_stop_transition();
    test_stop_then_wait_without_extra_poll();
    test_background_mode_stop_and_wait();
    test_event_order_reentry_and_handler_replacement();
    test_callback_exception_is_recorded_and_next_event_runs();
    test_callback_wait_rejects_self_and_stop_wakes_background();
    test_external_callback_wait_rejects_self_and_recursive_poll();
    test_idle_poll_is_woken_by_stop_and_concurrent_poll_rejected();
    test_create_fails_when_wakeup_pipe_unavailable();
    test_emit_event_failure_is_recorded();
    test_event_push_under_memory_pressure_fails_cleanly();
    test_listen_reports_real_errors();
    test_accept_inbound_peer_emits_event();
    test_connect_timeout_sweeps_pending_peer();
    test_connect_without_timeout_keeps_pending_peer();
    test_background_callback_self_destroy_is_contained();
    test_external_callback_self_destroy_is_contained();
    test_stop_dispatches_queued_events_instead_of_dropping();
    test_stop_flushes_pending_tx_within_shutdown_timeout();
    test_stop_force_closes_when_shutdown_timeout_expires();
    test_moves_preserve_running_node_and_destructors_join();
    std::printf("C++ node lifecycle results: %d/%d passed\n", passed.load(), passed.load() + failed.load());
    return failed.load() == 0 ? 0 : 1;
}
