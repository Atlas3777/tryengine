#!/usr/bin/env bash
# Check that the environment this repository's agent tooling depends on is usable.
#
#   tools/agent/doctor.sh
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"

fails=0
warns=0
ok() { printf '  %sOK%s   %s\n' "$C_GREEN" "$C_RESET" "$*"; }
warn() { printf '  %sWARN%s %s\n' "$C_YELLOW" "$C_RESET" "$*"; warns=$((warns + 1)); }
bad() { printf '  %sFAIL%s %s\n' "$C_RED" "$C_RESET" "$*"; fails=$((fails + 1)); }
have() { command -v "$1" >/dev/null 2>&1; }

log "==> repository: $ROOT"

log "==> toolchain"
for t in git cmake g++ jq clang-format; do
    if have "$t"; then ok "$t -> $(command -v "$t")"; else bad "$t not found"; fi
done
for t in ccache mold glslangValidator clang-tidy clangd; do
    if have "$t"; then ok "$t -> $(command -v "$t")"; else warn "$t not found (optional)"; fi
done

log "==> build directories"
for dir in "$DEBUG_DIR" "$RELEASE_DIR"; do
    name="$(basename "$dir")"
    if [[ ! -f "$dir/CMakeCache.txt" ]]; then
        bad "$name: not configured"
        continue
    fi
    ok "$name: configured"
    make_program="$(grep -m1 '^CMAKE_MAKE_PROGRAM' "$dir/CMakeCache.txt" | cut -d= -f2- || true)"
    if [[ -n "$make_program" && -x "$make_program" ]]; then
        ok "$name: build program $make_program"
        case $make_program in
            /opt/jetbrains/*) warn "$name: uses CLion's bundled ninja; set the CLion Toolchain ninja to /usr/bin/ninja to decouple" ;;
        esac
    else
        bad "$name: CMAKE_MAKE_PROGRAM is missing or not executable: ${make_program:-<empty>}"
    fi
    if [[ -f "$dir/compile_commands.json" ]]; then
        ok "$name: compile_commands.json present"
    else
        warn "$name: compile_commands.json missing (reconfigure the build directory)"
    fi
done

for bin in "$DEBUG_DIR/bin/editor" "$DEBUG_DIR/bin/game_client"; do
    if [[ -x "$bin" ]]; then ok "binary $(rel "$bin")"; else warn "binary $(rel "$bin") not built"; fi
done

log "==> daslang"
if [[ -x "$DASLANG" ]]; then
    ok "compiler $DASLANG ($("$DASLANG" --version 2>/dev/null | head -1 || true))"
else
    bad "compiler not executable: $DASLANG"
fi
[[ -f "$DAS_PROJECT" ]] && ok "project file $(rel "$DAS_PROJECT")" || bad "missing $(rel "$DAS_PROJECT")"
[[ -f "$DASLLVM/.das_module" ]] && ok "dasLLVM module $DASLLVM" || bad "missing dasLLVM module: $DASLLVM"
[[ -x "$DASFMT" ]] && ok "formatter $DASFMT" || warn "das-fmt not found: $DASFMT"

newest_src="$(
    find "$ROOT/engine_source" "$ROOT/editor" -type f \( -name '*.cpp' -o -name '*.hpp' \) \
        -printf '%T@\n' 2>/dev/null | sort -n | tail -1 || true
)"
newest_src="${newest_src%%.*}"
for mod in tryengine tryeditor; do
    so="$ROOT/modules/$mod/$mod.shared_module"
    if [[ ! -f "$so" ]]; then
        bad "missing modules/$mod/$mod.shared_module (run: tools/agent/build.sh modules)"
        continue
    fi
    so_mtime="$(stat -c %Y "$so" 2>/dev/null || echo 0)"
    if [[ -n "$newest_src" ]] && (( newest_src > so_mtime )); then
        warn "modules/$mod is older than the newest C++ source (rebuild: tools/agent/build.sh modules)"
    else
        ok "modules/$mod binding module is up to date"
    fi
done

log "==> search tooling"
if have sg; then
    sg_version="$( { sg --version 2>&1 || true; } | head -1)"
    case $sg_version in
        *ast-grep*) ok "ast-grep: $sg_version ($(command -v sg))" ;;
        *) warn "sg is not ast-grep (${sg_version:-no output}); install ast-grep 0.45.3 for MCP parse-aware tools" ;;
    esac
else
    warn "ast-grep (sg) not installed: MCP grep_usage/outline/cpp_* are disabled"
fi
if [[ -f "$ROOT/sgconfig.yml" ]]; then ok "sgconfig.yml present"; else warn "sgconfig.yml missing"; fi

log "==> session"
if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
    ok "display available (editor/game smoke checks possible)"
else
    warn "no DISPLAY/WAYLAND_DISPLAY: GUI binaries cannot run here"
fi
branch="$(git -C "$ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?')"
dirty="$(git -C "$ROOT" status --porcelain 2>/dev/null | wc -l)"
if (( dirty == 0 )); then ok "git: $branch, clean tree"; else warn "git: $branch, $dirty uncommitted path(s)"; fi

log ""
if (( fails > 0 )); then
    log "==> $fails failure(s), $warns warning(s)"
    exit 1
fi
log "==> environment OK ($warns warning(s))"
