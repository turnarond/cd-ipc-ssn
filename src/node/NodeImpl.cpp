#include "NodeImpl.hpp"

#include <algorithm>
#include <cerrno>
#include <exception>
#include <new>
#include <system_error>

#include "util/ssn_log.h"

namespace ssn {

Node::Impl::Impl(const NodeConfig& value) : config(value), registry(value.max_peers) {
    // 事件入队（含测试注入）与跨线程停止都要唤醒正在 pselect 等待的事件线程
    events.setOnPush([this] { driver.wake(); });
}

Result<PeerId> Node::Impl::connect(const ListenAddress& address,
                                   const ConnectOptions& options) {
    // I1：timeout > 0 设定完成截止时刻（由 driveOnce 的 pselect 截断 + 到期
    // 清扫兑现）；timeout ≤ 0 表示不超时，deadline 置为 time_point::max()。
    const auto deadline = options.timeout.count() > 0
        ? std::chrono::steady_clock::now() + options.timeout
        : std::chrono::steady_clock::time_point::max();

    std::lock_guard<std::mutex> lock(mutex);
    if (state != NodeState::Running) {
        return Status::error(ErrorCode::InvalidState, "Node 未在运行");
    }

    auto backend = detail::NodeBackend::create(address.address.view());
    if (!backend) {
        return backend.status();
    }

    const auto peer = registry.allocate(PeerDirection::Outbound, address.address.view());
    if (!peer) {
        return peer.status();
    }
    const PeerId id = peer.value();

    switch (backend->beginConnect()) {
    case detail::NodeBackend::ConnectState::Connected:
        if (auto session = registry.find(id); session) {
            session->attachBackend(
                std::make_unique<detail::NodeBackend>(std::move(backend).value()));
        }
        registry.transition(id, PeerState::Connected);
        emitEvent(NodeEventType::PeerConnected, id, Status{});
        return id;
    case detail::NodeBackend::ConnectState::InProgress:
        try {
            pending_connects.push_back(
                PendingConnect{id,
                               std::make_unique<detail::NodeBackend>(std::move(backend).value()),
                               deadline});
        } catch (const std::bad_alloc&) {
            registry.transition(id, PeerState::ConnectFailed);
            registry.erase(id);
            return Status::error(ErrorCode::ResourceLimit, "pending connect 内存不足");
        }
        // 任务 6 缺陷修复：connect 入队 pending 后必须唤醒驱动线程——否则
        // 后台线程睡在旧 watch 集的 pselect 里，无 listener 的节点上出站
        // 连接将永远停在 Connecting（集成测试未暴露是因为那些节点都被
        // 入站 accept 间接唤醒过）
        driver.wake();
        return id;
    default:
        break;
    }

    registry.transition(id, PeerState::ConnectFailed);
    registry.erase(id);
    return Status::error(ErrorCode::ConnectFailed, "连接发起失败");
}

bool Node::Impl::releaseForSelfDestruct() noexcept {
    bool background = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!dispatching_ || event_thread_ != std::this_thread::get_id()) {
            return false;
        }
        orphaned_.store(true, std::memory_order_release);
        background = mode_ == DriveMode::Background;
    }
    LOG_ERROR("Node: 事件回调线程上析构 Node 属契约违例，已转为 stop+detach 延迟回收");
    // stop() 内部自取 mutex；此处不得持锁调用
    stop();
    if (background) {
        // worker_ 即本线程：detach 避免收尾 delete this 时 join 自身死锁
        std::lock_guard<std::mutex> joining(join_mutex_);
        if (worker_.joinable()) { worker_.detach(); }
    }
    return true;
}

void Node::Impl::emitEvent(NodeEventType type, PeerId peer, Status status,
                           MessageView message, std::size_t queued_bytes) {
    NodeEvent event;
    event.type = type;
    event.peer = peer;
    event.status = std::move(status);
    event.message = message;
    // 任务 6：背压事件携带触发时刻的 Peer 队列字节快照
    event.queued_bytes = queued_bytes;
    if (!events.push(std::move(event)).ok()) {
        callback_error.store(ErrorCode::CallbackError);
        LOG_ERROR("Node: 事件入队失败，后续回调可能缺失事件");
    }
}

