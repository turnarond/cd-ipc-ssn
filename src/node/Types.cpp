#include "ssn/node/Types.hpp"

#include <string>
#include <utility>

namespace ssn {

class OwnedText::Impl final {
public:
    explicit Impl(std::string_view value)
        : text(value) {}

    std::string text;
};

OwnedText::OwnedText() noexcept = default;

OwnedText::OwnedText(const char* text)
    : OwnedText(text ? std::string_view{text} : std::string_view{}) {}

OwnedText::OwnedText(std::string_view text)
    : impl_(text.empty() ? nullptr : std::make_unique<Impl>(text)) {}

OwnedText::OwnedText(const OwnedText& other)
    : impl_(other.impl_ ? std::make_unique<Impl>(*other.impl_) : nullptr) {}

OwnedText& OwnedText::operator=(const OwnedText& other) {
    if (this != &other) {
        OwnedText copy(other);
        impl_.swap(copy.impl_);
    }
    return *this;
}

OwnedText::OwnedText(OwnedText&& other) noexcept = default;
OwnedText& OwnedText::operator=(OwnedText&& other) noexcept = default;

OwnedText& OwnedText::operator=(std::string_view text) {
    OwnedText copy(text);
    impl_.swap(copy.impl_);
    return *this;
}

OwnedText& OwnedText::operator=(const char* text) {
    return operator=(text ? std::string_view{text} : std::string_view{});
}

OwnedText::~OwnedText() noexcept = default;

std::string_view OwnedText::view() const noexcept {
    return impl_ ? std::string_view{impl_->text} : std::string_view{};
}

const char* OwnedText::data() const noexcept {
    return impl_ ? impl_->text.data() : nullptr;
}

std::size_t OwnedText::size() const noexcept {
    return impl_ ? impl_->text.size() : 0;
}

bool OwnedText::empty() const noexcept {
    return size() == 0;
}

}  // namespace ssn
