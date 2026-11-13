#!/usr/bin/env bash
# Smoke-check the editor startup: it compiles editor/daslang/EntryPoint.das (which requires
# trygame/Game) and prints compiler errors to the console. Needs a display: a window opens
# for the duration of the run. The CLI gate (das-check.sh) covers the same sources without
# a GUI; use this one when the editor itself is what you changed.
#
#   tools/agent/editor-check.sh [--release]
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"

release=0
for arg in "$@"; do
    case $arg in
        --release) release=1 ;;
        -h|--help) die "usage: editor-check.sh [--release]" ;;
        *) die "unknown option: $arg" ;;
    esac
done

if (( release )); then bin="$RELEASE_DIR/bin/editor"; else bin="$DEBUG_DIR/bin/editor"; fi
[[ -x "$bin" ]] || die "not built: $(rel "$bin") (run tools/agent/build.sh editor)"

if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    log "${C_YELLOW}warning:${C_RESET} no DISPLAY/WAYLAND_DISPLAY; the editor will fail to create a window"
fi

ensure_tmp
logfile="$TMP_DIR/editor-check.log"
timeout_s="${TRY_EDITOR_TIMEOUT:-60}"

log "==> editor ($(rel "$bin")) for ${timeout_s}s; a window will open"
log "    log: $(rel "$logfile")"

set +e
timeout "$timeout_s" "$bin" >"$logfile" 2>&1
status=$?
set -e

# Exit code 124 means the editor was still running when the timeout fired, which is the
# expected outcome for a healthy startup.
if (( status != 0 && status != 124 )); then
    log "${C_YELLOW}warning:${C_RESET} editor exited early with code $status"
fi

if [[ ! -s "$logfile" ]]; then
    log "${C_YELLOW}warning:${C_RESET} the editor produced no output; treat this run as unverified"
    exit 1
fi

errors="$(grep -nE 'error\[|\[Error\]|\[Critical\]|panic\(' "$logfile" || true)"
if [[ -n "$errors" ]]; then
    printf '%s\n' "$errors" | head -40 | sed 's/^/  /' >&2
    log "==> editor reported errors ($(printf '%s\n' "$errors" | wc -l) line(s)); full log: $(rel "$logfile")"
    exit 1
fi

if grep -qE 'Failed to create window|failed to create a (window|device)|SDL_Init' "$logfile"; then
    log "${C_YELLOW}warning:${C_RESET} window/device creation failed; the script compile was not verified"
    exit 1
fi

log "==> editor started without compiler errors"