void Node::Impl::completeConnects(const std::vector<int>& writable) {
    for (auto item = pending_connects.begin(); item != pending_connects.end();) {
        const int fd = item->backend->fd();
        if (std::find(writable.begin(), writable.end(), fd) == writable.end()) {
            ++item;
            continue;
        }

        const PeerId peer = item->peer;
        const Status status = item->backend->finishConnect();
        if (status.ok()) {
            if (auto session = registry.find(peer); session) {
                session->attachBackend(std::move(item->backend));
            }
            registry.transition(peer, PeerState::Connected);
            emitEvent(NodeEventType::PeerConnected, peer, Status{});
        } else {
            registry.transition(peer, PeerState::ConnectFailed);
            emitEvent(NodeEventType::Error, peer, status);
            registry.erase(peer);
        }
        item = pending_connects.erase(item);
    }
}

void Node::Impl::sweepExpiredConnects(std::chrono::steady_clock::time_point now) {
    // 调用方持有 mutex。到期连接仍在 pending（本轮未被 completeConnects 定论）
    // 才清扫：先发 Timeout 错误事件再回收，与主动连接失败路径的事件序列一致。
    for (auto it = pending_connects.begin(); it != pending_connects.end();) {
        if (it->deadline > now) {
            ++it;
            continue;
        }
        const PeerId peer = it->peer;
        registry.transition(peer, PeerState::ConnectFailed);
        emitEvent(NodeEventType::Error, peer,
                  Status::error(ErrorCode::Timeout, "连接超时"));
        registry.erase(peer);
        it = pending_connects.erase(it);
    }
}

Status Node::Impl::send(PeerId peer, ByteView bytes) {
    auto session = registry.find(peer);
    if (!session) { return Status::error(ErrorCode::NotFound, "Peer 不存在"); }
    if (session->state() != PeerState::Connected) {
        return Status::error(ErrorCode::InvalidState, "Peer 未在连接态");
    }
    {
        // I6：stop 语义为停止接收新发送，Stopping 期间拒绝再入队
        std::lock_guard<std::mutex> lock(mutex);
        if (state != NodeState::Running) {
            return Status::error(ErrorCode::InvalidState, "Node 停止中，拒绝新发送");
        }
    }
    auto status = session->enqueueSend(bytes, config.max_peer_queue_bytes,
                                       total_queued_bytes_,
                                       config.max_total_queue_bytes);
    if (status.ok()) {
        driver.wake();
        // 任务 6：越过高水位时恰好发一条 BackpressureOn（快照=当前滞留字节）
        if (session->markBackpressureIfCrossed(
                config.queue_high_watermark_bytes)) {
            emitEvent(NodeEventType::BackpressureOn, peer, Status{}, MessageView{},
                      session->txBytes());
        }
    }
    return status;
}

Status Node::Impl::broadcast(ByteView bytes) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (state != NodeState::Running) {
            return Status::error(ErrorCode::InvalidState, "Node 未在运行");
        }
    }
    if (bytes.size() > SSN_MAX_PAYLOAD_SIZE) {
        return Status::error(ErrorCode::InvalidArgument, "payload 超出最大帧负载");
    }
    // 尽力逐个入队，任一失败返回首错（已入队不回滚）
    Status first_error;
    for (const auto& info : registry.snapshot()) {
        if (info.state != PeerState::Connected) { continue; }
        auto session = registry.find(info.id);
        if (!session) { continue; }
        auto status = session->enqueueSend(bytes, config.max_peer_queue_bytes,
                                           total_queued_bytes_,
                                           config.max_total_queue_bytes);
        if (status.ok()) {
            if (session->markBackpressureIfCrossed(
                    config.queue_high_watermark_bytes)) {
                emitEvent(NodeEventType::BackpressureOn, info.id, Status{},
                          MessageView{}, session->txBytes());
            }
        } else if (first_error.ok()) {
            first_error = status;
        }
    }
    driver.wake();
    return first_error;
}

Status Node::Impl::disconnect(PeerId peer) {
    auto session = registry.find(peer);
    if (!session) { return Status::error(ErrorCode::NotFound, "Peer 不存在"); }
    // I4：Closing → 立即关闭传输丢弃未发队列 → PeerDisconnected 事件 → 回收
    if (auto status = registry.markClosing(peer); !status.ok()) { return status; }
    session->closeBackend(total_queued_bytes_);
    registry.transition(peer, PeerState::Disconnected);
    emitEvent(NodeEventType::PeerDisconnected, peer, Status{});
    registry.erase(peer);
    driver.wake();
    return {};
}

