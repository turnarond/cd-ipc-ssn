#ifndef SSN_NODE_PEER_REGISTRY_HPP
#define SSN_NODE_PEER_REGISTRY_HPP

#include "PeerSession.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace ssn::detail {

constexpr std::uint32_t nextPeerGeneration(std::uint32_t current) noexcept {
    ++current;
    return current == 0 ? 1 : current;
}

class PeerRegistry final {
public:
    explicit PeerRegistry(std::size_t capacity);

    Result<PeerId> allocate(PeerDirection direction, std::string_view address);
    std::shared_ptr<PeerSession> find(PeerId id) const;
    std::vector<PeerInfo> snapshot() const;
    Status markClosing(PeerId id);
    Status transition(PeerId id, PeerState target);
    Status erase(PeerId id);
    std::size_t size() const noexcept;

private:
    struct Slot final {
        std::uint32_t generation{1};
        bool reserved{false};
        std::shared_ptr<PeerSession> session;
    };

    mutable std::mutex mutex_;
    std::vector<Slot> slots_;
    std::atomic<std::size_t> size_{0};
};

}  // namespace ssn::detail

#endif  // SSN_NODE_PEER_REGISTRY_HPP
