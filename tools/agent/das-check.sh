#!/usr/bin/env bash
# Compile and simulate .das files with the daslang compiler plus the engine bindings.
#
#   tools/agent/das-check.sh              # changed .das files only
#   tools/agent/das-check.sh --all        # every tracked .das file
#   tools/agent/das-check.sh path/to/x.das
#
# The gate uses -dry-run (compile + simulate, no execution), not -compile-only, because
# macro/simulate failures are exactly what breaks the editor at startup. Lint stays on:
# the editor compiles with the lint pass, and `-no-lint` once hid a `type<T>` misuse
# (error 30264) that only the editor reported. Both the exit code
# and the printed `error[` lines are checked: daslang can print errors and still exit 0.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"

usage() {
    cat >&2 <<'EOF'
usage: das-check.sh [--all] [file.das ...]
EOF
}

all=0
files=()
for arg in "$@"; do
    case $arg in
        --all) all=1 ;;
        -h|--help) usage; exit 0 ;;
        -*) usage; exit 2 ;;
        *) files+=("$arg") ;;
    esac
done

need_file "$DASLANG"
ensure_tmp

if (( all )); then
    mapfile -t files < <(git -C "$ROOT" ls-files '*.das')
elif (( ${#files[@]} == 0 )); then
    mapfile -t files < <(changed_files | grep -E '\.das$' || true)
fi

if (( ${#files[@]} == 0 )); then
    log "no .das files to check"
    exit 0
fi

log "==> daslang gate: ${#files[@]} file(s), timeout ${DAS_TIMEOUT}s each"

fail=0
for f in "${files[@]}"; do
    abs="$(abs_path "$f")"
    if [[ ! -f "$abs" ]]; then
        printf '  %sMISS%s %s\n' "$C_YELLOW" "$C_RESET" "$f"
        fail=1
        continue
    fi

    out="$TMP_DIR/das-check-$(printf '%s' "$f" | tr '/' '_').log"
    start=$SECONDS
    if timeout "$DAS_TIMEOUT" "$DASLANG" -dry-run "${DAS_GATE[@]}" "$abs" >"$out" 2>&1 \
        && ! grep -qE 'error\[|panic\(|Simulation failed' "$out"; then
        printf '  %sOK%s   %-50s %ss\n' "$C_GREEN" "$C_RESET" "$f" "$((SECONDS - start))"
    else
        printf '  %sFAIL%s %-50s %ss\n' "$C_RED" "$C_RESET" "$f" "$((SECONDS - start))"
        grep -E 'error\[|panic\(|Simulation failed|missing prerequisite' "$out" | head -20 | sed 's/^/       /' || true
        printf '       full log: %s\n' "$(rel "$out")"
        fail=1
    fi
done

if (( fail )); then
    log "==> daslang gate FAILED"
    exit 1
fi

log "==> daslang gate passed"
