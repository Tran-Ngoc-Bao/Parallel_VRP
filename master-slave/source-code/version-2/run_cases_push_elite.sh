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
4 3 random 6 0.02 0 0
4 3 random 6 0.03 0 0
4 3 random 6 0.04 0 0
4 4 random 6 0.02 0 0
4 4 random 6 0.03 0 0
4 4 random 6 0.04 0 0
4 5 random 6 0.02 0 0
4 5 random 6 0.03 0 0
4 5 random 6 0.04 0 0
6 3 random 6 0.02 0 0
6 3 random 6 0.03 0 0
6 3 random 6 0.04 0 0
6 4 random 6 0.02 0 0
6 4 random 6 0.03 0 0
6 4 random 6 0.04 0 0
6 5 random 6 0.02 0 0
6 5 random 6 0.03 0 0
6 5 random 6 0.04 0 0
8 3 random 6 0.02 0 0
8 3 random 6 0.03 0 0
8 3 random 6 0.04 0 0
8 4 random 6 0.02 0 0
8 4 random 6 0.03 0 0
8 4 random 6 0.04 0 0
8 5 random 6 0.02 0 0
8 5 random 6 0.03 0 0
8 5 random 6 0.04 0 0
4 3 random 6 0.02 0 1
4 3 random 6 0.03 0 1
4 3 random 6 0.04 0 1
4 4 random 6 0.02 0 1
4 4 random 6 0.03 0 1
4 4 random 6 0.04 0 1
4 5 random 6 0.02 0 1
4 5 random 6 0.03 0 1
4 5 random 6 0.04 0 1
6 3 random 6 0.02 0 1
6 3 random 6 0.03 0 1
6 3 random 6 0.04 0 1
6 4 random 6 0.02 0 1
6 4 random 6 0.03 0 1
6 4 random 6 0.04 0 1
6 5 random 6 0.02 0 1
6 5 random 6 0.03 0 1
6 5 random 6 0.04 0 1
8 3 random 6 0.02 0 1
8 3 random 6 0.03 0 1
8 3 random 6 0.04 0 1
8 4 random 6 0.02 0 1
8 4 random 6 0.03 0 1
8 4 random 6 0.04 0 1
8 5 random 6 0.02 0 1
8 5 random 6 0.03 0 1
8 5 random 6 0.04 0 1
EOF