#!/usr/bin/env bash
# Build one or more CMake targets.
#
#   tools/agent/build.sh <target> [--release] [--debug]
#   tools/agent/build.sh modules          # every *_shared daslang binding, always Release
#   tools/agent/build.sh all [--release]  # editor + game_client
#
# Debug is the default configuration. The daslang binding modules are always built in
# Release: both configurations write the same modules/<name>/<name>.shared_module, so a
# Debug build would replace the module that the editor and the daslang CLI load.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"

usage() {
    cat >&2 <<'EOF'
usage: build.sh <target> [--release|--debug]
       build.sh modules
       build.sh all [--release]

targets: editor, game_client, engine_core, engine_graphics, engine_platform,
         engine_resources, engine_async, engine_physics, editor_core, das_modules,
         tryRenderer_shared, tryResources_shared, tryInput_shared,
         tryAsyncFile_shared, tryeditor_shared, tryPhysics_shared
EOF
}

[[ $# -ge 1 ]] || { usage; exit 2; }

target=$1
shift

release=0
for arg in "$@"; do
    case $arg in
        --release) release=1 ;;
        --debug) release=0 ;;
        -h|--help) usage; exit 0 ;;
        *) die "unknown option: $arg" ;;
    esac
done

case $target in
    modules)
        targets=(das_modules)
        release=1
        ;;
    all)
        targets=(editor game_client)
        ;;
    -*)
        usage
        exit 2
        ;;
    *)
        targets=("$target")
        ;;
esac

# Module targets are Release-only regardless of the flag.
for t in "${targets[@]}"; do
    case $t in
        das_modules|*_shared)
            if (( ! release )); then
                log "${C_YELLOW}note:${C_RESET} $t is built in Release only (shared modules are a single output path)"
            fi
            release=1
            ;;
    esac
done

if (( release )); then
    build_dir="$RELEASE_DIR"
    config=Release
else
    build_dir="$DEBUG_DIR"
    config=Debug
fi

[[ -f "$build_dir/CMakeCache.txt" ]] || die "not configured: $build_dir (do not create a new build directory, see AGENTS.md)"
need_tool cmake

for t in "${targets[@]}"; do
    log "==> $t ($config, $(basename "$build_dir"))"
    cmake --build "$build_dir" --target "$t" -j "$JOBS"
done

log "==> done: ${targets[*]} ($config)"
