#!/usr/bin/env bash
# Run clang-tidy over changed translation units, using the Debug compile database.
#
#   tools/agent/tidy.sh                # changed .cpp files
#   tools/agent/tidy.sh path/a.cpp
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"

files=()
for arg in "$@"; do
    case $arg in
        -h|--help) die "usage: tidy.sh [file.cpp ...]" ;;
        -*) die "unknown option: $arg" ;;
        *) files+=("$arg") ;;
    esac
done

if (( ${#files[@]} == 0 )); then
    mapfile -t files < <(changed_files | grep -E '\.cpp$' || true)
fi

if (( ${#files[@]} == 0 )); then
    log "no .cpp files to tidy"
    exit 0
fi

need_tool clang-tidy
db="$DEBUG_DIR/compile_commands.json"
[[ -f "$db" ]] || die "missing $db; build the Debug configuration first"

ensure_tmp
fail=0
for f in "${files[@]}"; do
    abs="$(abs_path "$f")"
    [[ -f "$abs" ]] || { printf '  %sMISS%s %s\n' "$C_YELLOW" "$C_RESET" "$f"; continue; }
    if ! jq -e --arg f "$abs" 'any(.[]; .file == $f)' "$db" >/dev/null; then
        printf '  %sSKIP%s %s (not in the compile database; build the target first)\n' "$C_YELLOW" "$C_RESET" "$f"
        continue
    fi
    log "==> clang-tidy $(rel "$abs")"
    out="$TMP_DIR/tidy-$(printf '%s' "$f" | tr '/' '_').log"
    if clang-tidy -p "$DEBUG_DIR" --quiet "$abs" >"$out" 2>&1; then
        printf '  %sOK%s   %s\n' "$C_GREEN" "$C_RESET" "$f"
    else
        printf '  %sFAIL%s %s\n' "$C_RED" "$C_RESET" "$f"
        head -40 "$out" | sed 's/^/       /'
        fail=1
    fi
done

(( fail == 0 )) || exit 1
log "==> clang-tidy passed"