void Node::Impl::removePeer(PeerId id, bool graceful, Status status) {
    auto session = registry.find(id);
    if (!session) { return; }
    session->closeBackend(total_queued_bytes_);
    if (graceful) {
        registry.markClosing(id);
        registry.transition(id, PeerState::Disconnected);
        emitEvent(NodeEventType::PeerDisconnected, id, Status{});
    } else {
        registry.transition(id, PeerState::Failed);
        emitEvent(NodeEventType::Error, id, std::move(status));
    }
    registry.erase(id);
}

void Node::Impl::recvFromPeers(const std::vector<int>& readable) {
    for (int fd : readable) {
        for (const auto& info : registry.snapshot()) {
            auto session = registry.find(info.id);
            if (!session || !session->backend() || session->backend()->fd() != fd) {
                continue;
            }
            std::byte buf[8192];
            const int n = session->backend()->recvRaw(buf, sizeof(buf));
            if (n > 0) {
                auto* ctx = session->rxCtx();
                if (!ctx) {
                    removePeer(info.id, false, Status::error(
                        ErrorCode::ResourceLimit, "接收上下文分配失败"));
                    break;
                }
                FrameContext fctx{this, info.id};
                if (!ssn_stream_feed(ctx, buf, static_cast<std::size_t>(n),
                                     &onFrame, &fctx)) {
                    removePeer(info.id, false, Status::error(
                        ErrorCode::ProtocolError, "帧流格式错误"));
                }
            } else if (n == 0) {
                removePeer(info.id, true, Status{});
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                removePeer(info.id, false, Status::error(
                    ErrorCode::IoError, "接收失败", errno));
            }
            break;
        }
    }
}

void Node::Impl::flushPeers(const std::vector<int>& writable) {
    // 任务 6：每轮对每个可写 Peer 施加相同字节预算，并轮转本轮起点，
    // 防止大队列 Peer 独占事件循环、饿死其他 Peer 的冲刷
    constexpr std::size_t kPerPeerFlushBudget = 64 * 1024;
    if (writable.empty()) { return; }
    flush_cursor_ %= writable.size();
    for (std::size_t i = 0; i < writable.size(); ++i) {
        const int fd = writable[(flush_cursor_ + i) % writable.size()];
        for (const auto& info : registry.snapshot()) {
            auto session = registry.find(info.id);
            if (!session || !session->backend() || session->backend()->fd() != fd) {
                continue;
            }
            session->flushTx(total_queued_bytes_, kPerPeerFlushBudget);
            if (session->state() == PeerState::Failed) {
                removePeer(info.id, false, Status::error(
                    ErrorCode::IoError, "发送失败"));
            } else if (session->clearBackpressureIfBelow(
                           config.queue_low_watermark_bytes)) {
                // 回落到低水位：恰好一条 BackpressureOff（快照=当前滞留字节）
                emitEvent(NodeEventType::BackpressureOff, info.id, Status{},
                          MessageView{}, session->txBytes());
            }
            break;
        }
    }
    ++flush_cursor_;
}

bool Node::Impl::onFrame(ssn_header_t* hdr, void* arg) {
    auto* ctx = static_cast<FrameContext*>(arg);
    if (ssn_get_msg_type(hdr) != SSN_MSG_TYPE_MESSAGE) { return true; }
    ssn_data_ref_t data{};
    if (!ssn_get_data(hdr, &data)) { return true; }
    ctx->impl->emitEvent(NodeEventType::MessageReceived, ctx->peer, Status{},
                         MessageView{static_cast<const std::byte*>(data.data),
                                     data.length});
    return true;
}

