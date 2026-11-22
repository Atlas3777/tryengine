#!/usr/bin/env bash
# Bake a .ttf into a FontBinary artifact and validate it, without launching the editor.
#
#   tools/agent/font-probe.sh game/assets/fonts/Main.ttf
#   tools/agent/font-probe.sh game/assets/fonts/Main.ttf --out /tmp/Main.font
#
# The probe compiles the real FontBaker.cpp (the importer's bake path) and reads the result
# with the runtime's own FontBinary::Unpack, so a font that passes here imports into a valid
# artifact. Nothing here is part of the game: it is a dev tool built on demand into
# tools/agent/.tmp.
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/env.sh"

ensure_tmp
need_tool g++

libs=()
for dir in "$RELEASE_DIR" "$DEBUG_DIR"; do
    resources="$dir/engine_source/resources/libengine_resources.a"
    core="$dir/engine_source/core/libengine_core.a"
    if [[ -f "$resources" && -f "$core" ]]; then
        libs=("$resources" "$core")
        break
    fi
done
[[ ${#libs[@]} -gt 0 ]] || die "engine_resources/engine_core static libs not found: run tools/agent/build.sh editor first"

eastl_include="$(echo "$ROOT"/3rdparty/eastl/*/include)"
eabase_include="$(echo "$ROOT"/3rdparty/eabase/*/include/Common)"
# 3rdparty may hold more than one cached stb revision; take the first, like CPM does.
stb_dirs=("$ROOT"/3rdparty/stb/*/)
[[ ${#stb_dirs[@]} -gt 0 && -d "${stb_dirs[0]}" ]] || die "stb sources are missing from 3rdparty/"
stb_include="${stb_dirs[0]}"

src="$ROOT/tools/font_probe.cpp"
src_baker="$ROOT/editor/src/import/FontBaker.cpp"
bin="$TMP_DIR/font-probe"

if [[ ! -x "$bin" || "$src" -nt "$bin" || "$src_baker" -nt "$bin" ]]; then
    log "==> building $(rel "$bin")"
    g++ -std=c++23 -O2 -w -DTRY_DEBUG_EASTL_ALLOCATOR=0 \
        -I "$ROOT/engine_source/core/include" -I "$ROOT/engine_source/resources/include" \
        -I "$ROOT/engine_source/async/include" -I "$ROOT/editor/include" \
        -I "$eastl_include" -I "$eabase_include" -I "$stb_include" \
        "$src" "$src_baker" "${libs[@]}" -o "$bin" -lpthread
fi

[[ $# -ge 1 ]] || die "usage: font-probe.sh <font.ttf> [--out <path>]"

exec "$bin" "$@"
