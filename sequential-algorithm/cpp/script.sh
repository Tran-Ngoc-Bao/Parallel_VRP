#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

DEFAULT_DATA_PREFIX="200"
PROBLEM_FILE="${1:-${SCRIPT_DIR}/../../data/soict-2025/${DEFAULT_DATA_PREFIX}.40.2.txt}"

"${BUILD_DIR}/tabu_search" run \
    "${PROBLEM_FILE}" \
    --disable-logging \