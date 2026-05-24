#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mpirun --allow-run-as-root -np 8 \
    "${SCRIPT_DIR}/build/tabu_search" run \
    "${SCRIPT_DIR}/../../../data/200.10.1.txt" \
    --adaptive-iterations 5 \
    --adaptive-pull-elite-segments 3 \
    --adaptive-pull-elite-limit 5 \
    --elite-pull-strategy topk \
    --diversity-weight-edge 0.9 \
    --diversity-weight-assignment 0.1