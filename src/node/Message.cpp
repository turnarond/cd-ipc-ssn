#include "ssn/node/Types.hpp"

#include <utility>
#include <vector>

namespace ssn {

class Message::Impl final {
public:
    Impl() = default;

    void assign(const std::byte* data, std::size_t size) {
        if (size != 0) {
            storage.assign(data, data + size);
        }
    }

    std::vector<std::byte> storage;
};

Message::Message()
    : impl_(std::make_unique<Impl>()) {}

Message::Message(const Message& other)
    : impl_(other.impl_ ? std::make_unique<Impl>(*other.impl_)
                        : std::make_unique<Impl>()) {}

Message& Message::operator=(const Message& other) {
    if (this != &other) {
        Message copy(other);
        impl_.swap(copy.impl_);
    }
    return *this;
}

Message::Message(Message&& other) noexcept = default;
Message& Message::operator=(Message&& other) noexcept = default;
Message::~Message() noexcept = default;

const std::byte* Message::data() const noexcept {
    return empty() ? nullptr : impl_->storage.data();
}

std::size_t Message::size() const noexcept {
    return impl_ ? impl_->storage.size() : 0;
}

bool Message::empty() const noexcept {
    return size() == 0;
}

ByteView Message::bytes() const noexcept {
    return ByteView{data(), size()};
}

Result<Message> Message::copy(ByteView bytes) {
    if (!bytes.valid()) {
        return Status::error(
            ErrorCode::InvalidArgument,
            "non-empty ByteView requires a non-null data pointer");
    }

    Message message;
    message.impl_->assign(bytes.data(), bytes.size());
    return Result<Message>::success(std::move(message));
}

Result<Message> MessageView::copy() const {
    return Message::copy(bytes());
}

}  // namespace ssn
