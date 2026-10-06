#include "ssn/node/Status.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace ssn {

class Status::Impl final {
public:
    Impl(ErrorCode error_code, std::string_view diagnostic, int native_error)
        : code(error_code), system_error(native_error), message(diagnostic) {}

    ErrorCode code;
    int system_error;
    std::string message;
};

Status::Status() noexcept = default;

Status::Status(const Status& other)
    : impl_(other.impl_ ? std::make_unique<Impl>(*other.impl_) : nullptr) {}

Status& Status::operator=(const Status& other) {
    if (this != &other) {
        Status copy(other);
        impl_.swap(copy.impl_);
    }
    return *this;
}

Status::Status(Status&& other) noexcept = default;
Status& Status::operator=(Status&& other) noexcept = default;
Status::~Status() noexcept = default;

Status Status::error(ErrorCode code, std::string_view message, int system_error) {
    if (code == ErrorCode::Ok) {
        return Status{};
    }
    Status result;
    result.impl_ = std::make_unique<Impl>(code, message, system_error);
    return result;
}

bool Status::ok() const noexcept {
    return impl_ == nullptr;
}

ErrorCode Status::code() const noexcept {
    return impl_ ? impl_->code : ErrorCode::Ok;
}

ErrorCategory Status::category() const noexcept {
    return error_category(code());
}

int Status::system_error() const noexcept {
    return impl_ ? impl_->system_error : 0;
}

std::string_view Status::message() const noexcept {
    return impl_ ? std::string_view{impl_->message} : std::string_view{};
}

namespace detail {

[[noreturn]] void throw_bad_result_access(std::string_view diagnostic) {
    throw std::logic_error(diagnostic.empty()
                               ? std::string{"Result has no value"}
                               : std::string{diagnostic});
}

}  // namespace detail

}  // namespace ssn
