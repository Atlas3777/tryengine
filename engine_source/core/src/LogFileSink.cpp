#include "engine/core/LogFileSink.hpp"

#include <EASTL/memory.h>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <system_error>

namespace tryengine::core {
namespace {

eastl::string& ActivePathStorage() {
    static eastl::string path;
    return path;
}

void FormatTime(char* out, const size_t size) {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);

    std::tm local{};
    localtime_r(&seconds, &local);
    std::snprintf(out, size, "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min, local.tm_sec,
                  static_cast<int>(millis));
}

//! logs/engine-YYYYMMDD-HHMMSS.log — тот же штамп, что у файлов профайлера, чтобы захват
//! кадров и журнал одного запуска лежали рядом и находились по времени.
eastl::string MakeLogFilePath() {
    std::error_code error;
    std::filesystem::create_directories("logs", error);

    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);

    char buffer[128] = {};
    std::snprintf(buffer, sizeof(buffer), "logs/engine-%04d%02d%02d-%02d%02d%02d.log", local.tm_year + 1900,
                  local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    return eastl::string(buffer);
}

}  // namespace

FileLogSink::FileLogSink(eastl::string path) : path_(eastl::move(path)) {
    file_ = std::fopen(path_.c_str(), "wb");
}

FileLogSink::~FileLogSink() {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void FileLogSink::write(const LogRecord& record) {
    if (file_ == nullptr)
        return;

    char time[16] = {};
    FormatTime(time, sizeof(time));

    const eastl::string_view level = to_string(record.level);
    const eastl::string_view category = to_string(record.category);
    std::fprintf(file_, "[%s] [%.*s] [%.*s] ", time, static_cast<int>(level.size()), level.data(),
                 static_cast<int>(category.size()), category.data());
    if (!record.message.empty())
        std::fwrite(record.message.data(), 1, record.message.size(), file_);

    // Файл и строку пишем только там, где их ищут: иначе каждая INFO-строка несёт шум.
    if (record.level >= LogLevel::Warning && !record.file.empty())
        std::fprintf(file_, "  (%.*s:%u)", static_cast<int>(record.file.size()), record.file.data(), record.line);

    std::fputc('\n', file_);
    // Строка сразу в файле: `tail -f` — штатный способ смотреть журнал приложения без UI.
    std::fflush(file_);
}

void FileLogSink::flush() {
    if (file_ != nullptr)
        std::fflush(file_);
}

const eastl::string& InstallLogFileSink() {
    eastl::string& active = ActivePathStorage();
    if (!active.empty())
        return active;

    auto* sink = new FileLogSink(MakeLogFilePath());
    if (!sink->is_open()) {
        LogError(LogCategory::Core, "logs/ is not writable: file logging is off");
        delete sink;
        return active;
    }

    active = sink->path();
    Logger::instance().add_sink(eastl::unique_ptr<ILogSink>(sink));
    LogInfo(LogCategory::Core, "log file: {}", active);
    return active;
}

const eastl::string& ActiveLogFilePath() {
    return ActivePathStorage();
}

}  // namespace tryengine::core
