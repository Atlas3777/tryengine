#include "engine/core/LogHistory.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace tryengine::core {
namespace {

//! Локальное время записи. localtime_r, а не localtime: журнал может писать и рабочий поток.
eastl::string FormatTimeNow() {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);

    std::tm local{};
    localtime_r(&seconds, &local);

    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min, local.tm_sec,
                  static_cast<int>(millis));
    return eastl::string(buffer);
}

uint32_t LevelBit(const LogLevel level) {
    return 1u << static_cast<uint32_t>(level);
}

uint32_t CategoryBit(const LogCategory category) {
    return 1u << static_cast<uint32_t>(category);
}

}  // namespace

LogHistory::LogHistory() {
    entries_.resize(CAPACITY);
}

LogHistory& LogHistory::instance() {
    static LogHistory history;
    return history;
}

void LogHistory::append(const LogRecord& record) {
    LogEntry entry;
    entry.level = record.level;
    entry.category = record.category;
    entry.line = record.line;
    entry.time = FormatTimeNow();
    if (!record.message.empty())
        entry.message.assign(record.message.data(), record.message.size());
    if (!record.file.empty())
        entry.file.assign(record.file.data(), record.file.size());

    std::lock_guard lock(mutex_);
    entry.sequence = next_sequence_++;

    const uint32_t slot = (head_ + count_) % CAPACITY;
    entries_[slot] = eastl::move(entry);
    if (count_ < CAPACITY)
        ++count_;
    else
        head_ = (head_ + 1) % CAPACITY;
    ++revision_;
}

void LogHistory::clear() {
    std::lock_guard lock(mutex_);
    for (auto& entry : entries_)
        entry = LogEntry{};
    head_ = 0;
    count_ = 0;
    ++revision_;
}

uint32_t LogHistory::count() {
    std::lock_guard lock(mutex_);
    return count_;
}

void LogHistory::set_filter(const uint32_t level_mask, const uint32_t category_mask) {
    std::lock_guard lock(mutex_);
    if (filter_levels_ == level_mask && filter_categories_ == category_mask)
        return;
    filter_levels_ = level_mask;
    filter_categories_ = category_mask;
}

uint32_t LogHistory::view_count() {
    std::lock_guard lock(mutex_);
    ensure_view_locked();
    return static_cast<uint32_t>(refs_.size());
}

uint64_t LogHistory::view_newest_sequence() {
    std::lock_guard lock(mutex_);
    ensure_view_locked();
    if (refs_.empty())
        return 0;
    return entries_[refs_.back().slot].sequence;
}

void LogHistory::rebuild_view_locked() {
    refs_.clear();
    refs_.reserve(count_);
    for (uint32_t i = 0; i < count_; ++i) {
        const uint32_t slot = (head_ + i) % CAPACITY;
        const LogEntry& entry = entries_[slot];
        if ((filter_levels_ & LevelBit(entry.level)) == 0)
            continue;
        if ((filter_categories_ & CategoryBit(entry.category)) == 0)
            continue;
        refs_.push_back(Ref{.slot = slot, .sequence = entry.sequence});
    }
    view_revision_ = revision_;
    view_levels_ = filter_levels_;
    view_categories_ = filter_categories_;
}

void LogHistory::ensure_view_locked() {
    if (view_revision_ == revision_ && view_levels_ == filter_levels_ && view_categories_ == filter_categories_)
        return;
    rebuild_view_locked();
}

const LogEntry* LogHistory::view_entry_locked(const uint32_t index) {
    ensure_view_locked();
    if (index >= refs_.size())
        return nullptr;
    const Ref ref = refs_[index];
    const LogEntry& entry = entries_[ref.slot];
    // Кольцо успело затереть запись между rebuild и этим вызовом: строка устарела.
    if (entry.sequence != ref.sequence)
        return nullptr;
    return &entry;
}

LogLevel LogHistory::view_level(const uint32_t index) {
    std::lock_guard lock(mutex_);
    const LogEntry* entry = view_entry_locked(index);
    return entry != nullptr ? entry->level : LogLevel::Info;
}

LogCategory LogHistory::view_category(const uint32_t index) {
    std::lock_guard lock(mutex_);
    const LogEntry* entry = view_entry_locked(index);
    return entry != nullptr ? entry->category : LogCategory::General;
}

uint32_t LogHistory::view_line(const uint32_t index) {
    std::lock_guard lock(mutex_);
    const LogEntry* entry = view_entry_locked(index);
    return entry != nullptr ? entry->line : 0;
}

bool LogHistory::view_time(const uint32_t index, eastl::string& out) {
    std::lock_guard lock(mutex_);
    const LogEntry* entry = view_entry_locked(index);
    if (entry == nullptr)
        return false;
    out = entry->time;
    return true;
}

bool LogHistory::view_message(const uint32_t index, eastl::string& out) {
    std::lock_guard lock(mutex_);
    const LogEntry* entry = view_entry_locked(index);
    if (entry == nullptr)
        return false;
    out = entry->message;
    return true;
}

bool LogHistory::view_file(const uint32_t index, eastl::string& out) {
    std::lock_guard lock(mutex_);
    const LogEntry* entry = view_entry_locked(index);
    if (entry == nullptr)
        return false;
    out = entry->file;
    return true;
}

void HistoryLogSink::write(const LogRecord& record) {
    LogHistory::instance().append(record);
}

}  // namespace tryengine::core
