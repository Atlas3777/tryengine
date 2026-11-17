#pragma once

//! Engine profiler: CPU scope timings plus GPU pass timestamps coming from GpuProfiler.
//!
//! Instrumentation is a compile-time switch and is NOT tied to NDEBUG: profiling is meant
//! for release builds. Set TRY_PROFILE_ENABLED to 0 and rebuild — the macros expand to
//! `((void)0)`, scope bodies are left untouched and no profiler code is compiled at all.
//!
//! Recording is off by default and is a runtime switch: SetRecording toggles it without a
//! restart, and with it off a scope costs one atomic load (no clock, no lock, no event).
//! The frame root is still measured, so LastFrameMs() keeps answering.
//!
//! Usage:
//!   TRY_PROFILE_BEGIN_FRAME("EditorApp::Run");  // top of the frame body, opens the root
//!   TRY_PROFILE_SCOPE(name);                    // any block/function
//!   TRY_PROFILE_FUNCTION();                     // name = __FUNCTION__
//!   TRY_PROFILE_END_FRAME();                    // bottom of the frame body
//!
//! The frame is delimited by the profiler, not by the app: BeginFrame opens the root scope and
//! EndFrame closes it, and the measured interval of that root IS the frame duration. That is
//! what makes the root bar span its whole frame - the frame time and the root's bar can not
//! disagree, and nothing the app does between iterations can fall outside of it.
//!
//! Every closed frame keeps its per-call events (name, start offset, duration, depth and the
//! index of the enclosing call), so a timeline and a per-frame call tree can be drawn from
//! them. History is never trimmed: it grows until Reset().
//!
//! A capture can be written while the app runs (StartCapture): a compact binary stream of
//! frames plus a one-line-per-second status file under ./logs/. Neither needs a UI, so
//! game_client is profiled the same way as the editor, and a capture can be imported back
//! (ImportLog) to be looked at in the panel. See ProfilerLog.hpp for the file format.

#ifndef TRY_PROFILE_ENABLED
#define TRY_PROFILE_ENABLED 1
#endif

#if TRY_PROFILE_ENABLED

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "engine/core/ProfilerLog.hpp"

namespace tryengine::core {

/// Upper bound of registered scopes; overflow is dropped instead of reallocating.
inline constexpr uint32_t kProfilerMaxScopes = 512;
/// Depth the timeline draws; deeper calls are clamped to the last lane.
inline constexpr uint32_t kProfilerMaxDepth = 32;
/// Hash table size for scope lookup: power of two, open addressing.
inline constexpr uint32_t kProfilerHashSize = 1024;
/// Frames held back before a committed frame goes to the capture: GPU timestamps are read
/// kMaxFramesInFlight frames late, so a frame is complete only after its successors commit.
inline constexpr size_t kProfilerCaptureDelayFrames = 4;
/// Default cap of the frame stream. The status file is not capped (it is ~120 bytes/second).
inline constexpr uint64_t kProfilerCaptureDefaultBytes = 64ull * 1024 * 1024;

static_assert((kProfilerHashSize & (kProfilerHashSize - 1)) == 0, "kProfilerHashSize must be a power of two");

/// Where a scope came from. GPU scopes are emitted by GpuProfiler at render graph pass bounds.
enum class ProfileScopeKind : uint8_t { Cpu, Gpu };

/// One measured call inside a frame. This is what the timeline draws.
struct ProfileEvent {
    uint32_t scope = 0;     ///< index into the scope table
    float start_ms = 0.0f;  ///< offset from the frame start
    float duration_ms = 0.0f;
    int32_t parent = -1;  ///< index of the enclosing call in the same frame, -1 for a root
    uint8_t depth = 0;    ///< nesting level, the timeline lane
    uint8_t kind = 0;     ///< ProfileScopeKind
};

/// A closed frame: total duration plus every call that happened in it.
struct ProfileFrameRecord {
    float delta_ms = 0.0f;
    eastl::vector<ProfileEvent> events;
};

/// One registered name. `totals[i]` is the summed time in frame `first_frame + i`.
struct ProfileScopeEntry {
    eastl::string name;
    ProfileScopeKind kind = ProfileScopeKind::Cpu;
    eastl::vector<float> totals;
    uint32_t first_frame = 0;
    uint32_t calls = 0;  ///< calls in the last closed frame
    uint32_t frame_calls = 0;
    uint8_t depth = 0;
    uint64_t total_calls = 0;  ///< calls over the whole recorded history
    double total_ms = 0.0;     ///< summed time over the whole recorded history
    float min_ms = 0.0f;       ///< shortest call over the whole recorded history
    float max_ms = 0.0f;       ///< longest call over the whole recorded history
};

class Profiler {
public:
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    static Profiler& Instance() {
        static Profiler instance;
        return instance;
    }

