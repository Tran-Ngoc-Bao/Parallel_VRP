#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cmake --build "${SCRIPT_DIR}/build"

mpirun --allow-run-as-root -np 8 \
    "${SCRIPT_DIR}/build/master_slave_v1" run \
    "${SCRIPT_DIR}/../../../data/200.40.4.txt" \
    --elite-pull-strategy random \
    --diversity-weight-edge 1.0 \
    --diversity-weight-assignment 0.0 \
    --adaptive-iterations 5