#ifndef SSN_NODE_NODE_BACKEND_HPP
#define SSN_NODE_NODE_BACKEND_HPP

#include "ssn/node/Status.hpp"
#include "transports/ssn_transport.h"

#include <string_view>

namespace ssn::detail {

// 单条出站连接的传输封装（RAII）：解析地址、建传输、发起非阻塞连接、判定结果。
// 私有实现：不安装、不导出，只被 NodeImpl 使用。
class NodeBackend final {
public:
    enum class ConnectState {
        Connected,
        InProgress,
        Failed
    };

    // 解析地址并创建对应传输（失败时返回 AddressError / ResourceLimit）
    static Result<NodeBackend> create(std::string_view address);

    // 创建监听后端：create + bind + listen(backlog) + listener fd 置 O_NONBLOCK。
    // bind/listen 失败时返回真实系统错误（AddressError / IoError），不假成功。
    static Result<NodeBackend> createListener(std::string_view address,
                                              int backlog);

    // 从监听后端接受一个入站连接；listener fd 已置 O_NONBLOCK。
    // 无可接受连接（EAGAIN）返回失败但 code 为 Ok（调用方按「本轮无连接」处理）。
    Result<NodeBackend> acceptOne();

    NodeBackend(NodeBackend&& other) noexcept;
    NodeBackend& operator=(NodeBackend&& other) noexcept;
    NodeBackend(const NodeBackend&) = delete;
    NodeBackend& operator=(const NodeBackend&) = delete;
    ~NodeBackend() noexcept;

    int fd() const noexcept;
    // 对端地址字符串（accept/connect 后有效）
    std::string_view peerAddress() const noexcept;
    // 非阻塞原始收发（fd 已 O_NONBLOCK）：返回字节数；-1=EAGAIN 或错误（errno 区分）
    int sendRaw(const void* data, std::size_t len) noexcept;
    int recvRaw(void* buf, std::size_t size) noexcept;
    ConnectState beginConnect() noexcept;
    // 仅在 fd 可写后调用：Ok 表示连接建立，其余为 ConnectFailed
    Status finishConnect() noexcept;
    void close() noexcept;

private:
    NodeBackend() = default;

    ssn_transport_t* transport_{nullptr};
    ssn_address_t address_{};
};

}  // namespace ssn::detail

#endif  // SSN_NODE_NODE_BACKEND_HPP
