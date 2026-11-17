#include "engine/core/Profiler.hpp"

#if TRY_PROFILE_ENABLED

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "engine/core/Log.hpp"

namespace tryengine::core {
namespace {

/// Structure of one imported call: what a Repeat record takes from the previous frame.
struct ImportedKey {
    uint32_t scope = 0;
    uint32_t parent_code = 0;
    uint8_t depth = 0;
    uint8_t kind = 0;

    bool operator==(const ImportedKey&) const = default;
};

constexpr char kMagic[8] = {'T', 'R', 'Y', 'P', 'F', 'L', '0', '1'};
constexpr uint8_t kTagScope = 1;
constexpr uint8_t kTagFrame = 2;
constexpr uint8_t kTagRepeat = 3;

/// LEB128 reader over a FILE*. Every read answers false on EOF or garbage.
struct LogReader {
    std::FILE* file = nullptr;

    bool Byte(uint8_t& out) {
        const int value = std::fgetc(file);
        if (value == EOF)
            return false;
        out = static_cast<uint8_t>(value);
        return true;
    }

    bool Varint(uint64_t& out) {
        out = 0;
        for (uint32_t shift = 0; shift < 64; shift += 7) {
            uint8_t byte = 0;
            if (!Byte(byte))
                return false;
            out |= static_cast<uint64_t>(byte & 0x7Fu) << shift;
            if ((byte & 0x80u) == 0)
                return true;
        }
        return false;
    }

