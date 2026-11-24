#!/usr/bin/env bash
# Undo/redo probe: runs tools/undo_probe.das against a headless DECS world.
#
#   tools/agent/undo-probe.sh
#
# The probe asserts the parts of tryeditor/UndoRedo that a GUI cannot check cheaply: the
# component snapshot round trip, undo of an added/removed component, deleting a subtree and
# getting it back under the same parent with a *new* entity id, an older history entry still
# reaching the re-spawned entity, copy/paste and drag coalescing. Its entities carry no model,
# so no asset or GPU state is needed; the run is a normal daslang program (not -dry-run).
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"

need_file "$DASLANG"
need_file "$ROOT/tools/undo_probe.das"
ensure_tmp

out="$TMP_DIR/undo-probe.log"
log "==> undo probe"

# The probe's main() returns the number of failed checks as the process exit code, so a
# compile error, a panic and a failed assertion all end up here the same way.
if timeout "$DAS_TIMEOUT" "$DASLANG" "${DAS_GATE[@]}" tools/undo_probe.das >"$out" 2>&1 \
    && ! grep -qE 'error\[|panic\(|Simulation failed' "$out"; then
    printf '  %sOK%s   %s\n' "$C_GREEN" "$C_RESET" "$(grep -c '  ok ' "$out") checks"
else
    printf '  %sFAIL%s\n' "$C_RED" "$C_RESET"
    grep -E 'error\[|panic\(|Simulation failed|  FAIL|-- failures' "$out" | head -20 | sed 's/^/       /' || true
    printf '       full log: %s\n' "$(rel "$out")"
    exit 1
fi