    /// Runtime toggle. Off by default; one relaxed atomic load in the hot path, so a scope with
    /// recording off costs nothing measurable. The compile-time switch that really removes the
    /// code is TRY_PROFILE_ENABLED.
    [[nodiscard]] bool IsRecording() const {
        return recording_.load(std::memory_order_relaxed);
    }

    void SetRecording(const bool enabled) {
        std::lock_guard lock(mutex_);
        recording_.store(enabled, std::memory_order_relaxed);
    }

    /// Duration of the last closed frame. Answers with recording off too: the frame root is
    /// always measured, only the per-call events are skipped.
    [[nodiscard]] float LastFrameMs() const {
        std::lock_guard lock(mutex_);
        return last_frame_ms_;
    }

    /// Opens a scope and returns a token for EndScope. Depth comes from the per-thread stack.
    /// With recording off nothing is measured and 0 is returned (EndScope ignores it).
    uint64_t BeginScope(const eastl::string_view name) {
        if (!recording_.load(std::memory_order_relaxed))
            return 0;
        return BeginScopeAt(name, std::chrono::steady_clock::now());
    }

    /// Opens a scope measured from `at` instead of now. Only the frame root uses it: it starts
    /// where the previous frame ended, so a frame covers the whole interval between two frame
    /// ends and the root bar can not be a millisecond short.
    uint64_t BeginScopeAt(const eastl::string_view name, const std::chrono::steady_clock::time_point at) {
        auto& stack = ThreadScopes();
        const auto now = at;
        const uint32_t depth = static_cast<uint32_t>(std::min<size_t>(stack.size(), kProfilerMaxDepth - 1));

        ProfileScopeEntry* entry = nullptr;
        uint32_t index = 0;
        float start_ms = 0.0f;
        int32_t event_index = -1;
        uint32_t serial = 0;
        {
            std::lock_guard lock(mutex_);
            OpenFrameLocked(now);
            start_ms = std::chrono::duration<float, std::milli>(now - frame_start_).count();
            if (recording_.load(std::memory_order_relaxed)) {
                entry = &GetOrRegister(name, ProfileScopeKind::Cpu, depth, static_cast<uint32_t>(frames_.size()));
                index = static_cast<uint32_t>(entry - entries_);
                entry->depth = static_cast<uint8_t>(depth);
                ++entry->frame_calls;
                ++entry->total_calls;
                // Слот события бронируется сразу: обёртка закрывается позже детей, и до этого
                // момента индекса её события, на который могли бы сослаться дети, не существует.
                // Родитель ссылается на свою бронь, только если она из этого же кадра: иначе
                // (END_FRAME внутри зоны) индекс указывал бы в чужую запись.
                const int32_t parent = stack.empty() || stack.back().serial != serial_ ? -1 : stack.back().event_index;
                current_events_.push_back(ProfileEvent{index, start_ms, 0.0f, parent, static_cast<uint8_t>(depth),
                                                       static_cast<uint8_t>(ProfileScopeKind::Cpu)});
                event_index = static_cast<int32_t>(current_events_.size()) - 1;
                serial = serial_;
            }
        }

        static thread_local uint64_t next_id = 0;
        const uint64_t id = ++next_id;
        stack.push_back(PendingScope{id, entry, index, now, start_ms, depth, event_index, serial});
        return id;
    }

