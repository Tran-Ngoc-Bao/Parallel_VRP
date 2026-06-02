#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DEFAULT_DATA_PREFIX="6.5"
PROBLEM_FILE="${1:-${SCRIPT_DIR}/../../../data/soict-2025/${DEFAULT_DATA_PREFIX}.1.txt}"

cmake --build "${SCRIPT_DIR}/build"

mpirun --allow-run-as-root -np 10 \
    "${SCRIPT_DIR}/build/tabu_search" run \
    "${PROBLEM_FILE}" \
    --adaptive-iterations 5 \
    --adaptive-pull-elite-segments 4 \
    --elite-pull-strategy rank \
    --min-pull-elites-per-worker-factor 6 \
    --elite-pool-factor 0.03 \
    --randomize-worker-hyperparams \
