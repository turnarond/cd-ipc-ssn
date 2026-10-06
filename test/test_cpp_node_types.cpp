#include "ssn/node/Status.hpp"
#include "ssn/node/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

static int g_cpp_passed = 0;
static int g_cpp_failed = 0;
#define CHECK(cond) do { if (cond) { ++g_cpp_passed; } else { ++g_cpp_failed; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace {

void test_status_reports_success_and_diagnostics() {
    const ssn::Status ok;
    CHECK(ok.ok());
    CHECK(ok.code() == ssn::ErrorCode::Ok);
    CHECK(ok.category() == ssn::ErrorCategory::None);
    CHECK(ok.system_error() == 0);
    CHECK(ok.message().empty());

    const auto error = ssn::Status::error(
        ssn::ErrorCode::IoError, "read failed", 104);
    CHECK(!error.ok());
    CHECK(error.code() == ssn::ErrorCode::IoError);
    CHECK(error.category() == ssn::ErrorCategory::Io);
    CHECK(error.system_error() == 104);
    CHECK(error.message() == std::string_view{"read failed"});
}

void test_result_success_and_failure_are_distinct() {
    auto success = ssn::Result<int>::success(42);
    CHECK(success.ok());
    CHECK(success.status().ok());
    CHECK(success.value() == 42);
    CHECK(*success == 42);
    CHECK(success.operator->() != nullptr);

    auto failure = ssn::Result<int>::failure(
        ssn::Status::error(ssn::ErrorCode::InvalidState, "not running"));
    CHECK(!failure.ok());
    CHECK(failure.status().code() == ssn::ErrorCode::InvalidState);

    bool value_failed_explicitly = false;
    try {
        static_cast<void>(failure.value());
    } catch (const std::logic_error&) {
        value_failed_explicitly = true;
    }
    CHECK(value_failed_explicitly);
}

void test_peer_id_compares_slot_and_generation() {
    const ssn::PeerId invalid;
    const ssn::PeerId invalid_slot{
        std::numeric_limits<std::uint32_t>::max(), 1};
    const ssn::PeerId zero_generation{7, 0};
    const ssn::PeerId first{7, 1};
    const ssn::PeerId same{7, 1};
    const ssn::PeerId next_generation{7, 2};
    const ssn::PeerId next_slot{8, 1};

    CHECK(!invalid.valid());
    CHECK(!invalid_slot.valid());
    CHECK(!zero_generation.valid());
    CHECK(first.valid());
    CHECK(first == same);
    CHECK(first != next_generation);
    CHECK(first != next_slot);
}

void test_message_view_copy_owns_bytes() {
    std::array<std::byte, 3> source{
        std::byte{1}, std::byte{2}, std::byte{3}};
    const ssn::MessageView view{source.data(), source.size()};
    auto owned_result = view.copy();
    CHECK(owned_result.ok());
    ssn::Message owned = std::move(owned_result).value();

    source[0] = std::byte{9};
    CHECK(view.data()[0] == std::byte{9});
    CHECK(owned.size() == 3);
    CHECK(owned.data()[0] == std::byte{1});

    ssn::Message copied = owned;
    ssn::Message moved = std::move(copied);
    CHECK(moved.size() == 3);
    CHECK(moved.data()[2] == std::byte{3});
    CHECK(copied.empty());
    CHECK(copied.data() == nullptr);

    ssn::Message copy_assigned;
    copy_assigned = owned;
    CHECK(copy_assigned.size() == 3);
    CHECK(copy_assigned.data()[0] == std::byte{1});

    ssn::Message move_assigned;
    move_assigned = std::move(copy_assigned);
    CHECK(move_assigned.size() == 3);
    CHECK(copy_assigned.empty());
    CHECK(copy_assigned.data() == nullptr);

    ssn::Message copied_from_moved = copy_assigned;
    CHECK(copied_from_moved.empty());
}