    /// Closes a scope opened by BeginScope and completes its reserved event in the open frame.
    void EndScope(const uint64_t id) {
        if (id == 0)
            return;  // зона не открывалась: запись была выключена
        auto& stack = ThreadScopes();
        for (size_t i = stack.size(); i-- > 0;) {
            if (stack[i].id != id)
                continue;
            const PendingScope scope = stack[i];
            stack.erase(stack.begin() + static_cast<ptrdiff_t>(i));
            const auto end = std::chrono::steady_clock::now();
            const float ms = std::chrono::duration<float, std::milli>(end - scope.start).count();
            std::lock_guard lock(mutex_);
            // The frame root reads its own duration from here, so it is filled even when the
            // panel has recording switched off: the frame time must still be wall-clock true.
            last_closed_ms_ = ms;
            if (scope.entry == nullptr)
                return;
            OpenFrameLocked(end);
            AddAt(scope.entry->totals, scope.entry->first_frame, static_cast<uint32_t>(frames_.size()), ms);
            scope.entry->total_ms += ms;
            if (scope.entry->total_calls <= 1) {
                scope.entry->min_ms = ms;
                scope.entry->max_ms = ms;
            } else {
                scope.entry->min_ms = ms < scope.entry->min_ms ? ms : scope.entry->min_ms;
                scope.entry->max_ms = ms > scope.entry->max_ms ? ms : scope.entry->max_ms;
            }
            if (scope.event_index >= 0 && scope.serial == serial_ &&
                static_cast<size_t>(scope.event_index) < current_events_.size()) {
                current_events_[static_cast<size_t>(scope.event_index)].duration_ms = ms;
            } else {
                // Кадр закрылся, пока зона была открыта: бронь уже уехала в историю, поэтому
                // пишем событие заново — иначе его пришлось бы искать в чужом кадре.
                AddEvent(scope.index, scope.start_ms, ms, scope.depth, ProfileScopeKind::Cpu);
            }
            return;
        }
    }

    /// GPU pass measured by GpuProfiler, attached to the frame it actually belongs to.
    void AppendGpuEvent(const uint32_t frame, const eastl::string_view name, const float start_ms,
                        const float duration_ms) {
        std::lock_guard lock(mutex_);
        if (frame >= frames_.size())
            return;
        auto& entry = GetOrRegister(name, ProfileScopeKind::Gpu, 0, frame);
        const uint32_t index = static_cast<uint32_t>(&entry - entries_);
        AddAt(entry.totals, entry.first_frame, frame, duration_ms);
        // GPU-пасс тоже вызов: без этого в блоке выделения у него были бы нули в истории.
        ++entry.total_calls;
        entry.total_ms += duration_ms;
        if (entry.total_calls <= 1) {
            entry.min_ms = duration_ms;
            entry.max_ms = duration_ms;
        } else {
            entry.min_ms = duration_ms < entry.min_ms ? duration_ms : entry.min_ms;
            entry.max_ms = duration_ms > entry.max_ms ? duration_ms : entry.max_ms;
        }

        // Пасс идёт внутри кейфрейма: подвешиваем его к корневой зоне кадра, если она записана.
        auto& events = frames_[frame].events;
        int32_t parent = -1;
        for (size_t i = events.size(); i-- > 0;) {
            if (events[i].depth == 0) {
                parent = static_cast<int32_t>(i);
                break;
            }
        }
        events.push_back(ProfileEvent{index, start_ms, duration_ms, parent, static_cast<uint8_t>(parent >= 0 ? 1 : 0),
                                      static_cast<uint8_t>(ProfileScopeKind::Gpu)});
    }

