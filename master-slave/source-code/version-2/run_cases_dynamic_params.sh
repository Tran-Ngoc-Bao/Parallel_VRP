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
200 10 4 random 2 0.03 1 1 10
200 10 4 random 4 0.03 1 1 10
200 10 4 random 6 0.03 1 1 10
200 10 4 random 8 0.03 1 1 10
200 10 4 random 10 0.03 1 1 10
200 10 4 topk 2 0.03 1 1 10
200 10 4 topk 4 0.03 1 1 10
200 10 4 topk 6 0.03 1 1 10
200 10 4 topk 8 0.03 1 1 10
200 10 4 topk 10 0.03 1 1 10
200 10 4 rank 2 0.03 1 1 10
200 10 4 rank 4 0.03 1 1 10
200 10 4 rank 6 0.03 1 1 10
200 10 4 rank 8 0.03 1 1 10
200 10 4 rank 10 0.03 1 1 10
200 10 4 pullcount 2 0.03 1 1 10
200 10 4 pullcount 4 0.03 1 1 10
200 10 4 pullcount 6 0.03 1 1 10
200 10 4 pullcount 8 0.03 1 1 10
200 10 4 pullcount 10 0.03 1 1 10
EOF