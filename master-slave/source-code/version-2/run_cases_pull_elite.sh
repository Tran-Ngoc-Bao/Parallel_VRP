#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCHMARK_RUN="${SCRIPT_DIR}/benchmark_runs.sh"

while read -r ai seg strat minpull pool rand prefer_pulled; do
  ADAPTIVE_ITERATIONS="$ai" \
  ADAPTIVE_PULL_ELITE_SEGMENTS="$seg" \
  ELITE_PULL_STRATEGY="$strat" \
  MIN_PULL_ELITES_PER_WORKER_FACTOR="$minpull" \
  ELITE_POOL_FACTOR="$pool" \
  RANDOMIZE_WORKER_HYPERPARAMS="$rand" \
  PREFER_PULLED="$prefer_pulled" \
  bash "${BENCHMARK_RUN}"
done <<'EOF'
6 4 random 5 0.03 0 1
6 4 random 6 0.03 0 1
6 4 random 7 0.03 0 1
6 4 random 8 0.03 0 1
6 4 random 9 0.03 0 1
6 4 topk 5 0.03 0 1
6 4 topk 6 0.03 0 1
6 4 topk 7 0.03 0 1
6 4 topk 8 0.03 0 1
6 4 topk 9 0.03 0 1
6 4 rank 5 0.03 0 1
6 4 rank 6 0.03 0 1
6 4 rank 7 0.03 0 1
6 4 rank 8 0.03 0 1
6 4 rank 9 0.03 0 1
6 4 pullcount 5 0.03 0 1
6 4 pullcount 6 0.03 0 1
6 4 pullcount 7 0.03 0 1
6 4 pullcount 8 0.03 0 1
6 4 pullcount 9 0.03 0 1
EOF