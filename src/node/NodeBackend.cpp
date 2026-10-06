#include "NodeBackend.hpp"

#include "transports/ssn_transport_async_internal.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <utility>

namespace ssn::detail {

Result<NodeBackend> NodeBackend::create(std::string_view address) {
    if (address.empty()) {
        return Status::error(ErrorCode::InvalidArgument, "地址为空");
    }

    NodeBackend backend;
    const std::string text(address);
    if (!ssn_address_parse(text.c_str(), &backend.address_)) {
        return Status::error(ErrorCode::AddressError, "地址解析失败");
    }

    ssn_transport_config_t config;
    memset(&config, 0, sizeof(config));
    config.type = backend.address_.type;
    // 后端自行管理非阻塞语义（begin 一律置 O_NONBLOCK），传输层的
    // non_blocking 配置保持默认，避免影响其他调用方。
    config.non_blocking = false;

    backend.transport_ = ssn_transport_create(backend.address_.type, &config);
    if (backend.transport_ == nullptr) {
        return Status::error(ErrorCode::ResourceLimit, "传输创建失败");
    }
    return backend;
}

Result<NodeBackend> NodeBackend::createListener(std::string_view address,
                                                int backlog) {
    auto backend = create(address);
    if (!backend) {
        return backend.status();
    }

    ssn_transport_t* transport = backend.value().transport_;
    if (!ssn_transport_bind(transport, &backend.value().address_)) {
        return Status::error(ErrorCode::AddressError, "绑定监听地址失败", errno);
    }
    if (!ssn_transport_listen(transport, backlog)) {
        return Status::error(ErrorCode::IoError, "监听失败", errno);
    }

    // 传输层 accept(timeout_ms=0) 对阻塞 fd 会卡住 accept()：
    // Node 的 listener 必须非阻塞，让「无可接受连接」以 EAGAIN 返回。
    const int fd = ssn_transport_get_fd(transport);
    if (fd >= 0) {
        const int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            return Status::error(ErrorCode::IoError, "监听 fd 置非阻塞失败", errno);
        }
    }
    return backend;
}

Result<NodeBackend> NodeBackend::acceptOne() {
    if (!transport_) {
        return Status::error(ErrorCode::InvalidState, "监听后端已关闭");
    }

    ssn_address_t client_addr{};
    // timeout_ms=0：listener fd 已非阻塞，无连接时 accept 立即返回 nullptr
    ssn_transport_t* client = ssn_transport_accept(transport_, &client_addr, 0);
    if (!client) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("Node: accept 失败: %s", strerror(errno));
        }
        // EAGAIN（本轮无连接）与真错误都返回失败：调用方只看 ok() 决定是否继续
        return Status::error(ErrorCode::InvalidState, "accept 未就绪或失败");
    }

    // accept 出的 client 继承 listener 的 non_blocking=false：Node 的收发
    // 全部走非阻塞 + PollDriver 聚合，必须自行置 O_NONBLOCK。
    const int client_fd = ssn_transport_get_fd(client);
    if (client_fd >= 0) {
        const int flags = fcntl(client_fd, F_GETFL, 0);
        if (flags >= 0) { (void)fcntl(client_fd, F_SETFL, flags | O_NONBLOCK); }
    }

    NodeBackend backend;
    backend.transport_ = client;
    backend.address_ = client_addr;
    return backend;
}

NodeBackend::NodeBackend(NodeBackend&& other) noexcept
    : transport_(other.transport_), address_(other.address_) {
    other.transport_ = nullptr;
}

NodeBackend& NodeBackend::operator=(NodeBackend&& other) noexcept {
    if (this != &other) {
        close();
        transport_ = other.transport_;
        address_ = other.address_;
        other.transport_ = nullptr;
    }
    return *this;
}

NodeBackend::~NodeBackend() noexcept {
    close();
}

int NodeBackend::fd() const noexcept {
    return transport_ ? ssn_transport_get_fd(transport_) : -1;
}

std::string_view NodeBackend::peerAddress() const noexcept {
    return address_.address_str;
}

int NodeBackend::sendRaw(const void* data, std::size_t len) noexcept {
    return transport_ ? ssn_transport_send(transport_, data, len) : -1;
}

int NodeBackend::recvRaw(void* buf, std::size_t size) noexcept {
    return transport_ ? ssn_transport_recv(transport_, buf, size, 0) : -1;
}

NodeBackend::ConnectState NodeBackend::beginConnect() noexcept {
    if (!transport_) {
        return ConnectState::Failed;
    }

    switch (ssn_transport_connect_begin_internal(transport_, &address_)) {
    case SSN_CONNECT_CONNECTED:
        return ConnectState::Connected;
    case SSN_CONNECT_IN_PROGRESS:
        return ConnectState::InProgress;
    default:
        return ConnectState::Failed;
    }
}

Status NodeBackend::finishConnect() noexcept {
    if (!transport_) {
        return Status::error(ErrorCode::ConnectFailed, "连接已释放");
    }

    if (ssn_transport_connect_finish_internal(transport_) == SSN_CONNECT_CONNECTED) {
        return {};
    }
    return Status::error(ErrorCode::ConnectFailed, "连接建立失败");
}

void NodeBackend::close() noexcept {
    if (transport_ != nullptr) {
        ssn_transport_destroy(transport_);
        transport_ = nullptr;
    }
}

}  // namespace ssn::detail
