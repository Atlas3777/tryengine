// engine/core/Log.hpp
#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <cstdint>
#include <format>
#include <iterator>
#include <mutex>

// ---------------------------------------------------------------------------
// std::format не умеет форматировать eastl::string(_view) из коробки. Без этой
// специализации оно попадает под дефолтное range-форматирование (т.к.
// eastl::string_view — это range<char>) и печатается как ['F','o','o',...].
// Специализации нужно видеть ДО первого использования format() с этими типами.
// ---------------------------------------------------------------------------
template <>
struct std::formatter<eastl::string_view> : std::formatter<std::string_view> {
    auto format(eastl::string_view sv, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(std::string_view(sv.data(), sv.size()), ctx);
    }
};

template <>
struct std::formatter<eastl::string> : std::formatter<std::string_view> {
    auto format(const eastl::string& s, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(std::string_view(s.data(), s.size()), ctx);
    }
};

namespace tryengine::core {

enum class LogLevel : uint8_t {
    Trace = 0,
    Info,
    Warning,
    Error,
    Critical,
};

// Расширяйте по мере роста движка — это просто список подсистем,
// компилятор не даст опечататься в отличие от строкового литерала.
enum class LogCategory : uint16_t {
    General = 0,
    Core,
    Graphics,
    Physics,
    Audio,
    Network,
    Gameplay,
    Assert,
    Importer,
};

eastl::string_view to_string(LogLevel level);
eastl::string_view to_string(LogCategory category);

// Одно готовое сообщение, которое получает каждый sink.
// message валиден только на время вызова write() — если sink'у нужно
// сохранить сообщение (например асинхронная запись в файл), он обязан
// скопировать его в eastl::string сам.
struct LogRecord {
    LogLevel level;
    LogCategory category;
    eastl::string_view message;
};

// Точка расширения. Реализуйте этот интерфейс для ImGui-консоли,
// файла, сети и т.д. — Logger просто разошлёт запись во все зарегистрированные sink'и.
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write(const LogRecord& record) = 0;
    virtual void flush() {}
};

// Центральный диспетчер. Владеет sink'ами, потокобезопасен.
class Logger {
public:
    static Logger& instance();

    // Logger забирает владение sink'ом. Возвращает non-owning указатель,
    // чтобы можно было потом настроить/удалить конкретный sink.
    ILogSink* add_sink(eastl::unique_ptr<ILogSink> sink);
    void remove_sink(ILogSink* sink);

    void set_min_level(LogLevel level) noexcept { m_min_level = level; }
    LogLevel min_level() const noexcept { return m_min_level; }

    void log(LogLevel level, LogCategory category, eastl::string_view message);

private:
    Logger() = default;

    eastl::vector<eastl::unique_ptr<ILogSink>> m_sinks;
    LogLevel m_min_level = LogLevel::Trace;
    std::mutex m_mutex;
};

// Дефолтный sink, регистрируется автоматически при первом обращении к Logger,
// чтобы логи никогда не терялись до настройки остальных sink'ов.
class ConsoleLogSink final : public ILogSink {
public:
    void write(const LogRecord& record) override;
};

namespace detail {

// eastl::format не существует. Пишем std::format прямо в eastl::string через
// back_inserter — без промежуточного std::string и лишней аллокации.
template <typename... Args>
eastl::string format(std::format_string<Args...> fmt, Args&&... args) {
    eastl::string result;
    std::format_to(std::back_inserter(result), fmt, std::forward<Args>(args)...);
    return result;
}

} // namespace detail
} // namespace tryengine::core

// ---------------------------------------------------------------------------
// Категория по умолчанию для файла.
// Определите ДО #include "engine/core/Log.hpp" в конкретном .cpp, чтобы все
// безкатегорийные вызовы TRY_LOG_* в этом файле шли в нужную категорию:
//
//   #define TRY_LOG_DEFAULT_CATEGORY ::tryengine::core::LogCategory::Graphics
//   #include "engine/core/Log.hpp"
// ---------------------------------------------------------------------------
#ifndef TRY_LOG_DEFAULT_CATEGORY
#define TRY_LOG_DEFAULT_CATEGORY ::tryengine::core::LogCategory::General
#endif

