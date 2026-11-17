//! Analysis report for a profiler capture (the *.tryprof files under logs/).
//!
//! Built and run by tools/prof-report.sh. It reads the file through the engine's own reader
//! (Profiler::ImportLog), so a capture this tool can read is one the editor's "Load latest"
//! can read too - the report doubles as a check of the file.
//!
//! What it answers, in order: how long the run was and how smooth it was (dt percentiles),
//! which scopes hold the time (total and self, i.e. with children subtracted) and which
//! individual frames were the slow ones and what was long inside them.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/core/Profiler.hpp"

using namespace tryengine::core;

namespace {

struct ScopeStat {
    double total_ms = 0.0;
    double self_ms = 0.0;
    uint64_t calls = 0;
    float max_ms = 0.0f;
    std::vector<float> durations;
};

/// Percentile of a sorted vector, 0 for an empty one.
float Percentile(const std::vector<float>& sorted, const double fraction) {
    if (sorted.empty())
        return 0.0f;
    size_t index = static_cast<size_t>(fraction * static_cast<double>(sorted.size()));
    if (index >= sorted.size())
        index = sorted.size() - 1;
    return sorted[index];
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: prof_report <capture.tryprof>\n");
        return 2;
    }

    auto& profiler = Profiler::Instance();
    if (!profiler.ImportLog(argv[1]))
        return 1;

    const uint32_t frames = profiler.FrameCount();
    if (frames == 0) {
        std::fprintf(stderr, "capture has no frames\n");
        return 1;
    }

    std::vector<float> frame_ms;
    frame_ms.reserve(frames);
    uint64_t events = 0;
    double wall_ms = 0.0;
    for (uint32_t f = 0; f < frames; ++f) {
        const float dt = profiler.FrameDeltaMs(f);
        frame_ms.push_back(dt);
        wall_ms += static_cast<double>(dt);
        events += profiler.FrameEventCount(f);
    }

    std::vector<ScopeStat> stats(kProfilerMaxScopes);
    const uint32_t scopes = profiler.ScopeCount();

    // Per call: own duration into the scope, and the same amount subtracted from its parent,
    // so `self_ms` is the time the call spent without its children.
    for (uint32_t f = 0; f < frames; ++f) {
        const uint32_t count = profiler.FrameEventCount(f);
        for (uint32_t e = 0; e < count; ++e) {
            const ProfileEvent& event = *profiler.FrameEvent(f, e);
            if (event.scope >= scopes)
                continue;
            ScopeStat& stat = stats[event.scope];
            stat.total_ms += static_cast<double>(event.duration_ms);
            stat.self_ms += static_cast<double>(event.duration_ms);
            stat.max_ms = std::max(stat.max_ms, event.duration_ms);
            stat.durations.push_back(event.duration_ms);
            ++stat.calls;
            if (event.parent >= 0 && static_cast<uint32_t>(event.parent) < count) {
                const ProfileEvent& parent = *profiler.FrameEvent(f, static_cast<uint32_t>(event.parent));
                if (parent.scope < scopes)
                    stats[parent.scope].self_ms -= static_cast<double>(event.duration_ms);
            }
        }
    }

    std::vector<float> sorted_dt = frame_ms;
    std::sort(sorted_dt.begin(), sorted_dt.end());

    std::printf("== capture ==\n");
    std::printf("frames      %u over %.2f s (%.1f fps average)\n", frames, wall_ms / 1000.0,
                wall_ms > 0.0 ? static_cast<double>(frames) * 1000.0 / wall_ms : 0.0);
    std::printf("calls       %llu, %.2f per frame\n", static_cast<unsigned long long>(events),
                static_cast<double>(events) / static_cast<double>(frames));
    std::printf("dt ms       min %.3f  p50 %.3f  p95 %.3f  p99 %.3f  max %.3f\n", sorted_dt.front(),
                Percentile(sorted_dt, 0.50), Percentile(sorted_dt, 0.95), Percentile(sorted_dt, 0.99),
                sorted_dt.back());
    std::printf("budget      %.1f fps at p50, %.1f at p99, %.1f at max\n", 1000.0 / Percentile(sorted_dt, 0.50),
                1000.0 / std::max(Percentile(sorted_dt, 0.99), 0.001f), 1000.0 / std::max(sorted_dt.back(), 0.001f));

