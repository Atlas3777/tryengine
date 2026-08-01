// engine/core/Assert.hpp
#pragma once

#include "engine/core/Log.hpp"
#include <source_location>
#include <cstdlib>
#include <EASTL/string.h>

// ---------------------------------------------------------------------------
// Кроссплатформенный триггер точки останова отладчика
// ---------------------------------------------------------------------------
#if defined(__clang__) || defined(__GNUC__)
    #define TRY_DEBUG_BREAK() __builtin_trap()
#elif defined(_MSC_VER)
    #define TRY_DEBUG_BREAK() __debugbreak()
#else
    #define TRY_DEBUG_BREAK() std::abort()
#endif

namespace tryengine::core::detail {

inline void report_failure(
    const char* kind,              // "Assertion" / "Verify" / "Check"
    const char* condition_text,
    LogLevel level,
    const eastl::string& message,
    const std::source_location& location)
{
    Logger::instance().log(level, LogCategory::Assert,
        detail::format("{} failed: ({})  {}\n    at {}:{} in {}",
            kind, condition_text, message,
            location.file_name(), location.line(), location.function_name()));
}

// Сообщение форматируется лениво (только при провале условия) —
// MessageFn это лямбда без аргументов, возвращающая eastl::string.
template <typename MessageFn>
bool verify_impl(
    bool condition,
    const char* condition_text,
    MessageFn&& make_message,
    const std::source_location& location)
{
    if (!condition) [[unlikely]] {
        report_failure("Verify", condition_text, LogLevel::Error, make_message(), location);
#if !defined(NDEBUG)
        TRY_DEBUG_BREAK();
#endif
    }
    return condition;
}

} // namespace tryengine::core::detail

// ---------------------------------------------------------------------------
// TRY_ASSERT — только debug. Программистская ошибка / инвариант.
// В release условие даже не вычисляется (как и раньше).
// ---------------------------------------------------------------------------
#if !defined(NDEBUG)
    #define TRY_ASSERT(condition, msg, ...) \
        do { \
            if (!(condition)) [[unlikely]] { \
                ::tryengine::core::detail::report_failure( \
                    "Assertion", #condition, ::tryengine::core::LogLevel::Critical, \
                    ::tryengine::core::detail::format(msg __VA_OPT__(,) __VA_ARGS__), \
                    std::source_location::current()); \
                TRY_DEBUG_BREAK(); \
            } \
        } while (0)
#else
    #define TRY_ASSERT(condition, msg, ...) do { (void)sizeof(condition); } while (0)
#endif

// ---------------------------------------------------------------------------
// TRY_VERIFY — условие проверяется ВСЕГДА (debug и release).
// При провале: логирует Error, в debug — точка останова, но НЕ прерывает
// выполнение. Возвращает bool — используйте для восстановимых сбоев внешних
// API (SDL, файловая система и т.п.):
//
//   if (!TRY_VERIFY(sdl_inited, "SDL_Init failed: {}", SDL_GetError())) {
//       return false;
//   }
// ---------------------------------------------------------------------------
#define TRY_VERIFY(condition, msg, ...) \
    ::tryengine::core::detail::verify_impl( \
        static_cast<bool>(condition), #condition, \
        [&] { return ::tryengine::core::detail::format(msg __VA_OPT__(,) __VA_ARGS__); }, \
        std::source_location::current())

// ---------------------------------------------------------------------------
// TRY_CHECK — условие проверяется ВСЕГДА и при провале программа ВСЕГДА
// завершается (даже в release). Для невосстановимых ошибок инициализации,
// без которых движок не может продолжать работу (например GPU device).
// ---------------------------------------------------------------------------
#define TRY_CHECK(condition, msg, ...) \
    do { \
        if (!(condition)) [[unlikely]] { \
            ::tryengine::core::detail::report_failure( \
                "Check", #condition, ::tryengine::core::LogLevel::Critical, \
                ::tryengine::core::detail::format(msg __VA_OPT__(,) __VA_ARGS__), \
                std::source_location::current()); \
            TRY_DEBUG_BREAK(); \
            std::abort(); \
        } \
    } while (0)
