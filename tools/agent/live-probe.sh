#!/usr/bin/env bash
# Check the `@live` round trip across a real reload without the editor.
#
#   tools/agent/live-probe.sh
#
# Runs a lifecycle script under daslang-live with the SDK's live host, waits
# for the cold start, edits the file to trigger the watcher, and requires the `@live` counter
# to come back with its runtime value instead of the source default. The store and the
# generated `[before_reload]`/`[after_reload]` hooks come from the same live host the editor
# links statically; daslang-live only drives the context swap. The file watcher is the one
# shipped with dasLiveHost (`live/live_watch`), so no editor or GPU is needed.
#
# The probe is headless and runs a temporary script from tools/agent/.tmp.
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/env.sh"

DAS_ROOT="$(dirname "$(dirname "$DASLANG")")"
DASLIVE="${TRY_DASLIVE:-$DAS_ROOT/bin/daslang-live}"
DAS_LIVE_HOST="${TRY_DAS_LIVE_HOST:-$DAS_ROOT/modules/dasLiveHost}"

need_file "$DASLIVE"
need_file "$DAS_LIVE_HOST/.das_module"

ensure_tmp
work="$TMP_DIR/live-probe"
out="$work/out.log"
script="$work/main.das"
port=$((19000 + ($$ % 4000)))

rm -rf "$work"
mkdir -p "$work"

cat >"$script" <<'DAS'
options gen2

require live_host
require live/live_vars
require live/live_watch

var @live counter = 0

[export]
def init() {
    print("[LIVE] init counter={counter}\n")
    if (counter == 0) {
        counter = 4242
    }
}

[export]
def update() {
    counter++
}

[export]
def shutdown() {
    print("[LIVE] shutdown counter={counter}\n")
}
DAS

"$DASLIVE" -project "$DAS_PROJECT" \
    -load_module "$DAS_LIVE_HOST" \
    --live-port "$port" "$script" >"$out" 2>&1 &
pid=$!

cleanup() {
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}
trap cleanup EXIT

# Counts matching lines in the host log. `-F` because the messages contain brackets.
count_lines() {
    grep -acF -- "$1" "$out" 2>/dev/null || true
}

wait_for_count() {
    local pattern=$1 want=$2 timeout=$3
    local start=$SECONDS
    while ((SECONDS - start < timeout)); do
        if (($(count_lines "$pattern") >= want)); then
            return 0
        fi
        if ! kill -0 "$pid" 2>/dev/null; then
            return 1
        fi
        sleep 0.2
    done
    return 1
}

if ! wait_for_count "[LIVE] init counter=" 1 60; then
    cat "$out" >&2
    die "cold start did not reach init()"
fi

# Let update() run, then touch the script: any content change is a reload trigger.
sleep 0.5
printf '\n// live-probe: reload marker\n' >>"$script"

if ! wait_for_count "[LIVE] init counter=" 2 60; then
    cat "$out" >&2
    die "the edited script did not reload"
fi

if grep -aqF -- "error[" "$out"; then
    cat "$out" >&2
    die "the reload printed compiler errors"
fi

restored=$(grep -aF -- "[LIVE] init counter=" "$out" | tail -1 | sed -E 's/.*counter=([0-9]+).*/\1/')
if ! [[ $restored =~ ^[0-9]+$ ]]; then
    cat "$out" >&2
    die "could not read the counter after the reload"
fi
if ((restored < 4242)); then
    cat "$out" >&2
    die "the @live counter did not survive the reload (counter=$restored)"
fi

log "  ${C_GREEN}OK${C_RESET}   live probe: counter preserved across the reload (counter=$restored)"
