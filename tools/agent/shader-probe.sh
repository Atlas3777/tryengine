#!/usr/bin/env bash
# Compile every shader through the real editor ShaderCompilerSystem, without a display.
#
#   tools/agent/shader-probe.sh                       # engine_content/shaders + game/shaders
#   tools/agent/shader-probe.sh path/to/Shader.slang  # one file
#
# The probe links the editor's ShaderCompilerSystem.cpp against engine_core and the bundled
# Slang library, so it checks the same entry-point lookup, module search paths and dependency
# collection the importer uses. Shader modules have no .meta, guid or artifact; this script
# also fails if one appears under a shader_modules directory.
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/env.sh"

ensure_tmp
need_tool g++

libs=()
lib_flags=()
for dir in "$RELEASE_DIR" "$DEBUG_DIR"; do
    core="$dir/engine_source/core/libengine_core.a"
    resources="$dir/engine_source/resources/libengine_resources.a"
    eastl="$dir/_deps/eastl-build/libEASTL.a"
    if [[ -f "$core" && -f "$resources" && -f "$eastl" ]]; then
        libs=("$core" "$resources" "$eastl")
        # core::Error has an extra checked_ member without NDEBUG; the probe TU must use the
        # same layout as the library it links or std::expected misreads the result.
        if [[ "$dir" == "$RELEASE_DIR" ]]; then lib_flags=(-DNDEBUG); fi
        break
    fi
done
[[ ${#libs[@]} -gt 0 ]] || die "engine_core/EASTL not found: run tools/agent/build.sh editor first"

slang_lib="$(find "$ROOT/3rdparty/slang" -maxdepth 3 -name libslang.so | head -1)"
[[ -n "$slang_lib" ]] || die "libslang.so is missing from 3rdparty/slang"
slang_dir="$(dirname "$slang_lib")"
slang_include="$slang_dir/../include"

eastl_include="$(echo "$ROOT"/3rdparty/eastl/*/include)"
eabase_include="$(echo "$ROOT"/3rdparty/eabase/*/include/Common)"

for modules in "$ROOT/engine_content/shader_modules" "$ROOT/game/shader_modules"; do
    [[ -d "$modules" ]] || continue
    junk="$(find "$modules" \( -name '*.meta' -o -name '*.tshd' -o -name '*.slang-module' \) -print)"
    if [[ -n "$junk" ]]; then
        printf 'shader module directories must not contain asset metadata or artifacts:\n%s\n' "$junk" >&2
        exit 1
    fi
done

src="$ROOT/tools/shader_probe.cpp"
src_compiler="$ROOT/editor/src/shader/ShaderCompilerSystem.cpp"
src_material="$ROOT/engine_source/graphics/src/MaterialConfig.cpp"
bin="$TMP_DIR/shader-probe"

if [[ ! -x "$bin" || "$src" -nt "$bin" || "$src_compiler" -nt "$bin" || "$src_material" -nt "$bin" ||
      "${libs[0]}" -nt "$bin" || "$slang_lib" -nt "$bin" ]]; then
    log "==> building $(rel "$bin")"
    g++ -std=c++23 -O0 -w -include cstdint -DTRY_DEBUG_EASTL_ALLOCATOR=0 "${lib_flags[@]}" \
        -I "$ROOT/editor/include" -I "$ROOT/editor/src" \
        -I "$ROOT/engine_source/core/include" -I "$ROOT/engine_source/resources/include" \
        -I "$ROOT/engine_source/async/include" -I "$ROOT/engine_source/graphics/include" \
        -I "$slang_include" -I "$eastl_include" -I "$eabase_include" \
        "$src" "$src_compiler" "$src_material" "${libs[@]}" \
        -L "$slang_dir" -lslang -Wl,-rpath,"$slang_dir" -lpthread -o "$bin"
fi

cd "$ROOT"
if [[ $# -gt 0 ]]; then
    exec "$bin" "$@"
fi

shopt -s nullglob
shaders=(engine_content/shaders/*.slang game/shaders/*.slang)
[[ ${#shaders[@]} -gt 0 ]] || die "no .slang shaders found"
exec "$bin" "${shaders[@]}"
