#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cmake --build "${SCRIPT_DIR}/build"

"${SCRIPT_DIR}/build/tabu_search" run \
    "${SCRIPT_DIR}/../../data/200.40.4.txt"