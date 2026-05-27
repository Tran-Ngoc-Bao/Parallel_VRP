#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cmake --build "${SCRIPT_DIR}/build"

mpirun --allow-run-as-root -np 8 \
    "${SCRIPT_DIR}/build/tabu_search" run \
    "${SCRIPT_DIR}/../../../data/200.40.4.txt" \
    --adaptive-iterations 5 \
    --adaptive-pull-elite-segments 4 \
    --elite-pull-strategy topk \
    --min-pull-elites-per-worker-factor 1.3 \
    # --randomize-worker-hyperparams \
    # --randomize-worker-adaptive-hyperparams \