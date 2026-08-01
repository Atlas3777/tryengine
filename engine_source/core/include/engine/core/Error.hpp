#pragma once

#include <string>
#include <string_view>

#include "engine/core/ErrorCode.hpp"

namespace tryengine::core {

class Error {
public:
    explicit constexpr Error(ErrorCode code) noexcept : code_(code) {}

    explicit constexpr Error(ErrorCode code, std::string message) : code_(code), message_(std::move(message)) {}

    explicit constexpr Error(std::string message) : message_(std::move(message)) {}

    [[nodiscard]]
    constexpr ErrorCode Code() const noexcept {
        return code_;
    }

    [[nodiscard]]
    bool Is(ErrorCode code) const noexcept {
        return code_ == code;
    }

    [[nodiscard]]
    friend bool operator==(const Error& lhs, ErrorCode rhs) noexcept {
        return lhs.code_ == rhs;
    }

    [[nodiscard]]
    friend bool operator==(ErrorCode lhs, const Error& rhs) noexcept {
        return lhs == rhs.code_;
    }

    [[nodiscard]]
    std::string_view Message() const noexcept {
        if (message_.empty())
            return ToString(code_);

        return message_;
    }

private:
    ErrorCode code_ = ErrorCode::Unknown;
    std::string message_;
};

}  // namespace tryengine::core