// ---------------------------------------------------------------------------
// Основные макросы логирования
// ---------------------------------------------------------------------------

#define TRY_LOG_IMPL(level, category, fmt, ...) \
    ::tryengine::core::Logger::instance().log( \
        (level), (category), ::tryengine::core::detail::format(fmt __VA_OPT__(,) __VA_ARGS__))

// Без категории — используют TRY_LOG_DEFAULT_CATEGORY.
#define TRY_LOG_TRACE(fmt, ...) TRY_LOG_IMPL(::tryengine::core::LogLevel::Trace,    TRY_LOG_DEFAULT_CATEGORY, fmt __VA_OPT__(,) __VA_ARGS__)
#define TRY_LOG_INFO(fmt, ...)  TRY_LOG_IMPL(::tryengine::core::LogLevel::Info,     TRY_LOG_DEFAULT_CATEGORY, fmt __VA_OPT__(,) __VA_ARGS__)
#define TRY_LOG_WARN(fmt, ...)  TRY_LOG_IMPL(::tryengine::core::LogLevel::Warning,  TRY_LOG_DEFAULT_CATEGORY, fmt __VA_OPT__(,) __VA_ARGS__)
#define TRY_LOG_ERROR(fmt, ...) TRY_LOG_IMPL(::tryengine::core::LogLevel::Error,    TRY_LOG_DEFAULT_CATEGORY, fmt __VA_OPT__(,) __VA_ARGS__)
#define TRY_LOG_CRIT(fmt, ...)  TRY_LOG_IMPL(::tryengine::core::LogLevel::Critical, TRY_LOG_DEFAULT_CATEGORY, fmt __VA_OPT__(,) __VA_ARGS__)

// С явной категорией — когда в одном файле логика разных подсистем.
#define TRY_LOG_TRACE_CAT(category, fmt, ...) TRY_LOG_IMPL(::tryengine::core::LogLevel::Trace,    category, fmt __VA_OPT__(,) __VA_ARGS__)
#define TRY_LOG_INFO_CAT(category, fmt, ...)  TRY_LOG_IMPL(::tryengine::core::LogLevel::Info,     category, fmt __VA_OPT__(,) __VA_ARGS__)
#define TRY_LOG_WARN_CAT(category, fmt, ...)  TRY_LOG_IMPL(::tryengine::core::LogLevel::Warning,  category, fmt __VA_OPT__(,) __VA_ARGS__)
#define TRY_LOG_ERROR_CAT(category, fmt, ...) TRY_LOG_IMPL(::tryengine::core::LogLevel::Error,    category, fmt __VA_OPT__(,) __VA_ARGS__)
#define TRY_LOG_CRIT_CAT(category, fmt, ...)  TRY_LOG_IMPL(::tryengine::core::LogLevel::Critical, category, fmt __VA_OPT__(,) __VA_ARGS__)

// ---------------------------------------------------------------------------
// TRY_VARS — псевдо-f-строки. C++ не умеет резолвить идентификаторы внутри
// строкового литерала (даже с рефлексией C++26), поэтому это не настоящая
// интерполяция, а макрос, который берёт текст выражения через #x и сам
// генерирует "x = {}, y = {}", x, y — не нужно писать имя дважды.
// Работает и с произвольными выражениями: TRY_VARS(width * 2).
// Максимум 8 аргументов за один вызов.
// ---------------------------------------------------------------------------

