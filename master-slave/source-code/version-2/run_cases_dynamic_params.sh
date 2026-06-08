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
6 4 rank 5 0.03 1 1
6 4 rank 6 0.03 1 1
6 4 rank 7 0.03 1 1
6 4 rank 8 0.03 1 1
6 4 rank 9 0.03 1 1
EOF