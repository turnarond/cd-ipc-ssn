#ifndef SSN_NODE_STATUS_HPP
#define SSN_NODE_STATUS_HPP

#include "ssn_export.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace ssn {

enum class ErrorCode : std::uint16_t {
    Ok = 0,
    InvalidArgument,
    InvalidState,
    AddressError,
    ConnectFailed,
    Timeout,
    IoError,
    ProtocolError,
    NotFound,
    ResourceLimit,
    QueueFull,
    CallbackError,
    WouldDeadlock
};

enum class ErrorCategory : std::uint8_t {
    None = 0,
    Argument,
    State,
    Address,
    Connection,
    Timeout,
    Io,
    Protocol,
    Resource,
    Backpressure,
    Callback,
    Deadlock
};

constexpr ErrorCategory error_category(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::Ok:
        return ErrorCategory::None;
    case ErrorCode::InvalidArgument:
        return ErrorCategory::Argument;
    case ErrorCode::InvalidState:
        return ErrorCategory::State;
    case ErrorCode::AddressError:
        return ErrorCategory::Address;
    case ErrorCode::ConnectFailed:
        return ErrorCategory::Connection;
    case ErrorCode::Timeout:
        return ErrorCategory::Timeout;
    case ErrorCode::IoError:
        return ErrorCategory::Io;
    case ErrorCode::ProtocolError:
        return ErrorCategory::Protocol;
    case ErrorCode::NotFound:
    case ErrorCode::ResourceLimit:
        return ErrorCategory::Resource;
    case ErrorCode::QueueFull:
        return ErrorCategory::Backpressure;
    case ErrorCode::CallbackError:
        return ErrorCategory::Callback;
    case ErrorCode::WouldDeadlock:
        return ErrorCategory::Deadlock;
    }
    return ErrorCategory::None;
}

class SSN_FRAMEWORK_API Status final {
public:
    Status() noexcept;
    Status(const Status& other);
    Status& operator=(const Status& other);
    Status(Status&& other) noexcept;
    Status& operator=(Status&& other) noexcept;
    ~Status() noexcept;

    static Status error(ErrorCode code, std::string_view message = {},
                        int system_error = 0);

    bool ok() const noexcept;
    explicit operator bool() const noexcept { return ok(); }
    ErrorCode code() const noexcept;
    ErrorCategory category() const noexcept;
    int system_error() const noexcept;

    // 返回的视图在本对象被修改或销毁之前保持有效。
    std::string_view message() const noexcept;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

namespace detail {

[[noreturn]] SSN_FRAMEWORK_API void throw_bad_result_access(
    std::string_view diagnostic);

}  // namespace detail

template <typename T>
class Result final {
public:
    static Result success(T value) {
        return Result(std::move(value));
    }

    static Result failure(Status status) {
        if (status.ok()) {
            status = Status::error(
                ErrorCode::InvalidState, "Result failure requires an error status");
        }
        return Result(std::move(status));
    }

    Result(T value)
        : status_(), value_(std::move(value)) {}

    Result(Status status)
        : status_(status.ok()
                      ? Status::error(ErrorCode::InvalidState,
                                      "Result error requires an error status")
                      : std::move(status)) {}

    bool ok() const noexcept { return value_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }
    const Status& status() const noexcept { return status_; }

    T& value() & {
        require_value();
        return *value_;
    }

    const T& value() const& {
        require_value();
        return *value_;
    }

    T&& value() && {
        require_value();
        return std::move(*value_);
    }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }
    T&& operator*() && { return std::move(*this).value(); }

private:
    void require_value() const {
        if (!value_) {
            detail::throw_bad_result_access(status_.message());
        }
    }

    Status status_;
    std::optional<T> value_;
};

}  // namespace ssn

#endif  // SSN_NODE_STATUS_HPP
