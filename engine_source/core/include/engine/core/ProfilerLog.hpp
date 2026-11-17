#pragma once

//! Compact on-disk capture of profiler frames, written while the application runs.
//!
//! This is the part of profiling that must work with no UI at all: game_client has no ImGui,
//! so the only way to look at its frame is a file. Two files are written side by side under
//! ./logs/ - the binary stream of frames (this class) and a one-line-per-second status
//! (ProfilerStatusLog) that `tail -f` shows.
//!
//! On-disk shape: an 8-byte header, then records. Every integer is a LEB128 varint, so the
//! common values cost one byte:
//!
//!   header   "TRYPFL01"
//!   Scope    u8 tag=1, varint id, u8 kind, u8 depth, varint length, name bytes
//!   Frame    u8 tag=2, varint dt_us, varint count, count x event
//!   Repeat   u8 tag=3, varint dt_us, count x (varint start_us, varint duration_us)
//!   event    varint scope, varint start_us, varint duration_us, varint parent_code,
//!            u8 depth | kind << 5
//!
//! `parent_code` is 0 for a root and `index - parent` otherwise: 1 for the common "child of
//! the call right before it", small for every real tree. A Repeat record reuses the previous
//! frame's whole structure (scope, parent, depth and kind of every event), so a steady loop
//! costs two varints per call instead of five.
//!
//! At 1000 fps with 14 calls per frame that is ~60 bytes per frame: a capture of a few
//! minutes is a couple of megabytes, where the CSV export of the same run was ~200 MB.
//!
//! Timestamps are microseconds. Resolution is enough for a 1 ms frame (a whole frame is
//! 1000 us) and it halves the size of a nanosecond field; a sub-microsecond call rounds to 0,
//! which is why the aggregate views sum whole frames instead of trusting single calls.

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace tryengine::core {

struct ProfileEvent;

/// `logs/<prefix>-YYYYMMDD-HHMMSS`; creates ./logs on the way. Callers append their own
/// extension, so the two files of one capture share a timestamp.
eastl::string ProfilerTimestampedBase(const eastl::string_view prefix);

/// Newest `logs/*.<extension>` by modification time, or an empty string when there is none.
eastl::string ProfilerLatestLog(const eastl::string_view extension);

/// Append-only binary stream of profiler frames.
class ProfilerLog {
public:
    ProfilerLog() = default;
    ProfilerLog(const ProfilerLog&) = delete;
    ProfilerLog& operator=(const ProfilerLog&) = delete;

    ~ProfilerLog() {
        Stop();
    }

    /// Opens `path` and writes the header. `max_bytes` 0 means no cap; otherwise the stream
    /// stops itself once it reaches the cap and Capped() turns true (the file stays valid:
    /// the cut happens on a frame boundary).
    bool Start(const eastl::string_view path, uint64_t max_bytes);
    void Stop();
    void Flush();

    [[nodiscard]] bool IsActive() const {
        return file_ != nullptr;
    }

    [[nodiscard]] const eastl::string& Path() const {
        return path_;
    }

    [[nodiscard]] uint64_t Bytes() const {
        return bytes_;
    }

    [[nodiscard]] uint32_t Frames() const {
        return frames_;
    }

    [[nodiscard]] bool Capped() const {
        return capped_;
    }

    /// Name table entry. Must be written before the first frame that refers to `id`; the
    /// profiler dumps the table on Start and appends on every new registration.
    void WriteScope(uint32_t id, uint8_t kind, uint8_t depth, const eastl::string_view name);

    /// One closed frame, with its calls in the order they were opened (parents before
    /// children, which is what makes `parent_code` a small number).
    void WriteFrame(float dt_ms, const ProfileEvent* events, size_t count);

private:
    /// Structure of one call: everything a Repeat record can take from the previous frame.
    struct StructureKey {
        uint32_t scope = 0;
        uint32_t parent_code = 0;
        uint8_t depth = 0;
        uint8_t kind = 0;

        bool operator==(const StructureKey&) const = default;
    };

    void WriteByte(uint8_t value);
    void WriteVarint(uint64_t value);
    void WriteRaw(const char* data, size_t size);
    /// Closes the stream when the cap is reached, keeping `capped_` as the reason.
    void CheckCap();

    std::FILE* file_ = nullptr;
    eastl::string path_;
    eastl::vector<char> buffer_;
    uint64_t bytes_ = 0;
    uint64_t max_bytes_ = 0;
    uint32_t frames_ = 0;
    bool capped_ = false;
    /// Structure of the last written frame, and the scratch it is compared against.
    eastl::vector<StructureKey> structure_;
    eastl::vector<StructureKey> scratch_;
    bool have_structure_ = false;
};

/// One status line per second: fps, frame time and the scopes that ate the time. Tiny next to
/// the binary stream, so it is never capped and a long run still has its history.
class ProfilerStatusLog {
public:
    ProfilerStatusLog() = default;
    ProfilerStatusLog(const ProfilerStatusLog&) = delete;
    ProfilerStatusLog& operator=(const ProfilerStatusLog&) = delete;

    ~ProfilerStatusLog() {
        Stop();
    }

    bool Start(const eastl::string_view path);
    void Write(const eastl::string_view line);
    void Stop();

    [[nodiscard]] bool IsActive() const {
        return file_ != nullptr;
    }

    [[nodiscard]] const eastl::string& Path() const {
        return path_;
    }

private:
    std::FILE* file_ = nullptr;
    eastl::string path_;
};

}  // namespace tryengine::core