    // Total and self time, busiest first.
    std::vector<uint32_t> order;
    order.reserve(scopes);
    for (uint32_t s = 0; s < scopes; ++s)
        if (stats[s].calls > 0)
            order.push_back(s);
    std::sort(order.begin(), order.end(),
              [&stats](const uint32_t a, const uint32_t b) { return stats[a].total_ms > stats[b].total_ms; });

    const double per_frame_ms = wall_ms / static_cast<double>(frames);
    std::printf("\n== scopes by total time ==\n");
    std::printf("  %-24s %-3s %4s %10s %7s %9s %9s %9s %9s %7s\n", "scope", "knd", "dpth", "calls", "/frame", "total s",
                "avg ms", "p99 ms", "max ms", "share");
    for (const uint32_t s : order) {
        const ScopeStat& stat = stats[s];
        const auto& entry = profiler.Scope(s);
        std::vector<float> durations = stat.durations;
        std::sort(durations.begin(), durations.end());
        std::printf("  %-24s %-3s %4u %10llu %7.2f %9.3f %9.4f %9.3f %9.3f %6.1f%%\n", entry.name.c_str(),
                    entry.kind == ProfileScopeKind::Gpu ? "gpu" : "cpu", entry.depth,
                    static_cast<unsigned long long>(stat.calls),
                    static_cast<double>(stat.calls) / static_cast<double>(frames), stat.total_ms / 1000.0,
                    stat.total_ms / static_cast<double>(stat.calls), Percentile(durations, 0.99), stat.max_ms,
                    100.0 * stat.total_ms / std::max(1.0, wall_ms));
    }

    // Self time: the time a scope spends on its own code, children subtracted. This is where
    // the answer to "who eats the frame" usually is, and it is invisible in a total table.
    std::sort(order.begin(), order.end(),
              [&stats](const uint32_t a, const uint32_t b) { return stats[a].self_ms > stats[b].self_ms; });
    std::printf("\n== scopes by self time (children subtracted) ==\n");
    std::printf("  %-24s %10s %9s %9s %7s %7s\n", "scope", "self s", "avg ms", "max ms", "self%", "of frame");
    for (const uint32_t s : order) {
        const ScopeStat& stat = stats[s];
        const auto& entry = profiler.Scope(s);
        if (stat.self_ms <= 0.0)
            continue;
        std::printf("  %-24s %10.3f %9.4f %9.3f %6.1f%% %6.1f%%\n", entry.name.c_str(), stat.self_ms / 1000.0,
                    stat.self_ms / static_cast<double>(stat.calls), stat.max_ms,
                    100.0 * stat.self_ms / std::max(1.0, stat.total_ms), 100.0 * stat.self_ms / std::max(1.0, wall_ms));
    }

    // Slowest frames, with their longest calls: a hitch is only actionable with its contents.
    std::vector<uint32_t> slow(frames);
    for (uint32_t f = 0; f < frames; ++f)
        slow[f] = f;
    const size_t shown = std::min<size_t>(8, frames);
    std::partial_sort(slow.begin(), slow.begin() + static_cast<ptrdiff_t>(shown), slow.end(),
                      [&frame_ms](const uint32_t a, const uint32_t b) { return frame_ms[a] > frame_ms[b]; });

    std::printf("\n== slowest frames ==\n");
    for (size_t i = 0; i < shown; ++i) {
        const uint32_t f = slow[i];
        const uint32_t count = profiler.FrameEventCount(f);
        // Longest calls of the frame, taken in open order: parent before child.
        std::vector<const ProfileEvent*> calls;
        calls.reserve(count);
        for (uint32_t e = 0; e < count; ++e) {
            const ProfileEvent* event = profiler.FrameEvent(f, e);
            // Only the top-level calls and their direct children: a full dump would be noise.
            if (event->depth <= 2)
                calls.push_back(event);
        }
        std::sort(calls.begin(), calls.end(),
                  [](const ProfileEvent* a, const ProfileEvent* b) { return a->duration_ms > b->duration_ms; });
        std::printf("  frame %-8u %7.3f ms (%.1fx median)  ", f, frame_ms[f],
                    frame_ms[f] / std::max(Percentile(sorted_dt, 0.50), 0.001f));
        for (size_t k = 0; k < std::min<size_t>(3, calls.size()); ++k) {
            const auto& entry = profiler.Scope(calls[k]->scope);
            std::printf("%s %.2f  ", entry.name.c_str(), calls[k]->duration_ms);
        }
        std::printf("\n");
    }
    return 0;
}
