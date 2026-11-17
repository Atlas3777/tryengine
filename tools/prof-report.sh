#!/usr/bin/env bash
# Report on a profiler capture (the *.tryprof files written under logs/).
#
#   tools/prof-report.sh                      # newest capture in logs/
#   tools/prof-report.sh logs/profiler-20261117-221110.tryprof
#
# The report is built from tools/prof_report.cpp and reads the file through the engine's own
# reader, so a capture it prints is a capture the editor's Profiler -> "Load latest" shows.
# Nothing here is part of the game: it is a dev tool, built on demand into tools/agent/.tmp.
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/agent/env.sh"

ensure_tmp
need_tool g++

lib=""
for candidate in "$RELEASE_DIR/engine_source/core/libengine_core.a" "$DEBUG_DIR/engine_source/core/libengine_core.a"; do
    if [[ -f "$candidate" ]]; then
        lib="$candidate"
        break
    fi
done
[[ -n "$lib" ]] || die "libengine_core.a not found: run tools/agent/build.sh editor first"

eastl_include="$(echo "$ROOT"/3rdparty/eastl/*/include)"
eabase_include="$(echo "$ROOT"/3rdparty/eabase/*/include/Common)"
src="$ROOT/tools/prof_report.cpp"
bin="$TMP_DIR/prof-report"

if [[ ! -x "$bin" || "$src" -nt "$bin" || "$lib" -nt "$bin" ]]; then
    log "==> building $(rel "$bin")"
    g++ -std=c++23 -O2 -w -DTRY_DEBUG_EASTL_ALLOCATOR=0 \
        -I "$ROOT/engine_source/core/include" -I "$eastl_include" -I "$eabase_include" \
        "$src" "$lib" -o "$bin" -lpthread
fi

target="${1:-}"
if [[ -z "$target" ]]; then
    target="$(ls -t "$ROOT"/logs/*.tryprof 2>/dev/null | head -1 || true)"
    [[ -n "$target" ]] || die "no capture in logs/: start the game, or tick Capture in the Profiler"
fi
[[ -f "$target" ]] || die "missing: $target"

log "==> $(rel "$target")  ($(du -h "$target" | cut -f1), $(date -r "$target" '+%Y-%m-%d %H:%M'))"
exec "$bin" "$target"