void test_empty_views_and_messages_are_safe() {
    const ssn::ByteView bytes;
    const ssn::MessageView view;
    auto message_result = view.copy();

    CHECK(bytes.empty());
    CHECK(bytes.data() == nullptr);
    CHECK(bytes.size() == 0);
    CHECK(view.empty());
    CHECK(view.data() == nullptr);
    CHECK(view.size() == 0);
    CHECK(message_result.ok());
    const ssn::Message& message = message_result.value();
    CHECK(message.empty());
    CHECK(message.data() == nullptr);
    CHECK(message.size() == 0);
}

void test_invalid_views_report_invalid_argument() {
    const ssn::ByteView invalid_bytes{nullptr, 3};
    const ssn::MessageView invalid_view{nullptr, 3};

    CHECK(!invalid_bytes.valid());
    CHECK(!invalid_view.valid());

    auto from_bytes = ssn::Message::copy(invalid_bytes);
    CHECK(!from_bytes.ok());
    CHECK(from_bytes.status().code() == ssn::ErrorCode::InvalidArgument);

    auto from_view = invalid_view.copy();
    CHECK(!from_view.ok());
    CHECK(from_view.status().code() == ssn::ErrorCode::InvalidArgument);
}

void test_configuration_defaults_are_bounded() {
    const ssn::NodeConfig config;
    CHECK(config.max_peers > 0);
    CHECK(config.max_peer_queue_bytes > 0);
    CHECK(config.max_total_queue_bytes >= config.max_peer_queue_bytes);
    CHECK(config.queue_low_watermark_bytes < config.queue_high_watermark_bytes);
    CHECK(config.queue_high_watermark_bytes <= config.max_peer_queue_bytes);
    CHECK(config.max_events_per_poll > 0);
    CHECK(config.shutdown_timeout.count() > 0);

    std::string source_address{"tcp://127.0.0.1:19001"};
    const ssn::ListenAddress listen{std::string_view{source_address}};
    source_address[0] = 'x';
    const ssn::ConnectOptions connect;
    ssn::PeerInfo peer;
    peer.address = std::string_view{"unix:///tmp/ssn"};
    const ssn::PeerInfo peer_copy = peer;
    const ssn::NodeEvent event;
    CHECK(listen.address.view() == std::string_view{"tcp://127.0.0.1:19001"});
    CHECK(connect.timeout.count() > 0);
    CHECK(!peer.id.valid());
    CHECK(peer.state == ssn::PeerState::Disconnected);
    CHECK(peer_copy.address.view() == std::string_view{"unix:///tmp/ssn"});
    CHECK(event.type == ssn::NodeEventType::None);
    CHECK(!event.peer.valid());
}

static_assert(sizeof(ssn::PeerId) == sizeof(std::uint32_t) * 2,
              "PeerId must remain two uint32_t fields");
static_assert(sizeof(ssn::ByteView) == sizeof(const std::byte*) + sizeof(std::size_t),
              "ByteView must remain pointer plus length");
static_assert(sizeof(ssn::Message) == sizeof(void*),
              "Message must retain a fixed pointer-only public layout");
static_assert(sizeof(ssn::Status) == sizeof(void*),
              "Status must not expose owning diagnostic storage");
static_assert(sizeof(ssn::OwnedText) == sizeof(void*),
              "OwnedText must hide owning text storage behind one pointer");
static_assert(std::is_same<decltype(std::declval<const ssn::Status&>().message()),
                           std::string_view>::value,
              "Status diagnostic access must be non-owning");
static_assert(std::is_nothrow_move_constructible<ssn::Message>::value,
              "Message move construction must be noexcept");

}  // namespace

int main() {
    test_status_reports_success_and_diagnostics();
    test_result_success_and_failure_are_distinct();
    test_peer_id_compares_slot_and_generation();
    test_message_view_copy_owns_bytes();
    test_empty_views_and_messages_are_safe();
    test_invalid_views_report_invalid_argument();
    test_configuration_defaults_are_bounded();
    std::printf("C++ node type results: %d/%d passed\n",
                g_cpp_passed, g_cpp_passed + g_cpp_failed);
    return g_cpp_failed == 0 ? 0 : 1;
}
