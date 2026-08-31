#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <cstdint>
#include <format>
#include <iterator>
#include <mutex>
#include <source_location>
#include <type_traits>

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
enum class LogCategory : uint8_t {
    General = 0,
    Core,
    Graphics,
    Physics,
    Audio,
    Network,
    Gameplay,
    Assert,
    Importer,
    Script,
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
    std::source_location location;
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

    void log(LogLevel level, LogCategory category, const std::source_location& location,
              eastl::string_view message);

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

// ---------------------------------------------------------------------------
// Дефолтная категория для файла. Определите ДО #include "engine/core/Log.hpp"
// в конкретном .cpp, если у файла есть основная подсистема:
//
//   #define TRY_LOG_DEFAULT_CATEGORY Graphics
//   #include "engine/core/Log.hpp"
// ---------------------------------------------------------------------------
#ifndef TRY_LOG_DEFAULT_CATEGORY
#define TRY_LOG_DEFAULT_CATEGORY General
#endif
#define TRY_LOG_DEFAULT_CATEGORY_Q ::tryengine::core::LogCategory::TRY_LOG_DEFAULT_CATEGORY

namespace detail {

// eastl::format не существует. Пишем std::format прямо в eastl::string через
// back_inserter — без промежуточного std::string и лишней аллокации.
template <typename... Args>
eastl::string format(std::format_string<Args...> fmt, Args&&... args) {
    eastl::string result;
    std::format_to(std::back_inserter(result), fmt, std::forward<Args>(args)...);
    return result;
}

inline void log_dispatch(LogLevel level, LogCategory category,
                          const std::source_location& location, eastl::string_view message) {
    Logger::instance().log(level, category, location, message);
}

} // namespace detail

// ---------------------------------------------------------------------------
// LocFmt — пара (format_string, source_location). Ключевая идея: location
// ловится в consteval-конструкторе как ДЕФОЛТНЫЙ АРГУМЕНТ, который по
// стандарту C++20 вычисляется в точке вызова, а не в точке объявления.
// Благодаря этому LogInfo/LogWarn/... — обычные функции, а не макросы:
// им не нужен __FILE__/__LINE__ через препроцессор.
//
// type_identity_t в LogXxx ниже нужен, чтобы Args выводился ТОЛЬКО из
// хвостового пака аргументов, а не пытался вывестись (неоднозначно) ещё
// и из самого locFmt.
// ---------------------------------------------------------------------------
template <typename... Args>
struct LocFmt {
    std::format_string<Args...> fmt;
    std::source_location loc;

    template <typename S>
        requires std::is_constructible_v<std::format_string<Args...>, const S&>
    consteval LocFmt(const S& str, std::source_location loc = std::source_location::current())
        : fmt(str), loc(loc) {}
};

// ---------------------------------------------------------------------------
// LogTrace / LogInfo / LogWarn / LogError / LogCritical.
//
// Обычные функции — работает автодополнение категорий, F12 (go to definition),
// подсказки параметров. Каждая функция — 4 перегрузки:
//
//   LogInfo("hello");                       // строка как есть, дефолтная категория
//   LogInfo(LogCategory::Physics, "hello"); // строка как есть, явная категория
//   LogInfo("x = {}", x);                   // форматирование, дефолтная категория
//   LogInfo(LogCategory::Physics, "x = {}", x); // форматирование, явная категория
//
// Категория, если указана, всегда идёт ПЕРВЫМ аргументом (а не через
// default-параметр) — иначе после параметра со значением по умолчанию
// нельзя поставить обязательный (message), см. правила default-аргументов.
//
// Перегрузка с чистым eastl::string_view (без форматирования) существует
// не просто для краткости: LocFmt требует compile-time литерал (constexpr
// строка), а eastl::string_view принимает и runtime-строку (переменную,
// склеенный буфер) — форматирующая перегрузка такое в принципе не может
// проверить на этапе компиляции.
// ---------------------------------------------------------------------------

#define TRY_DEFINE_LOG_LEVEL(FuncName, LevelEnumerator)                                                     \
    inline void FuncName(eastl::string_view message,                                                        \
                          std::source_location loc = std::source_location::current()) {                     \
        detail::log_dispatch(LogLevel::LevelEnumerator, TRY_LOG_DEFAULT_CATEGORY_Q, loc, message);           \
    }                                                                                                        \
    inline void FuncName(LogCategory category, eastl::string_view message,                                  \
                          std::source_location loc = std::source_location::current()) {                     \
        detail::log_dispatch(LogLevel::LevelEnumerator, category, loc, message);                             \
    }                                                                                                        \
    template <typename... Args>                                                                             \
        requires (sizeof...(Args) > 0)                                                                      \
    void FuncName(LocFmt<std::type_identity_t<Args>...> locFmt, Args&&... args) {                           \
        detail::log_dispatch(LogLevel::LevelEnumerator, TRY_LOG_DEFAULT_CATEGORY_Q, locFmt.loc,              \
                              detail::format(locFmt.fmt, std::forward<Args>(args)...));                      \
    }                                                                                                        \
    template <typename... Args>                                                                             \
        requires (sizeof...(Args) > 0)                                                                      \
    void FuncName(LogCategory category, LocFmt<std::type_identity_t<Args>...> locFmt, Args&&... args) {     \
        detail::log_dispatch(LogLevel::LevelEnumerator, category, locFmt.loc,                                \
                              detail::format(locFmt.fmt, std::forward<Args>(args)...));                      \
    }

TRY_DEFINE_LOG_LEVEL(LogTrace, Trace)
TRY_DEFINE_LOG_LEVEL(LogInfo, Info)
TRY_DEFINE_LOG_LEVEL(LogWarn, Warning)
TRY_DEFINE_LOG_LEVEL(LogError, Error)
TRY_DEFINE_LOG_LEVEL(LogCritical, Critical)

#undef TRY_DEFINE_LOG_LEVEL

} // namespace tryengine::core

// ---------------------------------------------------------------------------
// TRY_VARS — псевдо-f-строки. Единственное, что здесь ОБЯЗАНО быть макросом:
// C++ не умеет резолвить идентификаторы внутри строкового литерала (даже с
// рефлексией C++26), поэтому это макрос, который берёт текст выражения через
// #x и сам генерирует "x = {}, y = {}", x, y. Работает и с произвольными
// выражениями: TRY_VARS(width * 2). Максимум 8 аргументов за один вызов.
//
// Дальше — обычный вызов функции:
//   LogInfo(TRY_VARS(width, height));
//   LogInfo(LogCategory::Physics, TRY_VARS(width, height));
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
// Короткие имена без tryengine::core:: — именно using-declaration (не
// using-directive), поэтому в глобальную область попадают ТОЛЬКО эти 5
// функций (со всеми их перегрузками), а не весь namespace целиком.
// Logger/LogLevel/LogCategory/ErrorCode и т.д. по-прежнему нужно
// квалифицировать — но ими пользуются гораздо реже, чем самим логированием.
// ---------------------------------------------------------------------------
using tryengine::core::LogTrace;
using tryengine::core::LogInfo;
using tryengine::core::LogWarn;
using tryengine::core::LogError;
using tryengine::core::LogCritical;
using tryengine::core::LogCategory;