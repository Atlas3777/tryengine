// engine/core/Log.cpp
#include "engine/core/Log.hpp"

#include <EASTL/algorithm.h>
#include <iostream>

#include "engine/core/Assert.hpp"
#include "engine/core/LogHistory.hpp"

namespace tryengine::core {

eastl::string_view to_string(LogLevel level) {
    switch (level) {
        default:
            TRY_ASSERT(false, "Unhandled LogLevel");
            break;
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Critical:
            return "CRIT";
    }
}

eastl::string_view to_string(LogCategory category) {
    switch (category) {
        default:
            TRY_ASSERT(false, "Unhandled Log Category");
        case LogCategory::General:
            return "General";
        case LogCategory::Core:
            return "Core";
        case LogCategory::Graphics:
            return "Graphics";
        case LogCategory::Physics:
            return "Physics";
        case LogCategory::Audio:
            return "Audio";
        case LogCategory::Network:
            return "Network";
        case LogCategory::Gameplay:
            return "Gameplay";
        case LogCategory::Assert:
            return "Assert";
        case LogCategory::Importer:
            return "Importer";
        case LogCategory::Script:
            return "Script";
        case LogCategory::FileSystem:
            return "FileSystem";
    }
}

Logger& Logger::instance() {
    static Logger logger;
    // Консоль и история в памяти регистрируются лениво, один раз, при первом обращении —
    // так записи видны с самого старта, даже до того как приложение настроит файловый sink
    // и откроет окно журнала. Историю читает окно редактора; она же делает один журнал
    // общим для C++ и daslang.
    static const bool bootstrapped = [&logger] {
        logger.add_sink(eastl::unique_ptr<ILogSink>(new ConsoleLogSink()));
        logger.add_sink(eastl::unique_ptr<ILogSink>(new HistoryLogSink()));
        return true;
    }();
    (void) bootstrapped;
    return logger;
}

ILogSink* Logger::add_sink(eastl::unique_ptr<ILogSink> sink) {
    std::lock_guard lock(m_mutex);
    m_sinks.push_back(eastl::move(sink));
    return m_sinks.back().get();
}

void Logger::remove_sink(ILogSink* sink) {
    std::lock_guard lock(m_mutex);
    m_sinks.erase(eastl::remove_if(m_sinks.begin(), m_sinks.end(),
                                   [sink](const eastl::unique_ptr<ILogSink>& s) { return s.get() == sink; }),
                  m_sinks.end());
}

void Logger::log(LogLevel level, LogCategory category, const std::source_location& location,
                 eastl::string_view message) {
    log(level, category, eastl::string_view(location.file_name()), location.line(), message);
}

void Logger::log(LogLevel level, LogCategory category, eastl::string_view file, uint32_t line,
                 eastl::string_view message) {
    if (level < m_min_level)
        return;

    const LogRecord record{level, category, message, file, line};
    dispatch(record);
}

void Logger::dispatch(const LogRecord& record) {
    std::lock_guard lock(m_mutex);
    for (auto& sink : m_sinks)
        sink->write(record);
}

void ConsoleLogSink::write(const LogRecord& record) {
    std::ostream& stream = (record.level >= LogLevel::Error) ? std::cerr : std::cout;

    stream << "[" << to_string(record.level).data() << "] "
           << "[" << to_string(record.category).data() << "] ";
    stream.write(record.message.data(), static_cast<std::streamsize>(record.message.size()));

    if (record.level >= LogLevel::Error) {
        stream << "\n  at ";
        if (!record.file.empty())
            stream.write(record.file.data(), static_cast<std::streamsize>(record.file.size()));
        else
            stream << "<unknown>";
        stream << ":" << record.line;
    }

    stream << "\n";
}

}  // namespace tryengine::core