#include "PeerRegistry.hpp"

namespace ssn::detail {

PeerRegistry::PeerRegistry(std::size_t capacity)
    : slots_(capacity) {}

Result<PeerId> PeerRegistry::allocate(PeerDirection direction,
                                      std::string_view address) {
    PeerId id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            auto& slot = slots_[index];
            if (slot.session || slot.reserved) {
                continue;
            }

            slot.reserved = true;
            id = PeerId{static_cast<std::uint32_t>(index), slot.generation};
            break;
        }
    }

    if (!id.valid()) {
        return Result<PeerId>::failure(Status::error(
            ErrorCode::ResourceLimit, "peer registry capacity reached"));
    }

    std::shared_ptr<PeerSession> session;
    try {
        session = std::make_shared<PeerSession>(id, direction, address);
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& slot = slots_[id.slot];
        if (slot.reserved && !slot.session &&
            slot.generation == id.generation) {
            slot.reserved = false;
        }
        throw;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& slot = slots_[id.slot];
        slot.session = std::move(session);
        slot.reserved = false;
        size_.fetch_add(1, std::memory_order_relaxed);
    }
    return Result<PeerId>::success(id);
}

std::shared_ptr<PeerSession> PeerRegistry::find(PeerId id) const {
    if (!id.valid()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (id.slot >= slots_.size()) {
        return nullptr;
    }

    const auto& slot = slots_[id.slot];
    if (!slot.session || slot.generation != id.generation) {
        return nullptr;
    }
    return slot.session;
}

std::vector<PeerInfo> PeerRegistry::snapshot() const {
    std::vector<std::shared_ptr<PeerSession>> sessions;
    sessions.reserve(slots_.size());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& slot : slots_) {
            if (slot.session) {
                sessions.push_back(slot.session);
            }
        }
    }

    std::vector<PeerInfo> result;
    result.reserve(sessions.size());
    for (const auto& session : sessions) {
        result.push_back(session->snapshot());
    }
    return result;
}

Status PeerRegistry::markClosing(PeerId id) {
    const auto session = find(id);
    if (!session) {
        return Status::error(ErrorCode::NotFound, "peer not found");
    }
    return session->markClosing();
}

Status PeerRegistry::transition(PeerId id, PeerState target) {
    const auto session = find(id);
    if (!session) {
        return Status::error(ErrorCode::NotFound, "peer not found");
    }
    return session->transition(target);
}

Status PeerRegistry::erase(PeerId id) {
    if (!id.valid()) {
        return Status::error(ErrorCode::NotFound, "peer not found");
    }

    std::shared_ptr<PeerSession> removed;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (id.slot < slots_.size()) {
            auto& slot = slots_[id.slot];
            if (slot.session && slot.generation == id.generation) {
                removed = std::move(slot.session);
                slot.generation = nextPeerGeneration(slot.generation);
                size_.fetch_sub(1, std::memory_order_relaxed);
                found = true;
            }
        }
    }
    if (!found) {
        return Status::error(ErrorCode::NotFound, "peer not found");
    }
    return Status{};
}

std::size_t PeerRegistry::size() const noexcept {
    return size_.load(std::memory_order_relaxed);
}

}  // namespace ssn::detail
