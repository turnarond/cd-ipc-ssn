#ifndef SSN_NODE_IMPL_HPP
#define SSN_NODE_IMPL_HPP

#include "ssn/node/Node.hpp"
#include "EventQueue.hpp"
#include "NodeBackend.hpp"
#include "PeerRegistry.hpp"
#include "PollDriver.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ssn {

class Node::Impl final {
    // 测试访问口需要触发 emitEvent 的失败路径（I5 回归）
    friend struct detail::NodeTestAccess;

public:
    enum class DriveMode { Unselected, External, Background };

    // 进行中的出站连接：Peer 已分配，等待 fd 可写后判定结果
    struct PendingConnect final {
        PeerId peer;
        std::unique_ptr<detail::NodeBackend> backend;
        // 连接完成截止时刻；time_point::max() 表示不超时（options.timeout ≤ 0）
        std::chrono::steady_clock::time_point deadline;
    };

    explicit Impl(const NodeConfig& config);
    ~Impl() noexcept;
    Status poll(std::chrono::milliseconds timeout);
    Status startBackground();
    Status stop();
    Status waitStopped();
    void setEventHandler(EventHandler handler);
    Result<PeerId> connect(const ListenAddress& address,
                           const ConnectOptions& options);

    // 评审 I7 防御：在事件回调线程上析构 Node 时由 ~Node 调用。命中（回调线程
    // 自析构）则 stop + detach 后台线程、置 orphaned_ 并返回 true——~Node 必须
    // 释放所有权（release），Impl 改由事件线程在 poll/runBackground 收尾时
    // delete this 回收；未命中返回 false，~Node 走正常删除。
    bool releaseForSelfDestruct() noexcept;

    // I4：真实消息收发
    Status send(PeerId peer, ByteView bytes);
    Status broadcast(ByteView bytes);
    Status disconnect(PeerId peer);

    const NodeConfig config;
    detail::PeerRegistry registry;
    detail::EventQueue events;
    detail::PollDriver driver;
    std::vector<PendingConnect> pending_connects;
    mutable std::mutex mutex;
    NodeState state{NodeState::Created};
    // 多地址监听（I3）：每个 listen() 成功添加一个独立 listener；
    // 首地址保留用于 NodeTestAccess 语义锁定
    std::vector<std::unique_ptr<detail::NodeBackend>> listeners_;
    ListenAddress listen_address;
    std::atomic<ErrorCode> callback_error{ErrorCode::Ok};
    // 任务 6：全部 Peer 发送队列字节总和（CAS 维护，入队预留/冲刷归还/关闭清零）
    std::atomic<std::size_t> total_queued_bytes_{0};
    // 回调线程自析构标记：置位后 dispatchEvents 不再派发新事件，Impl 由事件线程
    // 在 poll/runBackground 收尾时 delete this 回收（所有权已随 ~Node release）。
    std::atomic<bool> orphaned_{false};

private:
    void driveOnce(std::chrono::milliseconds timeout);
    void completeConnects(const std::vector<int>& writable);
    // 清扫已过 deadline 的 pending 连接：ConnectFailed + Error(Timeout) + 回收
    void sweepExpiredConnects(std::chrono::steady_clock::time_point now);
    void recvFromPeers(const std::vector<int>& readable);
    void flushPeers(const std::vector<int>& writable);
    // graceful=true：对端关闭（Closing→Disconnected→PeerDisconnected）；
    // graceful=false：IO 错误（Failed→Error 事件 + status）
    void removePeer(PeerId id, bool graceful, Status status = {});
    void emitEvent(NodeEventType type, PeerId peer, Status status,
                   MessageView message = {}, std::size_t queued_bytes = 0);
    void dispatchBatch(const std::vector<detail::QueuedEvent>& batch);
    void dispatchEvents(std::chrono::milliseconds timeout);
    // I6：后台模式停止冲刷——关监听/失败未决连接，期限内派发已排队事件并
    // 尽力 flush 各 Peer 发送队列，到期强关剩余连接后派发关闭事件
    void drainOnShutdown();
    // 取尽并派发当前已排队事件（不等待新事件）
    void dispatchAllPending();
    // 是否还有 Connected Peer 的发送队列未排空
    bool hasPendingTx();
    void runBackground() noexcept;
    void finishDispatch();

    // ssn_stream_feed 回调：MESSAGE 帧 → MessageReceived 事件
    struct FrameContext { Impl* impl; PeerId peer; };
    static bool onFrame(ssn_header_t* hdr, void* arg);

    DriveMode mode_{DriveMode::Unselected};
    bool dispatching_{false};
    // 事件线程在回调内自停：本轮派发不完成 Stopping→Stopped，改由下一次
    // poll、waitStopped 或析构补完（仅影响补完时机，不影响可达性）。
    bool self_stop_{false};
    std::thread::id event_thread_;
    std::shared_ptr<EventHandler> handler_;
    std::condition_variable stopped_;
    std::mutex join_mutex_;
    std::thread worker_;
    // 任务 6：flushPeers 可写起点轮转游标（仅事件线程在锁内访问）
    std::size_t flush_cursor_{0};
};
}

#endif  // SSN_NODE_IMPL_HPP
