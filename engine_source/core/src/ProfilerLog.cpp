#include "engine/core/ProfilerLog.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string_view>
#include <system_error>

#include "engine/core/Log.hpp"
#include "engine/core/Profiler.hpp"

namespace tryengine::core {
namespace {

constexpr char kMagic[8] = {'T', 'R', 'Y', 'P', 'F', 'L', '0', '1'};
constexpr uint8_t kTagScope = 1;
constexpr uint8_t kTagFrame = 2;
constexpr uint8_t kTagRepeat = 3;
/// Big enough that a flush is one write syscall every ~16 frames at 1000 fps, small enough
/// that a crash loses almost nothing.
constexpr size_t kBufferBytes = 1 << 18;

/// Milliseconds -> the microsecond field the format stores.
uint64_t ToMicroseconds(const float ms) {
    if (!(ms > 0.0f))
        return 0;
    return static_cast<uint64_t>(static_cast<double>(ms) * 1000.0 + 0.5);
}

}  // namespace

eastl::string ProfilerTimestampedBase(const eastl::string_view prefix) {
    std::error_code error;
    std::filesystem::create_directories("logs", error);

    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);

    char buffer[128] = {};
    std::snprintf(buffer, sizeof(buffer), "logs/%.*s-%04d%02d%02d-%02d%02d%02d", static_cast<int>(prefix.size()),
                  prefix.data(), local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
                  local.tm_sec);
    return eastl::string(buffer);
}

eastl::string ProfilerLatestLog(const eastl::string_view extension) {
    eastl::string suffix = eastl::string(".");
    suffix.append(extension.data(), extension.size());

    eastl::string best;
    std::filesystem::file_time_type best_time{};
    std::error_code error;
    for (std::filesystem::directory_iterator it{"logs", error}, end; !error && it != end; it.increment(error)) {
        error.clear();
        if (!it->is_regular_file(error))
            continue;
        const std::string_view name = it->path().filename().c_str();
        if (name.size() <= suffix.size() || name.substr(name.size() - suffix.size()) != std::string_view(suffix))
            continue;
        const auto written = it->last_write_time(error);
        if (error) {
            error.clear();
            continue;
        }
        if (best.empty() || written > best_time) {
            best_time = written;
            best = eastl::string(it->path().c_str());
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// ProfilerLog
// ---------------------------------------------------------------------------

bool ProfilerLog::Start(const eastl::string_view path, const uint64_t max_bytes) {
    Stop();

    path_ = eastl::string(path);
    bytes_ = 0;
    frames_ = 0;
    max_bytes_ = max_bytes;
    capped_ = false;
    have_structure_ = false;
    structure_.clear();
    scratch_.clear();

    file_ = std::fopen(path_.c_str(), "wb");
    if (file_ == nullptr) {
        LogError("Profiler: не удалось открыть {} для записи", path_.c_str());
        return false;
    }
    buffer_.resize(kBufferBytes);
    std::setvbuf(file_, buffer_.data(), _IOFBF, buffer_.size());
    WriteRaw(kMagic, sizeof(kMagic));
    return true;
}

void ProfilerLog::Stop() {
    if (file_ == nullptr)
        return;
    std::fflush(file_);
    std::fclose(file_);
    file_ = nullptr;
    structure_.clear();
    scratch_.clear();
    have_structure_ = false;
}

void ProfilerLog::Flush() {
    if (file_ != nullptr)
        std::fflush(file_);
}

void ProfilerLog::WriteByte(const uint8_t value) {
    std::fputc(value, file_);
    ++bytes_;
}

void ProfilerLog::WriteVarint(uint64_t value) {
    while (value >= 0x80) {
        WriteByte(static_cast<uint8_t>(value) | 0x80u);
        value >>= 7;
    }
    WriteByte(static_cast<uint8_t>(value));
}

void ProfilerLog::WriteRaw(const char* data, const size_t size) {
    if (size == 0)
        return;
    std::fwrite(data, 1, size, file_);
    bytes_ += size;
}

void ProfilerLog::CheckCap() {
    if (max_bytes_ != 0 && bytes_ >= max_bytes_ && file_ != nullptr) {
        // The cut is taken between two frames, so the file the reader sees stays complete.
        capped_ = true;
        Stop();
        LogWarn("Profiler: кадр-лог достиг {} байт ({} кадров), запись остановлена", max_bytes_, frames_);
    }
}

void ProfilerLog::WriteScope(const uint32_t id, const uint8_t kind, const uint8_t depth,
                             const eastl::string_view name) {
    if (file_ == nullptr)
        return;
    WriteByte(kTagScope);
    WriteVarint(id);
    WriteByte(kind);
    WriteByte(depth);
    WriteVarint(name.size());
    WriteRaw(name.data(), name.size());
}

void ProfilerLog::WriteFrame(const float dt_ms, const ProfileEvent* events, const size_t count) {
    if (file_ == nullptr)
        return;

    scratch_.clear();
    scratch_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const ProfileEvent& event = events[i];
        const bool has_parent = event.parent >= 0 && static_cast<size_t>(event.parent) < i;
        scratch_.push_back(StructureKey{event.scope, has_parent ? static_cast<uint32_t>(i - event.parent) : 0u,
                                        event.depth, event.kind});
    }

    const uint64_t dt_us = ToMicroseconds(dt_ms);
    if (have_structure_ && structure_ == scratch_) {
        // Same shape as the previous frame: only the timings are new.
        WriteByte(kTagRepeat);
        WriteVarint(dt_us);
        for (size_t i = 0; i < count; ++i) {
            WriteVarint(ToMicroseconds(events[i].start_ms));
            WriteVarint(ToMicroseconds(events[i].duration_ms));
        }
    } else {
        WriteByte(kTagFrame);
        WriteVarint(dt_us);
        WriteVarint(count);
        for (size_t i = 0; i < scratch_.size(); ++i) {
            const StructureKey& key = scratch_[i];
            WriteVarint(key.scope);
            WriteVarint(ToMicroseconds(events[i].start_ms));
            WriteVarint(ToMicroseconds(events[i].duration_ms));
            WriteVarint(key.parent_code);
            WriteByte(static_cast<uint8_t>(key.depth | (key.kind << 5)));
        }
        structure_ = scratch_;
        have_structure_ = true;
    }

    ++frames_;
    if ((frames_ & 63u) == 0)
        Flush();
    if (std::ferror(file_) != 0) {
        LogError("Profiler: ошибка записи кадр-лога {}", path_.c_str());
        Stop();
        return;
    }
    CheckCap();
}

// ---------------------------------------------------------------------------
// ProfilerStatusLog
// ---------------------------------------------------------------------------

bool ProfilerStatusLog::Start(const eastl::string_view path) {
    Stop();
    path_ = eastl::string(path);
    file_ = std::fopen(path_.c_str(), "wb");
    if (file_ == nullptr) {
        LogError("Profiler: не удалось открыть {} для записи", path_.c_str());
        return false;
    }
    Write("# tryengine profiler status: one line per second, times in milliseconds");
    LogInfo("Profiler: статус пишется в {}", path_.c_str());
    return true;
}

void ProfilerStatusLog::Write(const eastl::string_view line) {
    if (file_ == nullptr)
        return;
    std::fwrite(line.data(), 1, line.size(), file_);
    std::fputc('\n', file_);
    // A line per second: flushing is free here and makes the file tail-able right away.
    std::fflush(file_);
}

void ProfilerStatusLog::Stop() {
    if (file_ == nullptr)
        return;
    std::fclose(file_);
    file_ = nullptr;
}

}  // namespace tryengine::core
