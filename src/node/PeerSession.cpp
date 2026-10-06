#include "PeerSession.hpp"
#include "NodeBackend.hpp"

#include <cerrno>
#include <cstring>
#include <new>

namespace ssn::detail {

PeerSession::PeerSession(PeerId id, PeerDirection direction,
                         std::string_view address)
    : id_(id), direction_(direction), address_(address) {}

PeerSession::~PeerSession() = default;

PeerId PeerSession::id() const noexcept {
    return id_;
}

PeerDirection PeerSession::direction() const noexcept {
    return direction_;
}

std::string_view PeerSession::address() const noexcept {
    return address_.view();
}

PeerState PeerSession::state() const noexcept {
    return state_.load(std::memory_order_acquire);
}

void PeerSession::attachBackend(std::unique_ptr<NodeBackend> backend) noexcept {
    backend_ = std::move(backend);
}

NodeBackend* PeerSession::backend() const noexcept {
    return backend_.get();
}

Status PeerSession::enqueueSend(ByteView payload, std::size_t peer_cap,
                                std::atomic<std::size_t>& total_queued,
                                std::size_t total_cap) {
    if (payload.size() > SSN_MAX_PAYLOAD_SIZE) {
        return Status::error(ErrorCode::InvalidArgument, "payload 超出最大帧负载");
    }
    const std::size_t frame_size = SSN_HEADER_SIZE + payload.size();

    // 任务 6：总量预算先 CAS 预留——多 Peer 并发入队时两级计数保持一致，
    // 后续任一步失败都按预留额精确归还
    std::size_t current = total_queued.load(std::memory_order_relaxed);
    for (;;) {
        if (current + frame_size > total_cap) {
            return Status::error(ErrorCode::QueueFull, "Node 总发送队列已达上限");
        }
        if (total_queued.compare_exchange_weak(
                current, current + frame_size,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            break;
        }
    }

    PendingFrame frame;
    try {
        frame.data.resize(frame_size);
    } catch (const std::bad_alloc&) {
        total_queued.fetch_sub(frame_size, std::memory_order_acq_rel);
        return Status::error(ErrorCode::ResourceLimit, "帧分配失败");
    }
    auto* hdr = ssn_create_header(frame.data.data(), SSN_MSG_TYPE_MESSAGE, 0, 0);
    ssn_set_data_length(hdr, static_cast<std::uint32_t>(payload.size()));
    if (payload.size() > 0) {
        std::memcpy(frame.data.data() + SSN_HEADER_SIZE, payload.data(), payload.size());
    }

    {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        // 单 Peer 上限在锁内复检：并发发送同一 Peer 时以锁内值为准
        if (tx_bytes_ + frame_size > peer_cap) {
            total_queued.fetch_sub(frame_size, std::memory_order_acq_rel);
            return Status::error(ErrorCode::QueueFull, "单 Peer 发送队列已达上限");
        }
        tx_bytes_ += frame_size;
        try {
            tx_.push_back(std::move(frame));
        } catch (const std::bad_alloc&) {
            tx_bytes_ -= frame_size;
            total_queued.fetch_sub(frame_size, std::memory_order_acq_rel);
            return Status::error(ErrorCode::ResourceLimit, "发送队列内存不足");
        }
    }
    return {};
}

void PeerSession::flushTx(std::atomic<std::size_t>& total_queued,
                          std::size_t budget) {
    if (!backend_) { return; }
    std::size_t sent_round = 0;
    std::lock_guard<std::mutex> lock(tx_mutex_);
    while (!tx_.empty() && sent_round < budget) {
        auto& front = tx_.front();
        const std::size_t want = front.data.size() - front.offset;
        const std::size_t allowed = budget - sent_round;
        const int n = backend_->sendRaw(front.data.data() + front.offset,
                                        want < allowed ? want : allowed);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { break; }
            // 真错误（对端 RST 等）：标记 Failed，由 NodeImpl 回收
            transition(PeerState::Failed);
            break;
        }
        front.offset += static_cast<std::size_t>(n);
        sent_round += static_cast<std::size_t>(n);
        tx_bytes_ -= static_cast<std::size_t>(n);
        // 实发字节实时归还 Node 总量计数（任务 6）
        total_queued.fetch_sub(static_cast<std::size_t>(n),
                               std::memory_order_acq_rel);
        if (front.offset == front.data.size()) {
            tx_.pop_front();
        } else {
            break; // 部分写：预算或内核缓冲用尽，下轮继续
        }
    }
}

bool PeerSession::markBackpressureIfCrossed(std::size_t high) {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    if (!bp_on_ && tx_bytes_ >= high) {
        bp_on_ = true;
        return true;
    }
    return false;
}

bool PeerSession::clearBackpressureIfBelow(std::size_t low) {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    if (bp_on_ && tx_bytes_ <= low) {
        bp_on_ = false;
        return true;
    }
    return false;
}

bool PeerSession::txPending() const {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    return !tx_.empty();
}

std::size_t PeerSession::txBytes() const {
    std::lock_guard<std::mutex> lock(tx_mutex_);
    return tx_bytes_;
}

ssn_stream_ctx_t* PeerSession::rxCtx() noexcept {
    if (!rx_) {
        try {
            rx_ = std::make_unique<ssn_stream_ctx_t>();
        } catch (const std::bad_alloc&) {
            return nullptr;
        }
        ssn_stream_init(rx_.get());
    }
    return rx_.get();
}

void PeerSession::closeBackend(std::atomic<std::size_t>& total_queued) noexcept {
    backend_.reset();
    rx_.reset();
    std::lock_guard<std::mutex> lock(tx_mutex_);
    // 任务 6：未发字节一次性归还 Node 总量计数，并复位背压滞回标志
    total_queued.fetch_sub(tx_bytes_, std::memory_order_acq_rel);
    tx_.clear();
    tx_bytes_ = 0;
    bp_on_ = false;
}

Status PeerSession::markClosing() {
    return transition(PeerState::Closing);
}

Status PeerSession::transition(PeerState target) {
    auto current = state_.load(std::memory_order_acquire);
    for (;;) {
        if (current == target) {
            return Status{};
        }

        const bool allowed =
            (current == PeerState::Connecting &&
             (target == PeerState::Connected ||
              target == PeerState::Closing ||
              target == PeerState::ConnectFailed)) ||
            (current == PeerState::Connected &&
             (target == PeerState::Closing || target == PeerState::Failed)) ||
            (current == PeerState::Closing &&
             target == PeerState::Disconnected);
        if (!allowed) {
            return Status::error(
                ErrorCode::InvalidState, "invalid peer state transition");
        }

        if (state_.compare_exchange_weak(
                current, target,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            return Status{};
        }
    }
}

PeerInfo PeerSession::snapshot() const {
    PeerInfo info;
    info.id = id_;
    info.direction = direction_;
    info.state = state();
    info.address = address_.view();
    info.queued_bytes = txBytes();
    return info;
}

}  // namespace ssn::detail
