#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cmake --build "${SCRIPT_DIR}/build"

mpirun --allow-run-as-root -np 8 \
    "${SCRIPT_DIR}/build/tabu_search" run \
    "${SCRIPT_DIR}/../../../data/100.30.4.txt" \
    --adaptive-iterations 5 \
    --adaptive-pull-elite-segments 3 \
    --elite-pull-strategy random \
    --diversity-weight-edge 1.0 \
    --diversity-weight-assignment 0.0 \
    --min-pull-elites-per-worker 5 \
    --randomize-worker-hyperparams \
    # --randomize-worker-adaptive-hyperparams