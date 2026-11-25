#!/usr/bin/env bash
# Check the glTF importer's up-axis conversion and its .meta schema, without launching the editor.
#
#   tools/agent/gltf-probe.sh                                   # every committed model .meta
#   tools/agent/gltf-probe.sh game/assets/lance.glb.meta
#
# The probe builds the real GltfUpAxis.cpp and walks the root rotation every source axis value
# produces, so a Y-up model imported with the default settings lands on its feet in a Z-up engine.
# It also reads each .meta through the importer's own MetaSettingsCodec and requires the writer to
# give the same bytes back, so a committed meta stays stable across imports. Nothing here is part
# of the game: it is a dev tool built on demand into tools/agent/.tmp.
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/env.sh"

ensure_tmp
need_tool g++

libs=()
abi_defs=()
for dir in "$RELEASE_DIR" "$DEBUG_DIR"; do
    resources="$dir/engine_source/resources/libengine_resources.a"
    core="$dir/engine_source/core/libengine_core.a"
    if [[ -f "$resources" && -f "$core" ]]; then
        libs=("$resources" "$core")
        # core::Error carries a debug-only member, so the probe must match the libraries'
        # NDEBUG or the std::expected error object is read through the wrong layout.
        [[ "$dir" == "$RELEASE_DIR" ]] && abi_defs=(-DNDEBUG)
        break
    fi
done
[[ ${#libs[@]} -gt 0 ]] || die "engine_resources/engine_core not found: run tools/agent/build.sh editor first"

eastl_include="$(echo "$ROOT"/3rdparty/eastl/*/include)"
eabase_include="$(echo "$ROOT"/3rdparty/eabase/*/include/Common)"
# The editor imports daScript's vector types, so engine/graphics/Transform.hpp needs the headers
# even though this probe links no daScript symbol.
das_include="${DASLANG%/bin/daslang}/include"
[[ -d "$das_include" ]] || die "daScript headers not found next to $DASLANG"

src="$ROOT/tools/gltf_probe.cpp"
src_axis="$ROOT/editor/src/import/GltfUpAxis.cpp"
bin="$TMP_DIR/gltf-probe"

if [[ ! -x "$bin" || "$src" -nt "$bin" || "$src_axis" -nt "$bin" ]]; then
    log "==> building $(rel "$bin")"
    g++ -std=c++23 -O2 -w -DTRY_DEBUG_EASTL_ALLOCATOR=0 "${abi_defs[@]}" \
        -I "$ROOT/engine_source/core/include" -I "$ROOT/engine_source/resources/include" \
        -I "$ROOT/engine_source/async/include" -I "$ROOT/engine_source/graphics/include" \
        -I "$ROOT/editor/include" \
        -I "$eastl_include" -I "$eabase_include" -isystem "$das_include" \
        "$src" "$src_axis" "${libs[@]}" -o "$bin" -lpthread
fi

if [[ $# -gt 0 ]]; then
    metas=("$@")
else
    mapfile -t metas < <(find game/assets game_client/include -name '*.glb.meta' -o -name '*.gltf.meta' | sort)
    [[ ${#metas[@]} -gt 0 ]] || die "no model .meta found; pass one explicitly"
fi

exec "$bin" "${metas[@]}"
