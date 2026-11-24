#!/usr/bin/env bash
# Entry point kept for the documented `undo-probe.sh` name: the test itself lives in
# tools/tests/undo_redo.das, and the suite runner owns compilation, isolation and reporting.
set -euo pipefail
exec "$(dirname -- "${BASH_SOURCE[0]}")/test.sh" undo_redo "$@"
