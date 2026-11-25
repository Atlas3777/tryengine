#!/usr/bin/env bash
# Check the script generation swap headlessly: @live state, reload hooks and clocks.
#
#   tools/agent/script-reload-probe.sh
#
# Builds a small host around the real ScriptSystem from engine_core, loads a probe script,
# advances the root clock, sets @live values and calls the engine's own hot reload
# (ReloadMainScript). The reload runs the registered [before_reload] hooks, compiles a new
# context from disk and runs [after_reload] there, so a pass means the @live store, the hook
# discovery and the time restore all work without the editor and without a display.
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/env.sh"

need_tool g++
DAS_ROOT="$(dirname "$(dirname "$DASLANG")")"

libs=()
abi_defs=()
for dir in "$RELEASE_DIR" "$DEBUG_DIR"; do
    core="$dir/engine_source/core/libengine_core.a"
    eastl="$dir/_deps/eastl-build/libEASTL.a"
    if [[ -f "$core" && -f "$eastl" ]]; then
        libs=("$core" "$eastl")
        # core::Error carries a debug-only member, so the probe must match the libraries'
        # NDEBUG or the std::expected error object is read through the wrong layout.
        [[ "$dir" == "$RELEASE_DIR" ]] && abi_defs=(-DNDEBUG)
        break
    fi
done
[[ ${#libs[@]} -gt 0 ]] || die "libengine_core.a/libEASTL.a not found: run tools/agent/build.sh editor first"

eastl_include="$(echo "$ROOT"/3rdparty/eastl/*/include)"
eabase_include="$(echo "$ROOT"/3rdparty/eabase/*/include/Common)"
src="$ROOT/tools/script_reload_probe.cpp"
bin="$TMP_DIR/script-reload-probe"

ensure_tmp
if [[ ! -x "$bin" || "$src" -nt "$bin" || "${libs[0]}" -nt "$bin" ]]; then
    log "==> building $(rel "$bin")"
    g++ -std=c++23 -O2 -w -DTRY_DEBUG_EASTL_ALLOCATOR=0 "${abi_defs[@]}" \
        -DDAS_ROOT_DIR="\"$DAS_ROOT\"" \
        -I "$ROOT/engine_source/core/include" \
        -I "$eastl_include" -I "$eabase_include" -I "$DAS_ROOT/include" \
        "$src" "${libs[@]}" \
        -L"$DAS_ROOT/lib" -llibDaScriptDyn -llibDaScriptDyn_runtime -Wl,-rpath,"$DAS_ROOT/lib" \
        -o "$bin" -lpthread -ldl
fi

script="$TMP_DIR/script-reload-probe.das"
cat >"$script" <<'DAS'
options gen2
options relaxed_assign = false

require tryengine/Live
require tryengine/Time

var @live counter = 0
var @live title = "cold"

var g_ok = false

// The annotations are registered by tryLive, not by the editor, so a plain host like this
// probe can use them too. Each hook writes a marker into the live store; ProbeCheck reads
// both marks after the swap.
[before_reload]
def ProbeBeforeHook() {
    var mark <- [uint8(5)]
    live_save("probe_before_hook", mark)
}

[after_reload]
def ProbeAfterHook() {
    var mark <- [uint8(7)]
    live_save("probe_after_hook", mark)
}

[export]
def ProbeSetup() {
    TimeInit()
}

[export]
def ProbeTick() {
    TimeTick()
}

[export]
def ProbeSet() {
    counter = 12345
    title = "hot"
}

[export]
def ProbeCheck() {
    let total = TimeTotal(root_clock)
    let reload = is_reload()
    var before_mark : array<uint8>
    var after_mark : array<uint8>
    let before_ok = live_load("probe_before_hook", before_mark) && length(before_mark) == 1 && before_mark[0] == uint8(5)
    let after_ok = live_load("probe_after_hook", after_mark) && length(after_mark) == 1 && after_mark[0] == uint8(7)
    g_ok = counter == 12345 && title == "hot" && reload && total > 0.2d && before_ok && after_ok
    print("[PROBE] counter={counter} title={title} is_reload={reload} total={total:0.3f} before_hook={before_ok} after_hook={after_ok}\n")
}

[export]
def ProbeOk() : bool {
    return g_ok
}
DAS

exec "$bin" "$script"
