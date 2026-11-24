#!/usr/bin/env bash
# Headless behaviour suite for the engine's daslang layer.
#
#   tools/agent/test.sh                 # whole suite, one process per test
#   tools/agent/test.sh prefab          # tests whose name matches the regex
#   tools/agent/test.sh --list          # names and expected outcomes
#   tools/agent/test.sh -j 1 -v         # serial, with each test's output
#
# A test is a standalone .das program in tools/tests/. It runs with the same compiler gate
# as the editor (project paths, native binding modules, LLVM) and reports failures through
# its exit code. The suite only checks headless behaviour: no window, no GPU, no assets.
#
# Expected outcome comes from the first `//! expect:` line of the test:
#   pass           exit 0 and no error/panic/FAIL in the output (default)
#   panic          the program aborts with `panic(`
#   exception      the program aborts with a script EXCEPTION
#   compile-error  the compiler prints `error[` (a negative test of a declaration)
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"

TEST_DIR="$ROOT/tools/tests"
TEST_TMP="$TMP_DIR/tests"
TEST_TIMEOUT_DEFAULT=120

usage() {
    cat >&2 <<'EOF'
usage: test.sh [options] [name-regex ...]
  -l, --list           list the selected tests and their expected outcome
  -j, --jobs N         run N tests at a time (default: $TRY_JOBS or nproc, at most 8)
  -t, --timeout SEC    per-test timeout (default: $TRY_TEST_TIMEOUT or 120)
  -v, --verbose        run serially and print every test's output
  -h, --help           this help
EOF
}

jobs=$JOBS
timeout_s=${TRY_TEST_TIMEOUT:-$TEST_TIMEOUT_DEFAULT}
list=0
verbose=0
filters=()

while (( $# > 0 )); do
    case $1 in
        -l|--list) list=1; shift ;;
        -j|--jobs) jobs=${2:?--jobs needs a value}; shift 2 ;;
        --jobs=*) jobs=${1#*=}; shift ;;
        -t|--timeout) timeout_s=${2:?--timeout needs a value}; shift 2 ;;
        --timeout=*) timeout_s=${1#*=}; shift ;;
        -v|--verbose) verbose=1; shift ;;
        -h|--help) usage; exit 0 ;;
        -*) usage; exit 2 ;;
        *) filters+=("$1"); shift ;;
    esac
done

[[ $jobs =~ ^[1-9][0-9]*$ ]] || die "--jobs must be a positive number, got '$jobs'"
[[ $timeout_s =~ ^[1-9][0-9]*$ ]] || die "--timeout must be a positive number, got '$timeout_s'"
(( verbose )) && jobs=1

need_file "$DASLANG"
[[ -d "$TEST_DIR" ]] || die "missing test directory: $(rel "$TEST_DIR")"

shopt -s nullglob
all_files=("$TEST_DIR"/*.das)
shopt -u nullglob
(( ${#all_files[@]} > 0 )) || die "no tests in $(rel "$TEST_DIR")"

filter_re=""
if (( ${#filters[@]} > 0 )); then
    filter_re=$(IFS='|'; printf '%s' "${filters[*]}")
fi

files=()
names=()
expects=()
for file in "${all_files[@]}"; do
    name=$(basename "$file" .das)
    if [[ -n $filter_re ]] && ! [[ $name =~ $filter_re ]]; then
        continue
    fi
    expect=$(sed -nE 's@^//! *expect: *([a-z-]+).*@\1@p' "$file" | head -n 1)
    expect=${expect:-pass}
    case $expect in
        pass|panic|exception|compile-error) ;;
        *) die "$(rel "$file"): unknown expect '$expect' (want pass, panic, exception or compile-error)" ;;
    esac
    files+=("$file")
    names+=("$name")
    expects+=("$expect")
done

(( ${#files[@]} > 0 )) || die "no test matches '${filters[*]}'"

if (( list )); then
    for i in "${!names[@]}"; do
        printf '%-36s %s\n' "${names[$i]}" "${expects[$i]}"
    done
    exit 0
fi

rm -rf "$TEST_TMP"
mkdir -p "$TEST_TMP"

# One test in its own process: the engine's daslang modules keep state (DECS, AssetCache,
# Time), so isolation is the point, not an optimisation.
run_one() {
    local name=$1 file=$2 expect=$3
    local log="$TEST_TMP/$name.log"
    local -a cmd=("$DASLANG")
    if [[ $expect == compile-error ]]; then
        cmd+=(-dry-run)
    fi
    cmd+=("${DAS_GATE[@]}" "$file")

    local start=$SECONDS
    local code=0
    timeout "$timeout_s" "${cmd[@]}" >"$log" 2>&1 || code=$?
    local verdict=FAIL

    case $expect in
        pass)
            if (( code == 0 )) && ! grep -qE 'error\[|panic\(|Simulation failed|EXCEPTION:|missing prerequisite|(^|[[:space:]])FAIL([: ]|$)' "$log"; then
                verdict=OK
            fi
            ;;
        panic)
            if (( code != 0 )) && grep -qE 'panic\(' "$log"; then
                verdict=OK
            fi
            ;;
        exception)
            if (( code != 0 )) && grep -qE 'EXCEPTION:' "$log"; then
                verdict=OK
            fi
            ;;
        compile-error)
            if grep -qE 'error\[' "$log" && ! grep -q 'missing prerequisite' "$log"; then
                verdict=OK
            fi
            ;;
    esac

    printf '%s %s %s\n' "$verdict" "$((SECONDS - start))" "$code" >"$TEST_TMP/$name.status"
    if (( verbose )); then
        sed 's/^/    /' "$log"
    fi
    return 0
}

log "==> tests: ${#files[@]} file(s), jobs $jobs, timeout ${timeout_s}s"

running=0
for i in "${!files[@]}"; do
    run_one "${names[$i]}" "${files[$i]}" "${expects[$i]}" &
    running=$((running + 1))
    if (( running >= jobs )); then
        wait -n || true
        running=$((running - 1))
    fi
done
wait

passed=0
failed=0
for i in "${!names[@]}"; do
    name=${names[$i]}
    status="$TEST_TMP/$name.status"
    log_file="$TEST_TMP/$name.log"
    if [[ ! -f "$status" ]]; then
        printf '  %sFAIL%s %-34s no status (crashed before the test ran)\n' "$C_RED" "$C_RESET" "$name"
        failed=$((failed + 1))
        continue
    fi
    read -r verdict seconds code <"$status"
    if [[ $verdict == OK ]]; then
        printf '  %sOK%s   %-34s %ss\n' "$C_GREEN" "$C_RESET" "$name" "$seconds"
        passed=$((passed + 1))
    else
        printf '  %sFAIL%s %-34s %ss (exit %s, expect %s)\n' \
            "$C_RED" "$C_RESET" "$name" "$seconds" "$code" "${expects[$i]}"
        grep -aE 'error\[|panic\(|EXCEPTION:|missing prerequisite|Simulation failed|(^|[[:space:]])FAIL([: ]|$)' \
            "$log_file" 2>/dev/null | head -8 | sed 's/^/       /' || true
        printf '       full log: %s\n' "$(rel "$log_file")"
        failed=$((failed + 1))
    fi
done

if (( failed > 0 )); then
    log "==> tests FAILED: $passed passed, $failed failed"
    exit 1
fi
log "==> tests passed: $passed file(s)"
