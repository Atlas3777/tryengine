// engine/core/Log.cpp
#include "engine/core/Log.hpp"

#include <iostream>
#include <EASTL/algorithm.h>

namespace tryengine::core {

eastl::string_view to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:    return "TRACE";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warning:  return "WARN";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRIT";
    }
    return "?????";
}

eastl::string_view to_string(LogCategory category) {
    switch (category) {
        case LogCategory::General:  return "General";
        case LogCategory::Core:     return "Core";
        case LogCategory::Graphics: return "Graphics";
        case LogCategory::Physics:  return "Physics";
        case LogCategory::Audio:    return "Audio";
        case LogCategory::Network:  return "Network";
        case LogCategory::Gameplay: return "Gameplay";
        case LogCategory::Assert:   return "Assert";
        case LogCategory::Importer:   return "Importer";
        case LogCategory::Script:   return "Script";
    }
    return "Unknown";
}

Logger& Logger::instance() {
    static Logger logger;
    // Регистрируем консольный sink лениво, один раз, при первом обращении —
    // так логи есть с самого старта, даже до того как движок настроит
    // файловый/ImGui sink в своей точке входа.
    static const bool bootstrapped = [&logger] {
        logger.add_sink(eastl::unique_ptr<ILogSink>(new ConsoleLogSink()));
        return true;
    }();
    (void)bootstrapped;
    return logger;
}

ILogSink* Logger::add_sink(eastl::unique_ptr<ILogSink> sink) {
    std::lock_guard lock(m_mutex);
    m_sinks.push_back(eastl::move(sink));
    return m_sinks.back().get();
}

void Logger::remove_sink(ILogSink* sink) {
    std::lock_guard lock(m_mutex);
    m_sinks.erase(
        eastl::remove_if(m_sinks.begin(), m_sinks.end(),
            [sink](const eastl::unique_ptr<ILogSink>& s) { return s.get() == sink; }),
        m_sinks.end());
}

void Logger::log(LogLevel level, LogCategory category, const std::source_location& location,
                  eastl::string_view message) {
    if (level < m_min_level) {
        return;
    }

    const LogRecord record{level, category, message, location};

    std::lock_guard lock(m_mutex);
    for (auto& sink : m_sinks) {
        sink->write(record);
    }
}

void ConsoleLogSink::write(const LogRecord& record) {
    std::ostream& stream = (record.level >= LogLevel::Error) ? std::cerr : std::cout;

    stream << "[" << to_string(record.level).data() << "] "
           << "[" << to_string(record.category).data() << "] ";
    stream.write(record.message.data(), static_cast<std::streamsize>(record.message.size()));

    if (record.level >= LogLevel::Error) {
        stream << "\n  at " << record.location.file_name() << ":" << record.location.line();
    }

    stream << "\n";
}

} // namespace tryengine::core