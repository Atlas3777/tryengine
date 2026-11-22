#!/usr/bin/env bash
# Shared environment for the tools in this directory. Source it, do not execute it.
#   source "$(dirname "$0")/env.sh"
set -euo pipefail

AGENT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$AGENT_DIR/../.." && pwd)"

DEBUG_DIR="${TRY_DEBUG_DIR:-$ROOT/build-debug}"
RELEASE_DIR="${TRY_RELEASE_DIR:-$ROOT/build-release}"
default_jobs="$(nproc 2>/dev/null || echo 4)"
(( default_jobs > 8 )) && default_jobs=8
JOBS="${TRY_JOBS:-$default_jobs}"

# The agent sandbox may not write outside the workspace, while CLion's ccache lives in
# ~/.cache/ccache. Point ccache at a workspace-local directory so builds work under the
# sandbox; the human's CLion keeps using its own cache.
export CCACHE_DIR="${TRY_CCACHE_DIR:-$ROOT/.cache/ccache}"
export CCACHE_MAXSIZE="${TRY_CCACHE_MAXSIZE:-2G}"

DASLANG="${TRY_DASLANG:-/opt/daslang/bin/daslang}"
DASFMT="${TRY_DASFMT:-/opt/daslang/bin/das-fmt.exe}"
DASLLVM="${TRY_DASLLVM:-/opt/daslang/modules/dasLLVM}"
DAS_PROJECT="$ROOT/project.das_project"
DAS_TIMEOUT="${TRY_DAS_TIMEOUT:-300}"

TMP_DIR="$ROOT/tools/agent/.tmp"

if [[ -t 1 ]]; then
    C_RED=$'\e[31m' C_GREEN=$'\e[32m' C_YELLOW=$'\e[33m' C_DIM=$'\e[2m' C_RESET=$'\e[0m'
else
    C_RED='' C_GREEN='' C_YELLOW='' C_DIM='' C_RESET=''
fi

log() { printf '%s\n' "$*" >&2; }
die() { printf '%serror:%s %s\n' "$C_RED" "$C_RESET" "$*" >&2; exit 1; }

need_file() { [[ -e "$1" ]] || die "missing: $1"; }
need_tool() { command -v "$1" >/dev/null 2>&1 || die "missing tool: $1"; }

# Human-readable relative path for messages.
rel() {
    local p=$1
    printf '%s\n' "${p#"$ROOT"/}"
}

# Merge base with main when it exists; HEAD otherwise. On main this is HEAD, so the
# diff shows uncommitted work only.
base_ref() {
    if git -C "$ROOT" rev-parse --verify --quiet main >/dev/null 2>&1; then
        git -C "$ROOT" merge-base HEAD main 2>/dev/null || git -C "$ROOT" rev-parse HEAD
    else
        git -C "$ROOT" rev-parse HEAD
    fi
}

# Changed (tracked, modified/added/renamed) plus untracked files, repo-relative.
changed_files() {
    local base
    base="$(base_ref)"
    {
        git -C "$ROOT" diff --name-only --diff-filter=ACMR "$base"
        git -C "$ROOT" ls-files --others --exclude-standard
    } | sort -u
}

# Absolute path from a repo-relative one.
abs_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s\n' "$ROOT/$1" ;;
    esac
}

# daslang gate flags shared by every .das check.
# -project resolves the module paths, -load_module registers the native binding modules
# and the LLVM emitter that daslib/just_in_time needs at simulate time. Each native module
# lives in its own folder modules/<name>/ with a .das_module descriptor.
DAS_MODULES=(tryRenderer tryResources tryInput tryAsyncFile tryeditor tryPhysics tryUi)
DAS_GATE=(-project "$DAS_PROJECT")
for _das_module in "${DAS_MODULES[@]}"; do
    DAS_GATE+=(-load_module "$ROOT/modules/$_das_module")
done
DAS_GATE+=(-load_module "$DASLLVM")
unset _das_module

ensure_tmp() { mkdir -p "$TMP_DIR"; }

cd "$ROOT"