    bool Raw(char* dst, const size_t size) {
        return std::fread(dst, 1, size, file) == size;
    }
};

/// Scope names land in a status line, which is space and comma separated: keep it one token.
void SanitizeName(const eastl::string& name, char* out, const size_t size) {
    const size_t available = size - 1;
    const size_t length = static_cast<size_t>(name.size()) < available ? static_cast<size_t>(name.size()) : available;
    for (size_t i = 0; i < length; ++i) {
        const char c = name[i];
        out[i] = (c == ' ' || c == ',' || c == '\t') ? '_' : c;
    }
    out[length] = '\0';
}

}  // namespace

// ---------------------------------------------------------------------------
// Каптион: кадр-лог + строка статуса
// ---------------------------------------------------------------------------

bool Profiler::StartCapture(const uint64_t max_bytes) {
    std::lock_guard lock(mutex_);
    if (capture_.IsActive() || status_log_.IsActive())
        return true;

    // Каптион без записи писал бы пустые кадры: кнопка Capture включает и Record.
    recording_.store(true, std::memory_order_relaxed);

    const eastl::string base = ProfilerTimestampedBase("profiler");
    eastl::string frames_path = base;
    frames_path += ".tryprof";
    if (!capture_.Start(frames_path, max_bytes))
        return false;
    // Зоны, зарегистрированные до старта, уже есть в таблице: без этого прогона читатель
    // потока не узнал бы их имён.
    for (uint32_t i = 0; i < scope_count_; ++i)
        capture_.WriteScope(i, static_cast<uint8_t>(entries_[i].kind), entries_[i].depth, entries_[i].name);

    eastl::string status_path = base;
    status_path += ".txt";
    status_log_.Start(status_path);

    status_at_ = {};
    status_prev_frames_ = 0;
    status_prev_ms_.clear();
    // Каптион пишет то, что происходит с этой секунды: уже собранная история остаётся в
    // памяти, и её можно отдельно выгрузить через ExportToLogs.
    capture_next_frame_ = static_cast<uint32_t>(frames_.size());
    LogInfo("Profiler: кадр-лог {} (кап {} байт)", frames_path.c_str(), max_bytes);
    return true;
}

void Profiler::StopCapture() {
    std::lock_guard lock(mutex_);
    StopCaptureLocked();
}

void Profiler::StopCaptureLocked() {
    if (!capture_.IsActive() && !status_log_.IsActive())
        return;
    if (capture_.IsActive()) {
        // Хвост, который держался ради поздних GPU-пассов, дописывается здесь.
        WritePendingFramesLocked(frames_.size());
        capture_.Flush();
        LogInfo("Profiler: кадр-лог {} закрыт ({} кадров, {} байт)", capture_.Path().c_str(), capture_.Frames(),
                capture_.Bytes());
    }
    capture_.Stop();
    status_log_.Stop();
    status_at_ = {};
    status_prev_frames_ = 0;
    status_prev_ms_.clear();
    capture_next_frame_ = 0;
}

bool Profiler::IsCapturing() const {
    std::lock_guard lock(mutex_);
    return capture_.IsActive() || status_log_.IsActive();
}

eastl::string Profiler::CapturePath() const {
    std::lock_guard lock(mutex_);
    return capture_.Path();
}

eastl::string Profiler::StatusPath() const {
    std::lock_guard lock(mutex_);
    return status_log_.Path();
}

uint64_t Profiler::CaptureBytes() const {
    std::lock_guard lock(mutex_);
    return capture_.Bytes();
}

uint32_t Profiler::CapturedFrames() const {
    std::lock_guard lock(mutex_);
    return capture_.Frames();
}

bool Profiler::IsCaptureCapped() const {
    std::lock_guard lock(mutex_);
    return capture_.Capped();
}

eastl::string Profiler::LatestLogPath() {
    return ProfilerLatestLog("tryprof");
}

void Profiler::WriteCaptureLocked() {
    if (capture_.IsActive()) {
        // GPU-пассы приходят на kMaxFramesInFlight кадров позже: кадр уходит в поток только
        // тогда, когда все его преемники уже зафиксированы, иначе пассы в лог не попадут.
        const size_t ready =
            frames_.size() > kProfilerCaptureDelayFrames ? frames_.size() - kProfilerCaptureDelayFrames : 0;
        WritePendingFramesLocked(ready);
    }
    WriteStatusLocked();
}

void Profiler::WritePendingFramesLocked(const size_t ready) {
    for (; capture_next_frame_ < ready && capture_.IsActive(); ++capture_next_frame_) {
        const ProfileFrameRecord& record = frames_[capture_next_frame_];
        capture_.WriteFrame(record.delta_ms, record.events.data(), record.events.size());
    }
}

void Profiler::WriteStatusLocked() {
    if (!status_log_.IsActive())
        return;

    const auto now = std::chrono::steady_clock::now();
    if (status_at_ == std::chrono::steady_clock::time_point{}) {
        // Первый тик только запоминает точку отсчёта: окно статуса — ровно одна секунда.
        status_at_ = now;
        status_prev_frames_ = static_cast<uint32_t>(frames_.size());
        status_prev_ms_.resize(scope_count_);
        for (uint32_t i = 0; i < scope_count_; ++i)
            status_prev_ms_[i] = entries_[i].total_ms;
        return;
    }

    const double wall_ms = std::chrono::duration<double, std::milli>(now - status_at_).count();
    if (wall_ms < 1000.0)
        return;

    const uint32_t frames = static_cast<uint32_t>(frames_.size());
    const uint32_t count = frames - eastl::min(frames, status_prev_frames_);
    double frame_sum = 0.0;
    float frame_max = 0.0f;
    uint64_t events = 0;
    for (uint32_t f = status_prev_frames_; f < frames; ++f) {
        frame_sum += static_cast<double>(frames_[f].delta_ms);
        frame_max = eastl::max(frame_max, frames_[f].delta_ms);
        events += frames_[f].events.size();
    }

    // Топ-3 зоны по приросту суммарного времени за это окно: прирост, а не абсолют, иначе
    // список никогда бы не менялся — с начала прогона всегда побеждает самая частая зона.
    uint32_t top_scope[3] = {0, 0, 0};
    double top_ms[3] = {0.0, 0.0, 0.0};
    auto consider = [&](const uint32_t i) {
        const double total = static_cast<double>(entries_[i].total_ms);
        const double previous = i < status_prev_ms_.size() ? status_prev_ms_[i] : 0.0;
        const double delta = total - previous;
        if (delta <= 0.0)
            return;
        for (int slot = 0; slot < 3; ++slot) {
            if (delta > top_ms[slot]) {
                for (int move = 2; move > slot; --move) {
                    top_ms[move] = top_ms[move - 1];
                    top_scope[move] = top_scope[move - 1];
                }
                top_ms[slot] = delta;
                top_scope[slot] = i;
                break;
            }
        }
    };
    // Корень кадра занимает всё окно и всегда был бы первым: он не ответ на вопрос «кто съел
    // время». Он попадает в список только когда других зон нет вовсе.
    for (uint32_t i = 0; i < scope_count_; ++i)
        if (entries_[i].depth > 0)
            consider(i);
    if (top_ms[0] <= 0.0)
        for (uint32_t i = 0; i < scope_count_; ++i)
            consider(i);

    char line[512] = {};
    const double fps = wall_ms > 0.0 ? static_cast<double>(count) * 1000.0 / wall_ms : 0.0;
    const double dt_avg = count > 0 ? frame_sum / static_cast<double>(count) : 0.0;
    const double events_per_frame = count > 0 ? static_cast<double>(events) / static_cast<double>(count) : 0.0;
    int used = std::snprintf(line, sizeof(line), "frames=%u fps=%.1f dt=%.3f dt_max=%.3f ev=%.1f wall=%.0fms", count,
                             fps, dt_avg, static_cast<double>(frame_max), events_per_frame, wall_ms);

    char list[256] = {};
    size_t listed = 0;
    for (int slot = 0; slot < 3 && used > 0; ++slot) {
        if (top_ms[slot] <= 0.0 || top_scope[slot] >= scope_count_)
            continue;
        char name[64] = {};
        SanitizeName(entries_[top_scope[slot]].name, name, sizeof(name));
        const int written =
            std::snprintf(list + listed, sizeof(list) - listed, "%s%s:%.1f", listed > 0 ? "," : "", name, top_ms[slot]);
        if (written > 0)
            listed += eastl::min(static_cast<size_t>(written), sizeof(list) - listed - 1);
    }
    if (listed > 0 && used > 0)
        std::snprintf(line + used, sizeof(line) - static_cast<size_t>(used), " top=%s", list);

    status_log_.Write(eastl::string_view(line, std::strlen(line)));

    status_at_ = now;
    status_prev_frames_ = frames;
    status_prev_ms_.resize(scope_count_);
    for (uint32_t i = 0; i < scope_count_; ++i)
        status_prev_ms_[i] = entries_[i].total_ms;
}

// ---------------------------------------------------------------------------
// Экспорт и импорт
// ---------------------------------------------------------------------------

eastl::string Profiler::ExportToLogs() const {
    std::lock_guard lock(mutex_);
    if (frames_.empty())
        return {};

    eastl::string path = ProfilerTimestampedBase("profiler");
    path += ".tryprof";
    ProfilerLog log;
    if (!log.Start(path, 0))
        return {};

    for (uint32_t i = 0; i < scope_count_; ++i)
        log.WriteScope(i, static_cast<uint8_t>(entries_[i].kind), entries_[i].depth, entries_[i].name);
    uint64_t events = 0;
    for (const auto& record : frames_) {
        log.WriteFrame(record.delta_ms, record.events.data(), record.events.size());
        events += record.events.size();
    }
    log.Stop();

    LogInfo("Profiler: экспорт {} ({} кадров, {} вызовов, {} байт)", path.c_str(), frames_.size(), events, log.Bytes());
    return path;
}

bool Profiler::ImportLog(const eastl::string_view path) {
    std::lock_guard lock(mutex_);

    const eastl::string file_path(path);
    std::FILE* file = std::fopen(file_path.c_str(), "rb");
    if (file == nullptr) {
        LogError("Profiler: не удалось открыть {} для чтения", file_path.c_str());
        return false;
    }

    char magic[sizeof(kMagic)] = {};
    if (std::fread(magic, 1, sizeof(magic), file) != sizeof(magic) || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        std::fclose(file);
        LogError("Profiler: {} не является кадр-логом (нет заголовка)", file_path.c_str());
        return false;
    }

    // Разбор идёт в отдельные контейнеры: история не должна пострадать от битого файла.
    eastl::vector<ProfileFrameRecord> frames;
    eastl::vector<ProfileScopeEntry> entries(kProfilerMaxScopes);
    eastl::vector<ImportedKey> structure;
    bool have_structure = false;
    uint32_t scope_count = 0;

    auto fail = [&](const char* reason) {
        std::fclose(file);
        LogError("Profiler: {}: {}", file_path.c_str(), reason);
        return false;
    };

    auto add_event = [&entries](const uint32_t frame, const ProfileEvent& event) {
        ProfileScopeEntry& entry = entries[event.scope];
        AddAt(entry.totals, entry.first_frame, frame, event.duration_ms);
        ++entry.total_calls;
        entry.total_ms += event.duration_ms;
        if (entry.total_calls <= 1) {
            entry.min_ms = event.duration_ms;
            entry.max_ms = event.duration_ms;
        } else {
            entry.min_ms = event.duration_ms < entry.min_ms ? event.duration_ms : entry.min_ms;
            entry.max_ms = event.duration_ms > entry.max_ms ? event.duration_ms : entry.max_ms;
        }
    };

    LogReader reader{file};
    for (;;) {
        uint8_t tag = 0;
        if (!reader.Byte(tag)) {
            if (std::ferror(file) != 0)
                return fail("read error");
            break;  // чистый EOF
        }
        if (tag == kTagScope) {
            uint64_t id = 0;
            uint64_t length = 0;
            uint8_t kind = 0;
            uint8_t depth = 0;
            if (!reader.Varint(id) || !reader.Byte(kind) || !reader.Byte(depth) || !reader.Varint(length))
                return fail("truncated scope record");
            if (id >= kProfilerMaxScopes || length > 4096)
                return fail("bad scope record");
            eastl::string name;
            name.resize(static_cast<size_t>(length));
            if (length > 0 && !reader.Raw(name.data(), static_cast<size_t>(length)))
                return fail("truncated scope name");
            ProfileScopeEntry& entry = entries[static_cast<size_t>(id)];
            entry = ProfileScopeEntry{};
            entry.name = eastl::move(name);
            entry.kind = static_cast<ProfileScopeKind>(kind);
            entry.depth = depth;
            entry.first_frame = static_cast<uint32_t>(frames.size());
            scope_count = eastl::max(scope_count, static_cast<uint32_t>(id) + 1);
            continue;
        }
        if (tag != kTagFrame && tag != kTagRepeat)
            return fail("unknown record tag");

        uint64_t dt_us = 0;
        if (!reader.Varint(dt_us))
            return fail("truncated frame record");
        ProfileFrameRecord record;
        record.delta_ms = static_cast<float>(static_cast<double>(dt_us) / 1000.0);
        const uint32_t frame = static_cast<uint32_t>(frames.size());

        if (tag == kTagFrame) {
            uint64_t count = 0;
            if (!reader.Varint(count) || count > 65536)
                return fail("bad frame event count");
            record.events.resize(static_cast<size_t>(count));
            structure.clear();
            structure.reserve(static_cast<size_t>(count));
            for (size_t i = 0; i < record.events.size(); ++i) {
                uint64_t scope = 0;
                uint64_t start_us = 0;
                uint64_t duration_us = 0;
                uint64_t parent_code = 0;
                uint8_t depth_kind = 0;
                if (!reader.Varint(scope) || !reader.Varint(start_us) || !reader.Varint(duration_us) ||
                    !reader.Varint(parent_code) || !reader.Byte(depth_kind)) {
                    return fail("truncated event");
                }
                if (scope >= scope_count || parent_code > i)
                    return fail("event refers to an unknown scope or parent");
                ProfileEvent& event = record.events[i];
                event.scope = static_cast<uint32_t>(scope);
                event.start_ms = static_cast<float>(static_cast<double>(start_us) / 1000.0);
                event.duration_ms = static_cast<float>(static_cast<double>(duration_us) / 1000.0);
                event.parent = parent_code == 0 ? -1 : static_cast<int32_t>(i - parent_code);
                event.depth = static_cast<uint8_t>(depth_kind & 0x1Fu);
                event.kind = static_cast<uint8_t>(depth_kind >> 5);
                structure.push_back(
                    ImportedKey{event.scope, static_cast<uint32_t>(parent_code), event.depth, event.kind});
            }
            have_structure = true;
        } else {
            if (!have_structure)
                return fail("repeat record before any structure");
            record.events.resize(structure.size());
            for (size_t i = 0; i < structure.size(); ++i) {
                uint64_t start_us = 0;
                uint64_t duration_us = 0;
                if (!reader.Varint(start_us) || !reader.Varint(duration_us))
                    return fail("truncated repeat event");
                const ImportedKey& key = structure[i];
                ProfileEvent& event = record.events[i];
                event.scope = key.scope;
                event.start_ms = static_cast<float>(static_cast<double>(start_us) / 1000.0);
                event.duration_ms = static_cast<float>(static_cast<double>(duration_us) / 1000.0);
                event.parent = key.parent_code == 0 ? -1 : static_cast<int32_t>(i - key.parent_code);
                event.depth = key.depth;
                event.kind = key.kind;
            }
        }

        for (const ProfileEvent& event : record.events)
            if (event.scope < scope_count)
                add_event(frame, event);
        frames.push_back(eastl::move(record));
    }
    std::fclose(file);

    if (frames.empty()) {
        LogError("Profiler: {} не содержит ни одного кадра", file_path.c_str());
        return false;
    }

    // Импорт подменяет историю: живой каптион пришлось бы перенумеровать вместе с id зон.
    StopCaptureLocked();
    frames_ = eastl::move(frames);
    current_events_.clear();
    frame_open_ = false;
    frame_root_id_ = 0;
    ++serial_;
    for (uint32_t i = 0; i < scope_count_; ++i)
        entries_[i] = ProfileScopeEntry{};
    for (uint32_t i = 0; i < scope_count; ++i)
        entries_[i] = eastl::move(entries[static_cast<size_t>(i)]);
    scope_count_ = scope_count;
    std::fill(std::begin(buckets_), std::end(buckets_), -1);
    for (uint32_t i = 0; i < scope_count_; ++i) {
        if (entries_[i].name.empty())
            continue;
        uint32_t slot = HashName(entries_[i].name) & (kProfilerHashSize - 1);
        while (buckets_[slot] >= 0)
            slot = (slot + 1) % kProfilerHashSize;
        buckets_[slot] = static_cast<int32_t>(i);
    }
    // «Вызовов в последнем кадре» — то, что панель печатает в списке зон.
    for (const ProfileEvent& event : frames_.back().events)
        if (event.scope < scope_count_)
            ++entries_[event.scope].calls;
    status_prev_ms_.clear();
    status_prev_frames_ = 0;
    capture_next_frame_ = 0;

    LogInfo("Profiler: импортирован {} ({} кадров, {} зон)", file_path.c_str(), frames_.size(), scope_count_);
    return true;
}

// ---------------------------------------------------------------------------
// Агрегаты для таймлайна
// ---------------------------------------------------------------------------

uint64_t Profiler::RangeEventCount(const uint32_t first, const uint32_t last) const {
    std::lock_guard lock(mutex_);
    uint64_t total = 0;
    for (uint32_t frame = first; frame <= last && frame < frames_.size(); ++frame)
        total += frames_[frame].events.size();
    return total;
}

float Profiler::RangeFrameMsMax(const uint32_t first, const uint32_t last) const {
    std::lock_guard lock(mutex_);
    float result = 0.0f;
    for (uint32_t frame = first; frame <= last && frame < frames_.size(); ++frame)
        result = eastl::max(result, frames_[frame].delta_ms);
    return result;
}

float Profiler::RangeFrameMsAvg(const uint32_t first, const uint32_t last) const {
    std::lock_guard lock(mutex_);
    double sum = 0.0;
    uint32_t count = 0;
    for (uint32_t frame = first; frame <= last && frame < frames_.size(); ++frame) {
        sum += static_cast<double>(frames_[frame].delta_ms);
        ++count;
    }
    return count > 0 ? static_cast<float>(sum / static_cast<double>(count)) : 0.0f;
}

float Profiler::RangeScopeMs(const uint32_t scope, const uint32_t first, const uint32_t last) const {
    std::lock_guard lock(mutex_);
    if (scope >= scope_count_)
        return 0.0f;
    float sum = 0.0f;
    for (uint32_t frame = first; frame <= last && frame < frames_.size(); ++frame)
        sum += At(entries_[scope], frame);
    return sum;
}

}  // namespace tryengine::core

#endif  // TRY_PROFILE_ENABLED
