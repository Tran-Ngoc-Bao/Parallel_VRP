#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${SCRIPT_DIR}/target/release/sequence-tabu-search"

DEFAULT_DATA_PREFIX="200"
PROBLEM_FILE="${1:-${SCRIPT_DIR}/../../data/soict-2025/${DEFAULT_DATA_PREFIX}.40.2.txt}"

"${BIN}" run \
    "${PROBLEM_FILE}" \
    --disable-logging \