void Node::Impl::driveOnce(std::chrono::milliseconds timeout) {
    std::vector<int> watch_read;
    std::vector<int> watch_write;
    auto wait = timeout;
    {
        std::lock_guard<std::mutex> lock(mutex);
        watch_read.reserve(listeners_.size());
        for (const auto& listener : listeners_) {
            watch_read.push_back(listener->fd());
        }
        watch_write.reserve(pending_connects.size());
        for (const auto& item : pending_connects) {
            watch_write.push_back(item.backend->fd());
            // I1：pselect 等待必须被最近的连接 deadline 截断，否则到期清扫
            // 无法按时触发（后台模式默认等待 24h，超时将永远不生效）。
            if (item.deadline == std::chrono::steady_clock::time_point::max()) {
                continue;
            }
            const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(
                item.deadline - std::chrono::steady_clock::now());
            if (remain < wait) {
                wait = remain.count() < 0 ? std::chrono::milliseconds{0} : remain;
            }
        }
        // 已连接 Peer：始终监听可读（收数据/对端关闭）；有待发数据时追加可写监听
        for (const auto& info : registry.snapshot()) {
            if (info.state != PeerState::Connected) { continue; }
            auto session = registry.find(info.id);
            if (!session || !session->backend()) { continue; }
            const int fd = session->backend()->fd();
            watch_read.push_back(fd);
            if (session->txPending()) { watch_write.push_back(fd); }
        }
    }

    detail::PollDriver::Ready ready;
    const Status status = driver.pollOnce(watch_read, watch_write, wait, ready);
    if (!status.ok()) {
        LOG_ERROR("Node: 轮询失败，本轮跳过 IO 处理");
    }

    if (!ready.readable.empty()) {
        std::lock_guard<std::mutex> lock(mutex);
        for (int fd : ready.readable) {
            // listener fd → accept；peer fd → recv
            bool is_listener = false;
            for (auto& listener : listeners_) {
                if (listener->fd() != fd) { continue; }
                is_listener = true;
                // 可读即「有至少一个连接等待 accept」：循环取尽防止内核队列堆积
                for (;;) {
                    auto accepted = listener->acceptOne();
                    if (!accepted) { break; }
                    const std::string_view addr = accepted.value().peerAddress();
                    auto peer = registry.allocate(PeerDirection::Inbound, addr);
                    if (!peer) {
                        LOG_ERROR("Node: 入站 Peer 分配失败");
                        break;
                    }
                    if (auto session = registry.find(peer.value()); session) {
                        session->attachBackend(std::make_unique<detail::NodeBackend>(
                            std::move(accepted).value()));
                    }
                    registry.transition(peer.value(), PeerState::Connected);
                    emitEvent(NodeEventType::PeerConnected, peer.value(), Status{});
                }
                break;
            }
            if (!is_listener) { recvFromPeers({fd}); }
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        // 先定论本轮可写的连接（成功/失败都给准确结论），再清扫其余到期连接：
        // 同一连接即便同时满足两者，也只会以 completeConnects 的结论入队一次。
        if (!ready.writable.empty()) {
            completeConnects(ready.writable);
            flushPeers(ready.writable);
        }
        sweepExpiredConnects(std::chrono::steady_clock::now());
    }

    // IO 事件已入队，本轮不再额外等待：总等待由上面的 pselect 限界
    dispatchEvents(std::chrono::milliseconds{0});
}

Node::Impl::~Impl() noexcept {
    stop();
    {
        std::unique_lock<std::mutex> lock(mutex);
        // 外部驱动模式下，由析构补上最后一轮停止转换。
        if (mode_ != DriveMode::Background) {
            stopped_.wait(lock, [&] { return !dispatching_; });
            state = NodeState::Stopped;
            stopped_.notify_all();
        }
    }
    waitStopped();
}

Status Node::Impl::poll(std::chrono::milliseconds timeout) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (mode_ == DriveMode::Background || dispatching_) {
            return Status::error(ErrorCode::InvalidState);
        }
        if (state == NodeState::Stopping) {
            state = NodeState::Stopped;
            stopped_.notify_all();
            return {};
        }
        if (state == NodeState::Stopped || state == NodeState::Failed) {
            return Status::error(ErrorCode::InvalidState);
        }
        if (timeout.count() < 0) { return Status::error(ErrorCode::InvalidArgument); }
        mode_ = DriveMode::External;
        state = NodeState::Running;
        dispatching_ = true;
        event_thread_ = std::this_thread::get_id();
    }
    try {
        driveOnce(timeout);
    } catch (...) {
        finishDispatch();
        if (orphaned_.load(std::memory_order_acquire)) { delete this; }
        return Status::error(ErrorCode::ResourceLimit);
    }
    finishDispatch();
    if (orphaned_.load(std::memory_order_acquire)) {
        // 自析构移交：Impl 由本事件线程收尾回收（delete this 后不得再触成员）
        delete this;
        return {};
    }
    return {};
}

