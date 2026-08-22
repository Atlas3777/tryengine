#pragma once

#include <EASTL/string.h>
#include <expected>
#include <format>

#include "Assert.hpp"
#include "ErrorCode.hpp"

namespace tryengine::core {

class [[nodiscard]] Error {
public:
    explicit constexpr Error(ErrorCode code) noexcept : code_(code) {}
    explicit constexpr Error(ErrorCode code, eastl::string message) noexcept
        : code_(code), message_(eastl::move(message)) {}
    explicit constexpr Error(eastl::string message) : message_(eastl::move(message)) {}

    Error(Error&& other) noexcept : code_(other.code_), message_(std::move(other.message_)) {
#ifndef NDEBUG
        checked_ = other.checked_;
        other.checked_ = true;
#endif
    }

    Error& operator=(Error&& other) noexcept {
        if (this != &other) {
            CheckUnhandled();
            code_ = other.code_;
            message_ = std::move(other.message_);
#ifndef NDEBUG
            checked_ = other.checked_;
            other.checked_ = true;
#endif
        }
        return *this;
    }

    ~Error() { CheckUnhandled(); }

    [[nodiscard]] ErrorCode Code() const noexcept {
        MarkChecked();
        return code_;
    }

    [[nodiscard]] eastl::string_view Message() const noexcept {
        MarkChecked();
        if (message_.empty())
            return ToString(code_);
        return message_;
    }

    // Если ошибку явно заглушили или обработали
    void Discard() const noexcept { MarkChecked(); }

    // template <typename T>
    // [[nodiscard]] constexpr operator std::expected<T, Error>() const& {
    //     return std::unexpected<Error>(*this);
    // }

    template <typename T>
    [[nodiscard]] constexpr operator std::expected<T, Error>() && {
        return std::unexpected<Error>(std::move(*this));
    }

private:
    void MarkChecked() const noexcept {
#ifndef NDEBUG
        checked_ = true;
#endif
    }

    void CheckUnhandled() const noexcept {
#ifndef NDEBUG
        if (!checked_ && code_ != ErrorCode::Unknown) {
            // Если вы дошли сюда — ошибка была создана/возвращена, но её никто не прочитал!
            TRY_ASSERT(false, "Unchecked Error destroyed! Message: {}", message_.c_str());
        }
#endif
    }

    ErrorCode code_ = ErrorCode::Unknown;
    eastl::string message_;

#ifndef NDEBUG
    mutable bool checked_ = false;
#endif
};

}  // namespace tryengine::core

// ---------------------------------------------------------------------------
// Позволяет писать TRY_LOG("{}", err) / std::format("{}", err) напрямую,
// без err.Message().c_str() на месте вызова. Вызывает Message(), а значит
// автоматически помечает ошибку как "проверенную" (checked_ = true) — что
// логично: если вы её отформатировали, вы её уже прочитали.
// ---------------------------------------------------------------------------
template <>
struct std::formatter<tryengine::core::Error> : std::formatter<std::string_view> {
    auto format(const tryengine::core::Error& err, std::format_context& ctx) const {
        const eastl::string_view msg = err.Message();
        return std::formatter<std::string_view>::format(std::string_view(msg.data(), msg.size()), ctx);
    }
};