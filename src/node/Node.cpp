#include "ssn/node/Node.hpp"
#include "NodeImpl.hpp"

#include <limits>
#include <new>

namespace ssn {

Node::Node(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Node::Node(Node&& other) noexcept = default;
Node& Node::operator=(Node&& other) noexcept = default;
Node::~Node() noexcept {
    // 评审 I7 防御：事件回调线程上的自析构不能就地删除 Impl——事件线程的
    // dispatch/poll 栈帧仍在使用该对象。命中时所有权移交事件线程延迟回收。
    if (impl_ && impl_->releaseForSelfDestruct()) {
        (void)impl_.release();
    }
}

Result<Node> Node::create(const NodeConfig& config) {
    if (config.max_peers == 0 ||
        config.max_peers >= std::numeric_limits<std::uint32_t>::max() ||
        config.max_events_per_poll == 0 || config.shutdown_timeout.count() < 0 ||
        config.max_peer_queue_bytes == 0 || config.max_total_queue_bytes == 0 ||
        config.queue_low_watermark_bytes > config.queue_high_watermark_bytes ||
        config.queue_high_watermark_bytes > config.max_peer_queue_bytes) {
        return Status::error(ErrorCode::InvalidArgument);
    }
    try {
        Node node{std::make_unique<Impl>(config)};
        // 评审 C1：唤醒管道必须可聚合轮询，否则后台模式 stop() 无法唤醒 pselect。
        if (!node.impl_->driver.usable()) {
            return Status::error(ErrorCode::ResourceLimit,
                                 "唤醒管道创建失败，Node 无法启动");
        }
        return node;
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::ResourceLimit);
    }
}

Status Node::listen(const ListenAddress& address) {
    if (!impl_) { return Status::error(ErrorCode::InvalidState); }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != NodeState::Created) { return Status::error(ErrorCode::InvalidState); }

    // I3：listen 必须真实 bind+listen，失败返回真实系统错误（不假成功）
    auto listener = detail::NodeBackend::createListener(address.address.view(), 128);
    if (!listener) { return listener.status(); }

    try {
        impl_->listeners_.push_back(
            std::make_unique<detail::NodeBackend>(std::move(listener).value()));
    } catch (const std::bad_alloc&) {
        return Status::error(ErrorCode::ResourceLimit, "监听后端内存不足");
    }
    if (impl_->listen_address.address.empty()) {
        impl_->listen_address = address; // 首地址保留用于语义锁定
    }
    return {};
}

Result<PeerId> Node::connect(const ListenAddress& address,
                            const ConnectOptions& options) {
    if (!impl_) { return Status::error(ErrorCode::InvalidState); }
    return impl_->connect(address, options);
}

Status Node::disconnect(PeerId peer) {
    if (!impl_) { return Status::error(ErrorCode::InvalidState); }
    return impl_->disconnect(peer);
}

Status Node::send(PeerId peer, ByteView bytes) {
    if (!impl_) { return Status::error(ErrorCode::InvalidState); }
    return impl_->send(peer, bytes);
}

Status Node::broadcast(ByteView bytes) {
    if (!impl_) { return Status::error(ErrorCode::InvalidState); }
    return impl_->broadcast(bytes);
}

Status Node::poll(std::chrono::milliseconds timeout) {
    return impl_ ? impl_->poll(timeout) : Status::error(ErrorCode::InvalidState);
}

Status Node::startBackground() {
    return impl_ ? impl_->startBackground() : Status::error(ErrorCode::InvalidState);
}

Status Node::stop() { return impl_ ? impl_->stop() : Status{}; }
Status Node::waitStopped() { return impl_ ? impl_->waitStopped() : Status{}; }

void Node::setEventHandler(EventHandler handler) {
    if (impl_) { impl_->setEventHandler(std::move(handler)); }
}

std::vector<PeerInfo> Node::peers() const {
    return impl_ ? impl_->registry.snapshot() : std::vector<PeerInfo>{};
}

Result<PeerInfo> Node::peerInfo(PeerId peer) const {
    if (!impl_) { return Status::error(ErrorCode::InvalidState); }
    for (auto& info : impl_->registry.snapshot()) {
        if (info.id == peer) { return std::move(info); }
    }
    return Status::error(ErrorCode::NotFound);
}
}
