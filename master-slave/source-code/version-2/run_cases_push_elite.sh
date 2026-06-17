#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCHMARK_RUN="${SCRIPT_DIR}/benchmark_runs.sh"

while read -r prefix ai seg strat minpull pool rand prefer_pulled workers; do
  echo "RUN CASE: $prefix $ai $seg $strat $minpull $pool $rand $prefer_pulled $workers"

  DEFAULT_DATA_PREFIX="$prefix" \
  ADAPTIVE_ITERATIONS="$ai" \
  ADAPTIVE_PULL_ELITE_SEGMENTS="$seg" \
  ELITE_PULL_STRATEGY="$strat" \
  MIN_PULL_ELITES_PER_WORKER_FACTOR="$minpull" \
  ELITE_POOL_FACTOR="$pool" \
  RANDOMIZE_WORKER_HYPERPARAMS="$rand" \
  PREFER_PULLED="$prefer_pulled" \
  NUM_WORKERS="$workers" \
  bash "${BENCHMARK_RUN}" </dev/null
done <<'EOF'
100 6 3 random 6 0.02 0 0 10
100 6 3 random 6 0.03 0 0 10
100 6 3 random 6 0.04 0 0 10
100 6 4 random 6 0.02 0 0 10
100 6 4 random 6 0.03 0 0 10
100 6 4 random 6 0.04 0 0 10
100 6 5 random 6 0.02 0 0 10
100 6 5 random 6 0.03 0 0 10
100 6 5 random 6 0.04 0 0 10
100 8 3 random 6 0.02 0 0 10
100 8 3 random 6 0.03 0 0 10
100 8 3 random 6 0.04 0 0 10
100 8 4 random 6 0.02 0 0 10
100 8 4 random 6 0.03 0 0 10
100 8 4 random 6 0.04 0 0 10
100 8 5 random 6 0.02 0 0 10
100 8 5 random 6 0.03 0 0 10
100 8 5 random 6 0.04 0 0 10
100 10 3 random 6 0.02 0 0 10
100 10 3 random 6 0.03 0 0 10
100 10 3 random 6 0.04 0 0 10
100 10 4 random 6 0.02 0 0 10
100 10 4 random 6 0.03 0 0 10
100 10 4 random 6 0.04 0 0 10
100 10 5 random 6 0.02 0 0 10
100 10 5 random 6 0.03 0 0 10
100 10 5 random 6 0.04 0 0 10
100 6 3 random 6 0.02 0 1 10
100 6 3 random 6 0.03 0 1 10
100 6 3 random 6 0.04 0 1 10
100 6 4 random 6 0.02 0 1 10
100 6 4 random 6 0.03 0 1 10
100 6 4 random 6 0.04 0 1 10
100 6 5 random 6 0.02 0 1 10
100 6 5 random 6 0.03 0 1 10
100 6 5 random 6 0.04 0 1 10
100 8 3 random 6 0.02 0 1 10
100 8 3 random 6 0.03 0 1 10
100 8 3 random 6 0.04 0 1 10
100 8 4 random 6 0.02 0 1 10
100 8 4 random 6 0.03 0 1 10
100 8 4 random 6 0.04 0 1 10
100 8 5 random 6 0.02 0 1 10
100 8 5 random 6 0.03 0 1 10
100 8 5 random 6 0.04 0 1 10
100 10 3 random 6 0.02 0 1 10
100 10 3 random 6 0.03 0 1 10
100 10 3 random 6 0.04 0 1 10
100 10 4 random 6 0.02 0 1 10
100 10 4 random 6 0.03 0 1 10
100 10 4 random 6 0.04 0 1 10
100 10 5 random 6 0.02 0 1 10
100 10 5 random 6 0.03 0 1 10
100 10 5 random 6 0.04 0 1 10
EOF