void Node::Impl::finishDispatch() {
    std::lock_guard<std::mutex> lock(mutex);
    dispatching_ = false;
    event_thread_ = {};
    // 外部驱动模式下，回调内自停不在本轮完成 Stopping→Stopped：本轮可能还有
    // 后续事件要派发，停止由下一次 poll / waitStopped / 析构补完。后台模式没有
    // 外部驱动者，必须由内部事件线程在本轮退出时就地停止。
    const bool defer_stop = mode_ == DriveMode::External && self_stop_;
    if (state == NodeState::Stopping && !defer_stop) { state = NodeState::Stopped; }
    self_stop_ = false;
    stopped_.notify_all();
}

Status Node::Impl::startBackground() {
    std::lock_guard<std::mutex> lock(mutex);
    if (state != NodeState::Created || mode_ != DriveMode::Unselected) {
        return Status::error(ErrorCode::InvalidState);
    }
    try {
        worker_ = std::thread([this] { runBackground(); });
    } catch (const std::system_error&) {
        return Status::error(ErrorCode::ResourceLimit);
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::ResourceLimit);
    }
    mode_ = DriveMode::Background;
    state = NodeState::Running;
    return {};
}

void Node::Impl::runBackground() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex);
        event_thread_ = std::this_thread::get_id();
        dispatching_ = true;
    }
    bool drain = false;
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            // 任何非 Running 状态都必须退出：只认 Stopping 会让迟启动的线程
            // 在已 Stopped 的节点上无限空转（EventQueue 已被 interrupt，take
            // 立即返回空批，循环体不阻塞）。
            if (state != NodeState::Running) {
                drain = (state == NodeState::Stopping);
                break;
            }
        }
        try {
            driveOnce(std::chrono::hours{24});
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex);
            state = NodeState::Failed;
            drain = false;
            break;
        }
    }
    // I6：正常停止进入冲刷收尾；Failed/回调自析构（orphaned）不保证冲刷。
    if (drain && !orphaned_.load(std::memory_order_acquire)) {
        drainOnShutdown();
    }
    finishDispatch();
    if (orphaned_.load(std::memory_order_acquire)) {
        // 自析构移交：Impl 由本事件线程收尾回收（delete this 后不得再触成员）
        delete this;
    }
}

Status Node::Impl::stop() {
    bool background = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        background = (mode_ == DriveMode::Background);
        if (state == NodeState::Created || state == NodeState::Running) {
            state = mode_ == DriveMode::Unselected ? NodeState::Stopped : NodeState::Stopping;
        }
        // 记录本次 stop 来自事件线程自身的回调（仅外部模式下影响停止归属）。
        if (dispatching_ && event_thread_ == std::this_thread::get_id()) {
            self_stop_ = true;
        }
    }
    // I6：后台模式保持事件队列开放——worker 冲刷期间关闭结论事件仍需入队，
    // seal 由 drainOnShutdown() 收尾完成；其他模式立即拒收新事件。
    if (!background) { events.interrupt(); }
    driver.wake(); // 唤醒可能阻塞在 pselect 的驱动线程
    stopped_.notify_all();
    return {};
}

Status Node::Impl::waitStopped() {
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (dispatching_ && event_thread_ == std::this_thread::get_id()) {
            return Status::error(ErrorCode::WouldDeadlock);
        }
        // 外部驱动模式下没有在途 poll 时，由等待者完成最后一步停止：
        // 否则「poll → stop → waitStopped」这一惯用关闭序列会永久阻塞。
        // 后台模式的停止由内部事件线程在退出时完成，等待者不代劳。
        while (state != NodeState::Stopped && state != NodeState::Failed) {
            if (mode_ != DriveMode::Background && state == NodeState::Stopping &&
                !dispatching_) {
                state = NodeState::Stopped;
                stopped_.notify_all();
                break;
            }
            stopped_.wait(lock);
        }
    }
    std::lock_guard<std::mutex> joining(join_mutex_);
    if (worker_.joinable()) { worker_.join(); }
    return {};
}

