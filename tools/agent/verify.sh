#!/usr/bin/env bash
# Verify the current change set: formatting, C++ syntax, daslang compile+simulate, and the
# build of the affected targets.
#
#   tools/agent/verify.sh            # full check
#   tools/agent/verify.sh --fast     # skip builds
#   tools/agent/verify.sh --tidy     # also run clang-tidy on changed translation units
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"

usage() {
    cat >&2 <<'EOF'
usage: verify.sh [--fast] [--tidy]
  --fast  skip the build steps
  --tidy  additionally run clang-tidy on changed .cpp files
EOF
}

fast=0
tidy=0
for arg in "$@"; do
    case $arg in
        --fast) fast=1 ;;
        --tidy) tidy=1 ;;
        -h|--help) usage; exit 0 ;;
        *) usage; exit 2 ;;
    esac
done

ensure_tmp

base="$(base_ref)"
mapfile -t changed < <(changed_files)

if (( ${#changed[@]} == 0 )); then
    log "==> no changes relative to $(git -C "$ROOT" rev-parse --short "$base")"
    exit 0
fi

cpp=()
das=()
need_editor=0
need_game=0
need_modules=0

for f in "${changed[@]}"; do
    case $f in
        *.cpp|*.hpp|*.h|*.cc) cpp+=("$f") ;;
    esac
    case $f in
        *.das) das+=("$f") ;;
    esac
    case $f in
        engine_source/*|editor/*|CMakeLists.txt|*/CMakeLists.txt) need_editor=1 ;;
    esac
    case $f in
        game_client/*) need_game=1 ;;
    esac
    case $f in
        */Module_*.cpp|*ScriptSetup*.hpp|*ScriptSetup*.cpp) need_modules=1 ;;
    esac
done

overall=0
declare -a summary=()

record() { summary+=("$1|$2"); }

log "==> $(git -C "$ROOT" rev-parse --short "$base")..worktree: ${#changed[@]} changed path(s)"
log "    C++: ${#cpp[@]}, das: ${#das[@]}, rebuild modules: $need_modules, editor: $need_editor, game_client: $need_game"

# --- 1. formatting -----------------------------------------------------------------------
if (( ${#cpp[@]} + ${#das[@]} > 0 )); then
    if "$AGENT_DIR/fmt.sh" --check "${cpp[@]}" "${das[@]}"; then
        record "format" OK
    else
        record "format" FAIL
        overall=1
    fi
fi

# --- 2. C++ syntax -----------------------------------------------------------------------
check_cpp_syntax() {
    local db="$DEBUG_DIR/compile_commands.json"
    if [[ ! -f "$db" ]]; then
        log "  ${C_YELLOW}SKIP${C_RESET} C++ syntax: missing $db"
        return 0
    fi
    local fail=0
    local f abs dir cmd out
    for f in "$@"; do
        case $f in *.cpp) ;; *) continue ;; esac
        abs="$(abs_path "$f")"
        if [[ ! -f "$abs" ]]; then
            printf '  %sMISS%s %s\n' "$C_YELLOW" "$C_RESET" "$f"
            continue
        fi
        dir="$(jq -r --arg f "$abs" '.[] | select(.file == $f) | .directory' "$db" | head -1)"
        cmd="$(jq -r --arg f "$abs" '.[] | select(.file == $f) | .command' "$db" | head -1)"
        if [[ -z "$cmd" || "$cmd" == "null" ]]; then
            printf '  %sSKIP%s %s (not in the compile database; build the target first)\n' \
                "$C_YELLOW" "$C_RESET" "$f"
            continue
        fi
        cmd="$(printf '%s' "$cmd" | sed -E 's/ -o [^ ]+/ /g; s/ -c / /g')"
        out="$TMP_DIR/syntax-$(printf '%s' "$f" | tr '/' '_').log"
        if (cd "$dir" && eval "$cmd -fsyntax-only -fdiagnostics-color=never") >"$out" 2>&1; then
            printf '  %sOK%s   %s\n' "$C_GREEN" "$C_RESET" "$f"
        else
            printf '  %sFAIL%s %s\n' "$C_RED" "$C_RESET" "$f"
            grep -E 'error:' "$out" | head -20 | sed 's/^/       /' || true
            printf '       full log: %s\n' "$(rel "$out")"
            fail=1
        fi
    done
    return "$fail"
}

if (( ${#cpp[@]} > 0 )); then
    log "==> C++ syntax check"
    if check_cpp_syntax "${cpp[@]}"; then
        record "cpp-syntax" OK
    else
        record "cpp-syntax" FAIL
        overall=1
    fi
fi

# --- 3. clang-tidy (opt-in) ---------------------------------------------------------------
if (( tidy && ${#cpp[@]} > 0 )); then
    if "$AGENT_DIR/tidy.sh" "${cpp[@]}"; then
        record "clang-tidy" OK
    else
        record "clang-tidy" FAIL
        overall=1
    fi
fi

# --- 4. daslang gate ----------------------------------------------------------------------
if (( ${#das[@]} > 0 )); then
    if "$AGENT_DIR/das-check.sh" "${das[@]}"; then
        record "daslang" OK
    else
        record "daslang" FAIL
        overall=1
    fi
fi

# --- 5. daslang behaviour suite -----------------------------------------------------------
# Every test is a standalone program; the suite isolates the global state (DECS, Time,
# AssetCache) that makes in-process tests order-dependent. It needs the native binding
# modules built, like the daslang gate above.
if "$AGENT_DIR/test.sh"; then
    record "tests" OK
else
    record "tests" FAIL
    overall=1
fi

# --- 6. builds ----------------------------------------------------------------------------
if (( fast )); then
    log "==> builds skipped (--fast)"
else
    if (( need_modules )); then
        if "$AGENT_DIR/build.sh" modules; then
            record "build modules" OK
        else
            record "build modules" FAIL
            overall=1
        fi
    fi
    if (( need_editor )); then
        if "$AGENT_DIR/build.sh" editor; then
            record "build editor" OK
        else
            record "build editor" FAIL
            overall=1
        fi
    fi
    if (( need_game )); then
        if "$AGENT_DIR/build.sh" game_client; then
            record "build game_client" OK
        else
            record "build game_client" FAIL
            overall=1
        fi
    fi
fi

# --- summary ------------------------------------------------------------------------------
log ""
log "==> summary"
for line in "${summary[@]}"; do
    name="${line%%|*}"
    status="${line#*|}"
    if [[ "$status" == OK ]]; then
        printf '  %sOK%s   %s\n' "$C_GREEN" "$C_RESET" "$name"
    else
        printf '  %sFAIL%s %s\n' "$C_RED" "$C_RESET" "$name"
    fi
done

if (( overall )); then
    log "==> verify FAILED"
    exit 1
fi
log "==> verify passed"
