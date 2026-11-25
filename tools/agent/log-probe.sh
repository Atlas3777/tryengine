#!/usr/bin/env bash
# Check the engine log without the editor: file sink, ring buffer, filter and Clear.
#
#   tools/agent/log-probe.sh
#
# The probe links the real LogHistory/LogFileSink from engine_core and drives them the way
# the Log panel does, so a passing run means the window has something to show and
# `tail -f logs/engine-*.log` sees the same lines. The log file the probe opens is removed
# on success and left in logs/ on failure. Nothing here is part of the game: it is a dev
# tool built on demand into tools/agent/.tmp.
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/env.sh"

ensure_tmp
need_tool g++

lib=""
for candidate in "$DEBUG_DIR/engine_source/core/libengine_core.a" "$RELEASE_DIR/engine_source/core/libengine_core.a"; do
    if [[ -f "$candidate" ]]; then
        lib="$candidate"
        break
    fi
done
[[ -n "$lib" ]] || die "libengine_core.a not found: run tools/agent/build.sh editor first"

eastl_include="$(echo "$ROOT"/3rdparty/eastl/*/include)"
eabase_include="$(echo "$ROOT"/3rdparty/eabase/*/include/Common)"
src="$ROOT/tools/log_probe.cpp"
bin="$TMP_DIR/log-probe"

if [[ ! -x "$bin" || "$src" -nt "$bin" || "$lib" -nt "$bin" ]]; then
    log "==> building $(rel "$bin")"
    g++ -std=c++23 -O2 -w -DTRY_DEBUG_EASTL_ALLOCATOR=0 \
        -I "$ROOT/engine_source/core/include" -I "$eastl_include" -I "$eabase_include" \
        "$src" "$lib" -o "$bin" -lpthread
fi

exec "$bin"