void Node::Impl::setEventHandler(EventHandler handler) {
    std::shared_ptr<EventHandler> replacement;
    if (handler) { replacement = std::make_shared<EventHandler>(std::move(handler)); }
    {
        std::lock_guard<std::mutex> lock(mutex);
        handler_.swap(replacement);
    }
    // 旧 handler 的捕获对象同样在释放 Node 互斥锁之后再析构。
}

void Node::Impl::dispatchBatch(const std::vector<detail::QueuedEvent>& batch) {
    for (const auto& queued : batch) {
        // 回调内自析构后不得再派发后续事件：handler 捕获已随 ~Node 失效
        if (orphaned_.load(std::memory_order_acquire)) { break; }
        std::shared_ptr<EventHandler> handler;
        {
            std::lock_guard<std::mutex> lock(mutex);
            handler = handler_;
        }
        if (!handler) { continue; }
        try {
            (*handler)(queued.event);
        } catch (const std::exception& error) {
            callback_error.store(ErrorCode::CallbackError);
            LOG_ERROR("Node: 用户回调抛出异常，已捕获并继续派发: %s", error.what());
        } catch (...) {
            callback_error.store(ErrorCode::CallbackError);
            LOG_ERROR("Node: 用户回调抛出未知异常，已捕获并继续派发");
        }
    }
}

void Node::Impl::dispatchEvents(std::chrono::milliseconds timeout) {
    dispatchBatch(events.take(timeout, config.max_events_per_poll));
}

void Node::Impl::dispatchAllPending() {
    // stop 冲刷期间 interrupted_ 尚未置位：take(0) 非阻塞取尽已排队事件，
    // 空批即队列已空；任一批次派发中命中自析构防御则立即停止。
    for (;;) {
        auto batch = events.take(std::chrono::milliseconds{0},
                                 config.max_events_per_poll);
        if (batch.empty()) { return; }
        dispatchBatch(batch);
        if (orphaned_.load(std::memory_order_acquire)) { return; }
    }
}

bool Node::Impl::hasPendingTx() {
    for (const auto& info : registry.snapshot()) {
        if (info.state != PeerState::Connected) { continue; }
        auto session = registry.find(info.id);
        if (session && session->txPending()) { return true; }
    }
    return false;
}

void Node::Impl::drainOnShutdown() {
    const auto deadline =
        std::chrono::steady_clock::now() + config.shutdown_timeout;

    // 1) 停止接收新连接：关闭全部 listener；未决出站连接判失败并发 Error 事件。
    //    此处不能先 interrupt 事件队列——关闭结论事件仍需入队派发。
    {
        std::lock_guard<std::mutex> lock(mutex);
        listeners_.clear();
        for (auto& item : pending_connects) {
            registry.transition(item.peer, PeerState::ConnectFailed);
            emitEvent(NodeEventType::Error, item.peer,
                      Status::error(ErrorCode::InvalidState, "Node 停止，连接未完成"));
            registry.erase(item.peer);
        }
        pending_connects.clear();
    }

    // 2) 关闭期限内：先派发已排队事件，仍有待发帧则驱动一轮 IO（pselect
    //    等待受剩余期限截断）；事件已空且无发送滞留即冲刷完成。
    for (;;) {
        dispatchAllPending();
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) { break; }
        if (!hasPendingTx()) { break; }
        const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now);
        try {
            driveOnce(remain);
        } catch (...) {
            LOG_ERROR("Node: 停止冲刷期间 IO 异常，转入强制关闭");
            break;
        }
    }

    // 3) 到期或冲刷完成：强关全部剩余 Peer（丢弃仍滞留的发送队列），
    //    按 Closing→Disconnected 序列发 PeerDisconnected。
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& info : registry.snapshot()) {
            auto session = registry.find(info.id);
            if (!session) { continue; }
            session->closeBackend(total_queued_bytes_);
            registry.markClosing(info.id);
            registry.transition(info.id, PeerState::Disconnected);
            emitEvent(NodeEventType::PeerDisconnected, info.id, Status{});
            registry.erase(info.id);
        }
        listeners_.clear();
    }

    // 4) 派发完关闭事件再 seal 队列：此后拒绝新事件（此刻已无生产者）。
    dispatchAllPending();
    events.interrupt();
}
}