    /// Opens a frame and its root scope. The frame runs from the end of the previous frame to
    /// the end of this one, so the root covers the loop tail as well: nothing the app does
    /// between iterations falls outside of the frame, and the frame time stays wall-clock true.
    void BeginFrame(const eastl::string_view name) {
        if (frame_root_id_ != 0) {
            // BeginFrame without EndFrame: close the previous frame instead of growing it.
            EndFrame();
        }
        {
            std::lock_guard lock(mutex_);
            if (frame_open_) {
                // A scope opened before the loop (startup code) already started a frame.
                const auto stale_end = std::chrono::steady_clock::now();
                const float ms = std::chrono::duration<float, std::milli>(stale_end - frame_start_).count();
                CommitFrameLocked(ms);
                last_frame_end_ = stale_end;
            }
            frame_open_ = true;
            frame_start_ = last_frame_end_ == std::chrono::steady_clock::time_point{} ? std::chrono::steady_clock::now()
                                                                                      : last_frame_end_;
            current_events_.clear();
            // Брони открытых зон теперь указывают в историю: пусть такие зоны закроются заново.
            ++serial_;
        }
        // The root's interval is [previous frame end, this frame end]: start 0, duration = delta.
        frame_root_id_ = BeginScopeAt(name, frame_start_);
    }

    /// Closes the root scope and stores the frame. The delta is the root's own duration, so
    /// the two can not disagree and the root bar always spans the whole frame column.
    void EndFrame() {
        const auto end = std::chrono::steady_clock::now();
        if (frame_root_id_ != 0) {
            EndScope(frame_root_id_);
            frame_root_id_ = 0;
            std::lock_guard lock(mutex_);
            last_frame_end_ = end;
            CommitFrameLocked(last_closed_ms_);
            return;
        }
        // No BeginFrame: still close whatever frame the scopes opened on their own.
        std::lock_guard lock(mutex_);
        if (!frame_open_)
            return;
        last_frame_end_ = end;
        const float ms = std::chrono::duration<float, std::milli>(end - frame_start_).count();
        CommitFrameLocked(ms);
    }

    [[nodiscard]] uint32_t FrameCount() const {
        std::lock_guard lock(mutex_);
        return static_cast<uint32_t>(frames_.size());
    }

    [[nodiscard]] float FrameDeltaMs(const uint32_t frame) const {
        std::lock_guard lock(mutex_);
        return frame < frames_.size() ? frames_[frame].delta_ms : 0.0f;
    }

    [[nodiscard]] uint32_t FrameEventCount(const uint32_t frame) const {
        std::lock_guard lock(mutex_);
        return frame < frames_.size() ? static_cast<uint32_t>(frames_[frame].events.size()) : 0;
    }

    [[nodiscard]] const ProfileEvent* FrameEvent(const uint32_t frame, const uint32_t index) const {
        std::lock_guard lock(mutex_);
        if (frame >= frames_.size() || index >= frames_[frame].events.size())
            return nullptr;
        return &frames_[frame].events[index];
    }

    /// Index of the enclosing call inside the same frame, -1 when the call is a root.
    [[nodiscard]] int32_t EventParent(const uint32_t frame, const uint32_t index) const {
        std::lock_guard lock(mutex_);
        if (frame >= frames_.size() || index >= frames_[frame].events.size())
            return -1;
        return frames_[frame].events[index].parent;
    }

    [[nodiscard]] uint32_t ScopeCount() const {
        std::lock_guard lock(mutex_);
        return scope_count_;
    }

    [[nodiscard]] const ProfileScopeEntry& Scope(const uint32_t index) const {
        std::lock_guard lock(mutex_);
        if (index >= scope_count_) {
            static const ProfileScopeEntry kEmpty{};
            return kEmpty;
        }
        return entries_[index];
    }

    /// Frames available for the scope, counted from its registration.
    [[nodiscard]] uint32_t ScopeHistoryCount(const uint32_t scope) const {
        std::lock_guard lock(mutex_);
        if (scope >= scope_count_)
            return 0;
        const auto& entry = entries_[scope];
        const uint32_t frames = static_cast<uint32_t>(frames_.size());
        return frames > entry.first_frame ? frames - entry.first_frame : 0;
    }

