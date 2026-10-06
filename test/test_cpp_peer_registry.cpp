#include "node/PeerRegistry.hpp"

#include <cstdio>
#include <limits>
#include <string_view>

static int g_cpp_passed = 0;
static int g_cpp_failed = 0;
#define CHECK(cond) do { if (cond) { ++g_cpp_passed; } else { ++g_cpp_failed; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace {

void test_capacity_reuse_rejects_stale_peer_ids() {
    ssn::detail::PeerRegistry peers{2};

    const auto first = peers.allocate(
        ssn::PeerDirection::Outbound, "tcp://127.0.0.1:19001");
    const auto second = peers.allocate(
        ssn::PeerDirection::Inbound, "unix:///tmp/ssn-peer");
    const auto overflow = peers.allocate(
        ssn::PeerDirection::Outbound, "tcp://127.0.0.1:19002");

    CHECK(first.ok());
    CHECK(second.ok());
    CHECK(first.value().generation == 1);
    CHECK(second.value().generation == 1);
    CHECK(peers.size() == 2);
    CHECK(!overflow.ok());
    CHECK(overflow.status().code() == ssn::ErrorCode::ResourceLimit);
    CHECK(peers.find(first.value()) != nullptr);
    CHECK(peers.erase(first.value()).ok());
    CHECK(peers.size() == 1);

    const auto replacement = peers.allocate(
        ssn::PeerDirection::Outbound, "tcp://127.0.0.1:19003");
    CHECK(replacement.ok());
    CHECK(replacement.value().slot == first.value().slot);
    CHECK(replacement.value().generation != first.value().generation);
    CHECK(peers.find(first.value()) == nullptr);
    CHECK(peers.find(replacement.value()) != nullptr);

    const auto stale_erase = peers.erase(first.value());
    CHECK(!stale_erase.ok());
    CHECK(stale_erase.code() == ssn::ErrorCode::NotFound);
    const auto stale_transition = peers.transition(
        first.value(), ssn::PeerState::Connected);
    CHECK(!stale_transition.ok());
    CHECK(stale_transition.code() == ssn::ErrorCode::NotFound);
    CHECK(peers.size() == 2);
}

void test_zero_capacity_never_allocates() {
    ssn::detail::PeerRegistry peers{0};
    const auto result = peers.allocate(
        ssn::PeerDirection::Outbound, "tcp://127.0.0.1:19001");

    CHECK(!result.ok());
    CHECK(result.status().code() == ssn::ErrorCode::ResourceLimit);
    CHECK(peers.size() == 0);
    CHECK(peers.snapshot().empty());
}

void test_generation_increment_skips_zero_on_wraparound() {
    CHECK(ssn::detail::nextPeerGeneration(1) == 2);
    CHECK(ssn::detail::nextPeerGeneration(
              std::numeric_limits<std::uint32_t>::max()) == 1);
}

void test_connected_peer_can_close() {
    ssn::detail::PeerRegistry peers{1};
    const auto peer = peers.allocate(
        ssn::PeerDirection::Outbound, "tcp://127.0.0.1:19001");
    CHECK(peer.ok());
    CHECK(peers.transition(peer.value(), ssn::PeerState::Connected).ok());
    CHECK(peers.find(peer.value())->state() == ssn::PeerState::Connected);
    CHECK(peers.markClosing(peer.value()).ok());
    CHECK(peers.find(peer.value())->state() == ssn::PeerState::Closing);
}

void test_terminal_peer_states_reject_closing() {
    const ssn::PeerState terminal_states[] = {
        ssn::PeerState::Disconnected,
        ssn::PeerState::ConnectFailed,
        ssn::PeerState::Failed,
    };

    for (const auto terminal_state : terminal_states) {
        ssn::detail::PeerRegistry peers{1};
        const auto peer = peers.allocate(
            ssn::PeerDirection::Outbound, "tcp://127.0.0.1:19001");
        CHECK(peer.ok());

        if (terminal_state == ssn::PeerState::Disconnected) {
            CHECK(peers.markClosing(peer.value()).ok());
        } else if (terminal_state == ssn::PeerState::Failed) {
            CHECK(peers.transition(peer.value(), ssn::PeerState::Connected).ok());
        }
        CHECK(peers.transition(peer.value(), terminal_state).ok());

        const auto closing = peers.markClosing(peer.value());
        CHECK(!closing.ok());
        CHECK(closing.code() == ssn::ErrorCode::InvalidState);
        CHECK(peers.find(peer.value())->state() == terminal_state);
    }
}

void test_closing_is_idempotent_and_snapshot_owns_sorted_values() {
    ssn::detail::PeerRegistry peers{3};
    const auto first = peers.allocate(
        ssn::PeerDirection::Outbound, "tcp://127.0.0.1:19001");
    const auto second = peers.allocate(
        ssn::PeerDirection::Inbound, "unix:///tmp/ssn-peer");

    CHECK(first.ok());
    CHECK(second.ok());
    const auto first_session = peers.find(first.value());
    CHECK(first_session != nullptr);
    CHECK(first_session->id() == first.value());
    CHECK(first_session->direction() == ssn::PeerDirection::Outbound);
    CHECK(first_session->address() == std::string_view{"tcp://127.0.0.1:19001"});
    CHECK(first_session->state() == ssn::PeerState::Connecting);

    CHECK(peers.markClosing(first.value()).ok());
    CHECK(peers.markClosing(first.value()).ok());
    CHECK(first_session->state() == ssn::PeerState::Closing);

    const auto snapshot = peers.snapshot();
    CHECK(snapshot.size() == 2);
    CHECK(snapshot[0].id == first.value());
    CHECK(snapshot[0].id.slot < snapshot[1].id.slot);
    CHECK(snapshot[0].direction == ssn::PeerDirection::Outbound);
    CHECK(snapshot[0].state == ssn::PeerState::Closing);
    CHECK(snapshot[0].address.view() ==
          std::string_view{"tcp://127.0.0.1:19001"});
    CHECK(snapshot[1].id == second.value());
    CHECK(snapshot[1].direction == ssn::PeerDirection::Inbound);
    CHECK(snapshot[1].address.view() == std::string_view{"unix:///tmp/ssn-peer"});

    CHECK(peers.erase(first.value()).ok());
    CHECK(first_session->id() == first.value());
    CHECK(first_session->address() ==
          std::string_view{"tcp://127.0.0.1:19001"});
    CHECK(first_session->state() == ssn::PeerState::Closing);
    CHECK(snapshot[0].address.view() ==
          std::string_view{"tcp://127.0.0.1:19001"});
    CHECK(peers.snapshot().size() == 1);

    const auto repeated_erase = peers.erase(first.value());
    CHECK(!repeated_erase.ok());
    CHECK(repeated_erase.code() == ssn::ErrorCode::NotFound);
    const auto stale_close = peers.markClosing(first.value());
    CHECK(!stale_close.ok());
    CHECK(stale_close.code() == ssn::ErrorCode::NotFound);
}

}  // namespace

int main() {
    test_capacity_reuse_rejects_stale_peer_ids();
    test_zero_capacity_never_allocates();
    test_generation_increment_skips_zero_on_wraparound();
    test_connected_peer_can_close();
    test_terminal_peer_states_reject_closing();
    test_closing_is_idempotent_and_snapshot_owns_sorted_values();
    std::printf("C++ peer registry results: %d/%d passed\n",
                g_cpp_passed, g_cpp_passed + g_cpp_failed);
    return g_cpp_failed == 0 ? 0 : 1;
}
