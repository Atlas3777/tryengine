#!/usr/bin/env bash
# Headless test of the retained UI (no window, no GPU): id addressing, hit-testing,
# press/click, sound events, var write-back, draw command list.
#
#   tools/agent/ui-smoke.sh
#
# The test source is tools/agent/ui-smoke.das; it panics on the first failed run.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"

ensure_tmp
need_file "$DASLANG"

src="$ROOT/tools/agent/ui-smoke.das"
out="$TMP_DIR/ui-smoke.log"

log "==> ui smoke: $(rel "$src")"
# The exit code is not the signal here: a script that is both a module and an endpoint makes
# daslang exit non-zero even on success. Success is the OK line plus no failed check.
timeout "$DAS_TIMEOUT" "$DASLANG" "${DAS_GATE[@]}" "$src" >"$out" 2>&1 || true
if grep -q 'UI SMOKE OK' "$out" && ! grep -qE 'FAIL:|error\[|Simulation failed|CRASH' "$out"; then
    grep 'UI SMOKE OK' "$out"
    if grep -q 'duplicate widget id' "$out"; then
        printf '   (expected diagnostic: %s)\n' "$(grep 'duplicate widget id' "$out")"
    fi
    exit 0
fi

printf '%s\n' '--- ui smoke output ---'
grep -E 'FAIL:|error\[|panic|Simulation failed' "$out" | head -40 || true
printf 'full log: %s\n' "$(rel "$out")"
exit 1
