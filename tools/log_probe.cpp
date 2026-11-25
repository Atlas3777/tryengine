//! Headless check of the engine log: the file sink, the in-memory ring and its filter.
//!
//! Built and run by tools/agent/log-probe.sh. The editor window is only a view over the
//! same LogHistory, so what this probe verifies is what the Log panel shows: entries reach
//! the ring from the C++ Logger, the level/category masks select them, Clear empties the
//! ring, a full ring drops the oldest record, and the file sink has every line on disk while
//! the stream stays open.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "engine/core/Log.hpp"
#include "engine/core/LogFileSink.hpp"
#include "engine/core/LogHistory.hpp"

namespace {

int g_failures = 0;

void Check(const bool condition, const char* what) {
    if (condition) {
        std::printf("  ok   %s\n", what);
    } else {
        std::printf("  FAIL %s\n", what);
        ++g_failures;
    }
}

std::string ReadFile(const eastl::string& path) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open())
        return {};
    std::string text;
    file.seekg(0, std::ios::end);
    text.resize(static_cast<size_t>(file.tellg()));
    file.seekg(0, std::ios::beg);
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    return text;
}

uint32_t LevelsMask(const std::initializer_list<tryengine::core::LogLevel> levels) {
    uint32_t mask = 0;
    for (const auto level : levels)
        mask |= 1u << static_cast<uint32_t>(level);
    return mask;
}

}  // namespace

int main() {
    using namespace tryengine::core;

    const eastl::string& path = InstallLogFileSink();
    Check(!path.empty(), "file sink is installed");
    Check(std::filesystem::exists(path.c_str()), "log file exists");

    // The probe owns the ring state from here: earlier records (the sink's own "log file:"
    // line) must not confuse the counts.
    LogHistory::instance().clear();

    LogInfo("info probe");
    LogWarn(LogCategory::Physics, "warn probe");
    LogError(LogCategory::Script, "error probe");
    LogTrace("trace probe");

    Check(LogHistory::instance().count() == 4, "C++ records reach the ring");

    const std::string file_text = ReadFile(path);
    Check(file_text.find("info probe") != std::string::npos, "file has the INFO line");
    Check(file_text.find("[WARN] [Physics] warn probe") != std::string::npos, "file line carries level and category");
    Check(file_text.find("error probe") != std::string::npos, "file has the ERROR line");
    Check(file_text.find("trace probe") != std::string::npos, "file has the TRACE line");

    // Filter by importance: only errors.
    LogHistory::instance().set_filter(LevelsMask({LogLevel::Error, LogLevel::Critical}), ~0u);
    Check(LogHistory::instance().view_count() == 1, "level filter keeps one record");
    eastl::string message;
    Check(LogHistory::instance().view_message(0, message) && message == "error probe",
          "level-filtered record is the error");

    // Filter by reason: only physics.
    const uint32_t physics = 1u << static_cast<uint32_t>(LogCategory::Physics);
    LogHistory::instance().set_filter(~0u, physics);
    Check(LogHistory::instance().view_count() == 1, "category filter keeps one record");
    Check(LogHistory::instance().view_message(0, message) && message == "warn probe",
          "category-filtered record is the physics warning");

    // The view tells the UI when the newest visible record changes, not just when the count
    // grows: Follow has to work on a full ring too.
    const uint64_t newest = LogHistory::instance().view_newest_sequence();
    LogInfo(LogCategory::Physics, "second physics line");
    Check(LogHistory::instance().view_newest_sequence() != newest, "newest sequence advances");
    Check(LogHistory::instance().view_count() == 2, "both physics lines are visible");

    LogHistory::instance().clear();
    Check(LogHistory::instance().count() == 0, "Clear empties the ring");
    Check(LogHistory::instance().view_count() == 0, "Clear empties the filtered view");

    // Overflow: the sink path is used directly, so the probe's console and file stay short.
    const uint32_t total = LogHistory::CAPACITY + 50;
    HistoryLogSink sink;
    for (uint32_t i = 0; i < total; ++i) {
        char text[32] = {};
        std::snprintf(text, sizeof(text), "filler %u", i);
        const LogRecord record{LogLevel::Info, LogCategory::Gameplay, eastl::string_view(text),
                               eastl::string_view("log_probe.cpp"), 1};
        sink.write(record);
    }
    LogHistory::instance().set_filter(~0u, ~0u);
    Check(LogHistory::instance().count() == LogHistory::CAPACITY, "ring is capped at CAPACITY");
    Check(LogHistory::instance().view_count() == LogHistory::CAPACITY, "view covers a full ring");
    Check(LogHistory::instance().view_message(0, message) && message == "filler 50", "the oldest records were dropped");

    // The stream is still open after all that writing: a later line still lands.
    LogInfo("after overflow");
    const std::string tail = ReadFile(path);
    Check(tail.find("after overflow") != std::string::npos, "file stream stays open");

    if (g_failures == 0) {
        std::error_code error;
        std::filesystem::remove(path.c_str(), error);
        std::printf("LOG PROBE OK\n");
        return 0;
    }
    std::printf("LOG PROBE FAILED: %d check(s)\n", g_failures);
    return 1;
}