    /// Calls of the scope over the whole recorded history.
    [[nodiscard]] uint64_t ScopeTotalCalls(const uint32_t scope) const {
        std::lock_guard lock(mutex_);
        return scope < scope_count_ ? entries_[scope].total_calls : 0u;
    }

    /// Summed time of every call of the scope, over the whole recorded history.
    [[nodiscard]] float ScopeTotalMs(const uint32_t scope) const {
        std::lock_guard lock(mutex_);
        return scope < scope_count_ ? static_cast<float>(entries_[scope].total_ms) : 0.0f;
    }

    [[nodiscard]] float ScopeMinMs(const uint32_t scope) const {
        std::lock_guard lock(mutex_);
        return scope < scope_count_ ? entries_[scope].min_ms : 0.0f;
    }

    [[nodiscard]] float ScopeMaxMs(const uint32_t scope) const {
        std::lock_guard lock(mutex_);
        return scope < scope_count_ ? entries_[scope].max_ms : 0.0f;
    }

    /// Summed scope time in an absolute frame index (0 = the oldest stored frame).
    [[nodiscard]] float ScopeHistoryAt(const uint32_t scope, const uint32_t frame) const {
        std::lock_guard lock(mutex_);
        if (scope >= scope_count_ || frame >= frames_.size())
            return 0.0f;
        return At(entries_[scope], frame);
    }

    /// Drops every recorded frame and scope. Names re-register on the next call. A running
    /// capture is stopped: scope ids are reused after a reset, and a stream that renamed them
    /// mid-file would mislabel the frames recorded before it.
    void Reset() {
        std::lock_guard lock(mutex_);
        StopCaptureLocked();
        frames_.clear();
        current_events_.clear();
        frame_open_ = false;
        frame_root_id_ = 0;
        last_closed_ms_ = 0.0f;
        last_frame_end_ = {};
        ++serial_;
        for (uint32_t i = 0; i < scope_count_; ++i)
            entries_[i] = ProfileScopeEntry{};
        scope_count_ = 0;
        std::fill(std::begin(buckets_), std::end(buckets_), -1);
        status_prev_ms_.clear();
        status_prev_frames_ = 0;
    }

    /// Dumps the whole history to a timestamped capture under ./logs/ (the same compact
    /// format a running capture writes, so it can be imported back). Returns the written
    /// path, or an empty string if nothing was recorded or the file could not be created.
    eastl::string ExportToLogs() const;

    /// Starts writing a capture under ./logs/: a compact binary stream of frames plus a text
    /// file with one status line per second (`tail -f` works on it). Both are written from
    /// the frame path, so an app with no UI can be profiled the same way as the editor.
    /// `max_bytes` 0 means no cap on the stream; the status file is never capped.
    bool StartCapture(uint64_t max_bytes = kProfilerCaptureDefaultBytes);
    void StopCapture();

    [[nodiscard]] bool IsCapturing() const;
    [[nodiscard]] eastl::string CapturePath() const;
    [[nodiscard]] eastl::string StatusPath() const;
    [[nodiscard]] uint64_t CaptureBytes() const;
    [[nodiscard]] uint32_t CapturedFrames() const;
    /// True once the frame stream reached its cap and stopped (the status file keeps going).
    [[nodiscard]] bool IsCaptureCapped() const;

    /// Replaces the history with the frames of a capture file. Returns false, leaving the
    /// history untouched, when the file is missing or is not a capture.
    bool ImportLog(const eastl::string_view path);

    /// Newest capture under ./logs/, or an empty string when there is none.
    static eastl::string LatestLogPath();

    // Aggregates over an absolute frame range. The timeline calls these instead of walking
    // every call of every visible frame: one call answers a whole range under one lock, so a
    // zoomed-out capture of 50k frames costs a few hundred calls, not a million.
    [[nodiscard]] uint64_t RangeEventCount(uint32_t first, uint32_t last) const;
    [[nodiscard]] float RangeFrameMsMax(uint32_t first, uint32_t last) const;
    [[nodiscard]] float RangeFrameMsAvg(uint32_t first, uint32_t last) const;
    [[nodiscard]] float RangeScopeMs(uint32_t scope, uint32_t first, uint32_t last) const;

private:
    Profiler() {
        std::fill(std::begin(buckets_), std::end(buckets_), -1);
    }

