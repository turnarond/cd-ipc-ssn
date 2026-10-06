#ifndef SSN_NODE_TYPES_HPP
#define SSN_NODE_TYPES_HPP

#include "ssn/node/Status.hpp"
#include "ssn_export.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>

namespace ssn {

struct PeerId final {
    std::uint32_t slot{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t generation{0};

    constexpr bool valid() const noexcept {
        return slot != std::numeric_limits<std::uint32_t>::max() && generation != 0;
    }
};

constexpr bool operator==(PeerId lhs, PeerId rhs) noexcept {
    return lhs.slot == rhs.slot && lhs.generation == rhs.generation;
}

constexpr bool operator!=(PeerId lhs, PeerId rhs) noexcept {
    return !(lhs == rhs);
}

class ByteView final {
public:
    constexpr ByteView() noexcept = default;
    constexpr ByteView(const std::byte* data, std::size_t size) noexcept
        : data_(data), size_(size) {}

    constexpr const std::byte* data() const noexcept { return data_; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }
    constexpr bool valid() const noexcept { return data_ != nullptr || size_ == 0; }

private:
    const std::byte* data_{nullptr};
    std::size_t size_{0};
};

class Message;

class MessageView final {
public:
    constexpr MessageView() noexcept = default;
    constexpr MessageView(const std::byte* data, std::size_t size) noexcept
        : data_(data), size_(size) {}
    constexpr explicit MessageView(ByteView bytes) noexcept
        : data_(bytes.data()), size_(bytes.size()) {}

    constexpr const std::byte* data() const noexcept { return data_; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }
    constexpr bool valid() const noexcept { return data_ != nullptr || size_ == 0; }
    constexpr ByteView bytes() const noexcept { return ByteView{data_, size_}; }
    SSN_FRAMEWORK_API Result<Message> copy() const;

private:
    const std::byte* data_{nullptr};
    std::size_t size_{0};
};

class SSN_FRAMEWORK_API Message final {
public:
    Message();
    Message(const Message& other);
    Message& operator=(const Message& other);
    Message(Message&& other) noexcept;
    Message& operator=(Message&& other) noexcept;
    ~Message() noexcept;

    const std::byte* data() const noexcept;
    std::size_t size() const noexcept;
    bool empty() const noexcept;
    ByteView bytes() const noexcept;
    static Result<Message> copy(ByteView bytes);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class SSN_FRAMEWORK_API OwnedText final {
public:
    OwnedText() noexcept;
    OwnedText(const char* text);
    OwnedText(std::string_view text);
    OwnedText(const OwnedText& other);
    OwnedText& operator=(const OwnedText& other);
    OwnedText(OwnedText&& other) noexcept;
    OwnedText& operator=(OwnedText&& other) noexcept;
    OwnedText& operator=(std::string_view text);
    // 字符串字面量赋值需要此重载：仅有 string_view 重载时，"text" 经
    // OwnedText(const char*) 与 std::string_view 两条用户定义转换都成立，
    // 与 copy/move 赋值产生重载歧义（编译失败）。
    OwnedText& operator=(const char* text);
    ~OwnedText() noexcept;

    // 返回的视图在本对象被修改或销毁之前保持有效。
    std::string_view view() const noexcept;
    const char* data() const noexcept;
    std::size_t size() const noexcept;
    bool empty() const noexcept;
    explicit operator std::string_view() const noexcept { return view(); }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

enum class PeerDirection : std::uint8_t {
    Inbound = 0,
    Outbound
};

enum class NodeState : std::uint8_t {
    Created = 0,
    Running,
    Stopping,
    Stopped,
    Failed
};

enum class PeerState : std::uint8_t {
    Connecting = 0,
    Connected,
    Closing,
    Disconnected,
    ConnectFailed,
    Failed
};

enum class NodeEventType : std::uint8_t {
    None = 0,
    PeerConnected,
    PeerDisconnected,
    MessageReceived,
    Error,
    BackpressureOn,
    BackpressureOff
};

struct NodeConfig final {
    std::size_t max_peers{1024};
    std::size_t max_peer_queue_bytes{1024 * 1024};
    std::size_t max_total_queue_bytes{16 * 1024 * 1024};
    std::size_t queue_high_watermark_bytes{768 * 1024};
    std::size_t queue_low_watermark_bytes{512 * 1024};
    std::size_t max_events_per_poll{256};
    std::chrono::milliseconds shutdown_timeout{5000};
};

struct ListenAddress final {
    OwnedText address;
};

struct ConnectOptions final {
    std::chrono::milliseconds timeout{5000};
};

struct PeerInfo final {
    PeerId id;
    PeerDirection direction{PeerDirection::Inbound};
    PeerState state{PeerState::Disconnected};
    OwnedText address;
    std::size_t queued_bytes{0};
};

struct NodeEvent final {
    NodeEventType type{NodeEventType::None};
    PeerId peer;
    MessageView message;
    Status status;
    std::size_t queued_bytes{0};
};

}  // namespace ssn

#endif  // SSN_NODE_TYPES_HPP