#define TRY_VARS_FMT_1(x)          #x " = {}"
#define TRY_VARS_FMT_2(x, ...)     #x " = {}, " TRY_VARS_FMT_1(__VA_ARGS__)
#define TRY_VARS_FMT_3(x, ...)     #x " = {}, " TRY_VARS_FMT_2(__VA_ARGS__)
#define TRY_VARS_FMT_4(x, ...)     #x " = {}, " TRY_VARS_FMT_3(__VA_ARGS__)
#define TRY_VARS_FMT_5(x, ...)     #x " = {}, " TRY_VARS_FMT_4(__VA_ARGS__)
#define TRY_VARS_FMT_6(x, ...)     #x " = {}, " TRY_VARS_FMT_5(__VA_ARGS__)
#define TRY_VARS_FMT_7(x, ...)     #x " = {}, " TRY_VARS_FMT_6(__VA_ARGS__)
#define TRY_VARS_FMT_8(x, ...)     #x " = {}, " TRY_VARS_FMT_7(__VA_ARGS__)

#define TRY_VARS_NARG_(_1,_2,_3,_4,_5,_6,_7,_8,N,...) N
#define TRY_VARS_NARG(...) TRY_VARS_NARG_(__VA_ARGS__, 8,7,6,5,4,3,2,1)

#define TRY_VARS_CAT_(a, b) a##b
#define TRY_VARS_CAT(a, b) TRY_VARS_CAT_(a, b)

#define TRY_VARS(...) \
    TRY_VARS_CAT(TRY_VARS_FMT_, TRY_VARS_NARG(__VA_ARGS__))(__VA_ARGS__), __VA_ARGS__

// ---------------------------------------------------------------------------
// Логирование переменных без явного форматирования (авто-f-строка)
// ---------------------------------------------------------------------------

#define TRY_LOG_VARS_IMPL(level, category, ...) \
TRY_LOG_IMPL((level), (category), TRY_VARS(__VA_ARGS__))

// Без категории
#define TRY_LOG_VARS_TRACE(...) TRY_LOG_VARS_IMPL(::tryengine::core::LogLevel::Trace,    TRY_LOG_DEFAULT_CATEGORY, __VA_ARGS__)
#define TRY_LOG_VARS_INFO(...)  TRY_LOG_VARS_IMPL(::tryengine::core::LogLevel::Info,     TRY_LOG_DEFAULT_CATEGORY, __VA_ARGS__)
#define TRY_LOG_VARS_WARN(...)  TRY_LOG_VARS_IMPL(::tryengine::core::LogLevel::Warning,  TRY_LOG_DEFAULT_CATEGORY, __VA_ARGS__)
#define TRY_LOG_VARS_ERROR(...) TRY_LOG_VARS_IMPL(::tryengine::core::LogLevel::Error,    TRY_LOG_DEFAULT_CATEGORY, __VA_ARGS__)
#define TRY_LOG_VARS_CRIT(...)  TRY_LOG_VARS_IMPL(::tryengine::core::LogLevel::Critical, TRY_LOG_DEFAULT_CATEGORY, __VA_ARGS__)

// С явной категорией
#define TRY_LOG_VARS_TRACE_CAT(cat, ...) TRY_LOG_VARS_IMPL(::tryengine::core::LogLevel::Trace,    cat, __VA_ARGS__)
#define TRY_LOG_VARS_INFO_CAT(cat, ...)  TRY_LOG_VARS_IMPL(::tryengine::core::LogLevel::Info,     cat, __VA_ARGS__)
#define TRY_LOG_VARS_WARN_CAT(cat, ...)  TRY_LOG_VARS_IMPL(::tryengine::core::LogLevel::Warning,  cat, __VA_ARGS__)
#define TRY_LOG_VARS_ERROR_CAT(cat, ...) TRY_LOG_VARS_IMPL(::tryengine::core::LogLevel::Error,    cat, __VA_ARGS__)
#define TRY_LOG_VARS_CRIT_CAT(cat, ...)  TRY_LOG_VARS_IMPL(::tryengine::core::LogLevel::Critical, cat, __VA_ARGS__)