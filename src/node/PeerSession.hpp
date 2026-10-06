#ifndef SSN_NODE_PEER_SESSION_HPP
#define SSN_NODE_PEER_SESSION_HPP

#include "ssn/node/Types.hpp"
#include "ssn_frame.h" // ssn_stream_ctx_t

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace ssn::detail {

class PeerRegistry;
class NodeBackend;

// 发送队列中的一帧：MESSAGE header + payload，offset 记录已发字节数
struct PendingFrame final {
    std::vector<std::byte> data;
    std::size_t offset{0};
};

class PeerSession final {
public:
    PeerSession(PeerId id, PeerDirection direction, std::string_view address);
    // unique_ptr 成员的析构需要完整类型，在 .cpp 定义
    ~PeerSession();

    PeerId id() const noexcept;
    PeerDirection direction() const noexcept;
    std::string_view address() const noexcept;
    PeerState state() const noexcept;

    // I2：连接成功句柄移交——NodeBackend 由 PeerSession 持有，
    // 不再随 connect/accept 的临时对象析构而关闭
    void attachBackend(std::unique_ptr<NodeBackend> backend) noexcept;
    NodeBackend* backend() const noexcept;

    // I4/任务 6：组 MESSAGE 帧并入队发送（线程安全，可被任意线程调）。
    // 受两级上限约束：peer_cap 为单 Peer 队列字节上限，total/total_cap 为
    // Node 跨 Peer 总量计数（CAS 预留，任一步失败精确归还）；超限返回 QueueFull
    Status enqueueSend(ByteView payload, std::size_t peer_cap,
                       std::atomic<std::size_t>& total_queued,
                       std::size_t total_cap);
    // I4/任务 6：fd 可写时按 budget 字节预算尽力冲刷（只在事件线程调），
    // 预算用尽或 EAGAIN 即停，剩余留待下轮；实发字节实时归还总量计数
    void flushTx(std::atomic<std::size_t>& total_queued, std::size_t budget);
    bool txPending() const;
    std::size_t txBytes() const;
    // 任务 6：高低水位滞回——队列首次越过高水位/回落到低水位时各返回 true
    // 恰好一次，由 NodeImpl 据此派发 BackpressureOn/Off 事件
    bool markBackpressureIfCrossed(std::size_t high);
    bool clearBackpressureIfBelow(std::size_t low);
    // I4：接收流上下文（惰性创建，只在事件线程访问）
    ssn_stream_ctx_t* rxCtx() noexcept;
    // I4/任务 6：关闭传输并释放接收上下文；未发字节一次性归还总量计数
    void closeBackend(std::atomic<std::size_t>& total_queued) noexcept;

private:
    friend class PeerRegistry;

    Status markClosing();
    Status transition(PeerState target);
    PeerInfo snapshot() const;

    PeerId id_;
    PeerDirection direction_;
    OwnedText address_;
    std::atomic<PeerState> state_{PeerState::Connecting};
    std::unique_ptr<NodeBackend> backend_;

    // 发送队列（tx_mutex_ 保护）：send() 从任意线程入队，flushTx 从事件线程出队
    mutable std::mutex tx_mutex_;
    std::deque<PendingFrame> tx_;
    std::size_t tx_bytes_{0};
    // 任务 6：背压滞回标志——高/低水位事件只在穿越时刻各触发一次
    bool bp_on_{false};

    // 接收流上下文：128KiB 缓冲，惰性分配（仅在首次 recv 时创建），
    // 随 closeBackend 释放
    std::unique_ptr<ssn_stream_ctx_t> rx_;
};

}  // namespace ssn::detail

#endif  // SSN_NODE_PEER_SESSION_HPP