    struct PendingScope {
        uint64_t id = 0;
        ProfileScopeEntry* entry = nullptr;  // nullptr when recording is off
        uint32_t index = 0;
        std::chrono::steady_clock::time_point start;
        float start_ms = 0.0f;
        uint32_t depth = 0;
        int32_t event_index = -1;  // reserved slot in current_events_, -1 when not recorded
        uint32_t serial = 0;       // serial_ at BeginScope: tells if the slot is still valid
    };

    static eastl::vector<PendingScope>& ThreadScopes() {
        static thread_local eastl::vector<PendingScope> scopes;
        return scopes;
    }

    static uint32_t HashName(const eastl::string_view name) {
        uint32_t hash = 2166136261u;
        for (const char c : name) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }

    static float At(const ProfileScopeEntry& entry, const uint32_t frame) {
        if (frame < entry.first_frame)
            return 0.0f;
        const size_t index = frame - entry.first_frame;
        return index < entry.totals.size() ? entry.totals[index] : 0.0f;
    }

    static void AddAt(eastl::vector<float>& totals, const uint32_t first_frame, const uint32_t frame,
                      const float value) {
        if (frame < first_frame)
            return;
        const size_t index = frame - first_frame;
        if (totals.size() <= index)
            totals.resize(index + 1, 0.0f);
        totals[index] += value;
    }

    /// Hands the open frame over to the history and resets the per-frame counters. With
    /// recording off the frame is not stored (history stays empty and does not grow), but its
    /// duration is kept for LastFrameMs().
    void CommitFrameLocked(const float frame_ms) {
        last_frame_ms_ = frame_ms;
        if (recording_.load(std::memory_order_relaxed)) {
            frames_.push_back(ProfileFrameRecord{frame_ms, eastl::move(current_events_)});
            // Кадр стал историей — только теперь его можно отдать в лог и в строку статуса.
            WriteCaptureLocked();
        }
        current_events_.clear();
        frame_open_ = false;
        // Брони открытых зон теперь указывают в историю: пусть такие зоны закроются заново.
        ++serial_;
        for (uint32_t i = 0; i < scope_count_; ++i) {
            entries_[i].calls = entries_[i].frame_calls;
            entries_[i].frame_calls = 0;
        }
        WriteCaptureLocked();
    }

    void OpenFrameLocked(const std::chrono::steady_clock::time_point now) {
        if (frame_open_)
            return;
        frame_open_ = true;
        frame_start_ = now;
        current_events_.clear();
        ++serial_;
    }

    void AddEvent(const uint32_t scope, const float start_ms, const float duration_ms, const uint32_t depth,
                  const ProfileScopeKind kind) {
        if (!recording_.load(std::memory_order_relaxed))
            return;
        current_events_.push_back(
            ProfileEvent{scope, start_ms, duration_ms, -1, static_cast<uint8_t>(depth), static_cast<uint8_t>(kind)});
    }

    ProfileScopeEntry& GetOrRegister(const eastl::string_view name, const ProfileScopeKind kind, const uint32_t depth,
                                     const uint32_t first_frame) {
        const uint32_t hash = HashName(name);
        uint32_t slot = hash & (kProfilerHashSize - 1);
        for (uint32_t probe = 0; probe < kProfilerHashSize; ++probe) {
            const int32_t index = buckets_[slot];
            if (index < 0)
                break;
            auto& candidate = entries_[static_cast<uint32_t>(index)];
            if (candidate.kind == kind && candidate.name == name)
                return candidate;
            slot = (slot + 1) % kProfilerHashSize;
        }

        if (scope_count_ >= kProfilerMaxScopes) {
            static ProfileScopeEntry overflow;
            return overflow;
        }

        const uint32_t index = scope_count_++;
        auto& entry = entries_[index];
        entry.name = name;
        entry.kind = kind;
        entry.depth = static_cast<uint8_t>(depth);
        entry.first_frame = first_frame;
        // GPU entries arrive late: pad so index == frame - first_frame stays valid.
        const uint32_t frames = static_cast<uint32_t>(frames_.size());
        if (frames > first_frame)
            entry.totals.resize(frames - first_frame, 0.0f);
        buckets_[slot] = static_cast<int32_t>(index);
        // Имя новое: читателю потока без этой записи id события ни о чём не говорит.
        capture_.WriteScope(index, static_cast<uint8_t>(kind), static_cast<uint8_t>(depth), name);
        return entry;
    }

