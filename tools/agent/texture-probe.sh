#!/usr/bin/env bash
# Bake an image into a TextureBinary artifact and validate it, without launching the editor.
#
#   tools/agent/texture-probe.sh                          # synthetic 64x64 PNG, all settings cases
#   tools/agent/texture-probe.sh game/assets/ui/logo.png
#
# The probe compiles the real TextureBaker.cpp + TextureProcessor.cpp (the importer's bake path)
# and reads the result with the runtime's own TextureBinary::Unpack, so a texture that passes here
# imports into a valid artifact. Nothing here is part of the game: it is a dev tool built on
# demand into tools/agent/.tmp.
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/env.sh"

ensure_tmp
need_tool g++

libs=()
abi_defs=()
for dir in "$RELEASE_DIR" "$DEBUG_DIR"; do
    resources="$dir/engine_source/resources/libengine_resources.a"
    core="$dir/engine_source/core/libengine_core.a"
    bc7="$dir/editor/libbc7enc_lib.a"
    if [[ -f "$resources" && -f "$core" && -f "$bc7" ]]; then
        libs=("$resources" "$core" "$bc7")
        # core::Error carries a debug-only member, so the probe must match the libraries'
        # NDEBUG or the std::expected error object is read through the wrong layout.
        [[ "$dir" == "$RELEASE_DIR" ]] && abi_defs=(-DNDEBUG)
        break
    fi
done
[[ ${#libs[@]} -gt 0 ]] || die "engine_resources/engine_core/libbc7enc_lib not found: run tools/agent/build.sh editor first"

eastl_include="$(echo "$ROOT"/3rdparty/eastl/*/include)"
eabase_include="$(echo "$ROOT"/3rdparty/eabase/*/include/Common)"
# 3rdparty may hold more than one cached revision; take the first, like CPM does.
stb_dirs=("$ROOT"/3rdparty/stb/*/)
[[ ${#stb_dirs[@]} -gt 0 && -d "${stb_dirs[0]}" ]] || die "stb sources are missing from 3rdparty/"
stb_include="${stb_dirs[0]}"
resize_dirs=("$ROOT"/3rdparty/stb_image_resize/*/)
[[ ${#resize_dirs[@]} -gt 0 && -d "${resize_dirs[0]}" ]] || die "stb_image_resize is missing from 3rdparty/"
resize_include="${resize_dirs[0]}"
bc_dirs=("$ROOT"/3rdparty/bc7enc/*/)
[[ ${#bc_dirs[@]} -gt 0 && -d "${bc_dirs[0]}" ]] || die "bc7enc is missing from 3rdparty/"
bc_include="${bc_dirs[0]}"
gltf_dirs=("$ROOT"/3rdparty/tinygltf/*/)
[[ ${#gltf_dirs[@]} -gt 0 && -d "${gltf_dirs[0]}" ]] || die "tinygltf is missing from 3rdparty/"
gltf_include="${gltf_dirs[0]}"
glaze_dirs=("$ROOT"/3rdparty/glaze/*/)
[[ ${#glaze_dirs[@]} -gt 0 && -d "${glaze_dirs[0]}include" ]] || die "glaze is missing from 3rdparty/"
glaze_include="${glaze_dirs[0]}include"

src="$ROOT/tools/texture_probe.cpp"
src_baker="$ROOT/editor/src/import/TextureBaker.cpp"
src_processor="$ROOT/editor/src/import/TextureProccessor.cpp"
bin="$TMP_DIR/texture-probe"

if [[ ! -x "$bin" || "$src" -nt "$bin" || "$src_baker" -nt "$bin" || "$src_processor" -nt "$bin" ]]; then
    log "==> building $(rel "$bin")"
    g++ -std=c++23 -O2 -w -fopenmp -include cstdint -DTRY_DEBUG_EASTL_ALLOCATOR=0 "${abi_defs[@]}" \
        -I "$ROOT/engine_source/core/include" -I "$ROOT/engine_source/resources/include" \
        -I "$ROOT/engine_source/async/include" -I "$ROOT/editor/include" \
        -I "$eastl_include" -I "$eabase_include" -I "$stb_include" -I "$resize_include" \
        -I "$bc_include" -I "$gltf_include" -I "$glaze_include" \
        "$src" "$src_baker" "$src_processor" "${libs[@]}" -o "$bin" -lpthread
fi

exec "$bin" "$@"
