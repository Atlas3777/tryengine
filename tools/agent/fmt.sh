#!/usr/bin/env bash
# Check or apply formatting for C++ (clang-format) and daslang (das-fmt).
#
#   tools/agent/fmt.sh                  # check changed files
#   tools/agent/fmt.sh --write          # rewrite changed files in place
#   tools/agent/fmt.sh --check path.cpp # explicit files
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"

usage() {
    cat >&2 <<'EOF'
usage: fmt.sh [--check|--write] [files...]
  --check  report formatting differences (default)
  --write  rewrite the files in place
EOF
}

write=0
files=()
for arg in "$@"; do
    case $arg in
        --write|-i) write=1 ;;
        --check) write=0 ;;
        -h|--help) usage; exit 0 ;;
        -*) usage; exit 2 ;;
        *) files+=("$arg") ;;
    esac
done

if (( ${#files[@]} == 0 )); then
    mapfile -t files < <(changed_files | grep -E '\.(cpp|hpp|h|cc|c|das)$' || true)
fi

if (( ${#files[@]} == 0 )); then
    log "no C++/das files to format"
    exit 0
fi

cpp_files=()
das_files=()
for f in "${files[@]}"; do
    case $f in
        *.cpp|*.hpp|*.h|*.cc|*.c) cpp_files+=("$f") ;;
        *.das) das_files+=("$f") ;;
    esac
done

fail=0

if (( ${#cpp_files[@]} > 0 )); then
    need_tool clang-format
    if (( write )); then
        log "==> clang-format -i (${#cpp_files[@]} file(s))"
        clang-format -i --style=file "${cpp_files[@]}" || fail=1
    else
        log "==> clang-format --check (${#cpp_files[@]} file(s))"
        if ! clang-format --dry-run --Werror --style=file "${cpp_files[@]}"; then
            printf '  %shint:%s fix with: tools/agent/fmt.sh --write\n' "$C_YELLOW" "$C_RESET"
            fail=1
        fi
    fi
fi

if (( ${#das_files[@]} > 0 )); then
    need_file "$DASFMT"
    if (( write )); then
        log "==> das-fmt (${#das_files[@]} file(s))"
    else
        log "==> das-fmt --verify (${#das_files[@]} file(s))"
    fi

    for f in "${das_files[@]}"; do
        abs="$(abs_path "$f")"
        [[ -f "$abs" ]] || { printf '  %sMISS%s %s\n' "$C_YELLOW" "$C_RESET" "$f"; fail=1; continue; }
        if (( write )); then
            "$DASFMT" -- --path "$abs" >/dev/null 2>&1 || { printf '  %sFAIL%s %s\n' "$C_RED" "$C_RESET" "$f"; fail=1; }
        else
            if ! out=$("$DASFMT" -- --path "$abs" --verify 2>&1); then
                printf '  %sFAIL%s %s\n' "$C_RED" "$C_RESET" "$f"
                printf '%s\n' "$out" | grep -E 'Unformatted|Verification failed' | sed 's/^/       /' || true
                fail=1
            fi
        fi
    done
fi

if (( fail )); then
    log "==> format check FAILED"
    exit 1
fi

log "==> format check passed"