    void StopCaptureLocked();

    /// Отдаёт кадр в бинарный поток и раз в секунду дописывает строку статуса.
    void WriteCaptureLocked();
    void WritePendingFramesLocked(size_t ready);
    void WriteStatusLocked();

    mutable std::mutex mutex_;
    ProfileScopeEntry entries_[kProfilerMaxScopes]{};
    int32_t buckets_[kProfilerHashSize];
    uint32_t scope_count_ = 0;

    eastl::vector<ProfileFrameRecord> frames_;
    eastl::vector<ProfileEvent> current_events_;
    bool frame_open_ = false;
    uint64_t frame_root_id_ = 0;   // token of the root scope opened by BeginFrame, 0 = none
    float last_closed_ms_ = 0.0f;  // duration of the call EndScope closed last: the frame delta
    // Where the previous frame ended: the next frame starts there, so the interval between two
    // frames is measured completely instead of losing the loop tail between them.
    std::chrono::steady_clock::time_point last_frame_end_{};
    uint32_t serial_ = 0;  // raised whenever current_events_ is handed over to the history
    std::chrono::steady_clock::time_point frame_start_{};

    std::atomic<bool> recording_{false};
    float last_frame_ms_ = 0.0f;  // длительность последнего кадра, считается и без записи

    // Каптион: бинарный поток кадров и текстовая строка статуса, обе работают без UI.
    ProfilerLog capture_;
    ProfilerStatusLog status_log_;
    std::chrono::steady_clock::time_point status_at_{};
    eastl::vector<double> status_prev_ms_;  // суммарное время каждой зоны на прошлом тике
    uint32_t status_prev_frames_ = 0;
    uint32_t capture_next_frame_ = 0;  // следующий кадр истории, ещё не ушедший в поток
};

/// RAII scope. Placed by the macro, not used directly.
class ProfileScope {
public:
    explicit ProfileScope(const eastl::string_view name) : token_(Profiler::Instance().BeginScope(name)) {}

    ~ProfileScope() {
        Profiler::Instance().EndScope(token_);
    }

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    uint64_t token_ = 0;
};

}  // namespace tryengine::core

#define TRY_PROFILE_CONCAT_IMPL(a, b) a##b
#define TRY_PROFILE_CONCAT(a, b) TRY_PROFILE_CONCAT_IMPL(a, b)
#define TRY_PROFILE_SCOPE(name) ::tryengine::core::ProfileScope TRY_PROFILE_CONCAT(_try_profile_scope_, __LINE__)(name)
#define TRY_PROFILE_FUNCTION() TRY_PROFILE_SCOPE(__FUNCTION__)
#define TRY_PROFILE_BEGIN_FRAME(name) ::tryengine::core::Profiler::Instance().BeginFrame(name)
#define TRY_PROFILE_END_FRAME() ::tryengine::core::Profiler::Instance().EndFrame()

#else  // TRY_PROFILE_ENABLED

// Profiling is compiled out: the macro argument is not evaluated, scope bodies compile as
// usual, and not a single instruction of measurement remains.
#define TRY_PROFILE_SCOPE(name) ((void) 0)
#define TRY_PROFILE_FUNCTION() ((void) 0)
#define TRY_PROFILE_BEGIN_FRAME(name) ((void) 0)
#define TRY_PROFILE_END_FRAME() ((void) 0)

#endif  // TRY_PROFILE_ENABLED
