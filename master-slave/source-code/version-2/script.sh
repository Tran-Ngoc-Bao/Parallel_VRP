#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cmake --build "${SCRIPT_DIR}/build"

mpirun --allow-run-as-root -np 10 \
    "${SCRIPT_DIR}/build/tabu_search" run \
    "${SCRIPT_DIR}/../../../data/200.40.4.txt" \
    --adaptive-iterations 5 \
    --adaptive-pull-elite-segments 4 \
    --elite-pull-strategy pullcount \
    --min-pull-elites-per-worker-factor 6 \
    --elite-pool-factor 0.03 \
    --randomize-worker-hyperparams \
