#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_SCRIPT="${SCRIPT_DIR}/script.sh"

DEFAULT_DATA_PREFIX="$(sed -n 's/^DEFAULT_DATA_PREFIX="\([^"]\+\)"$/\1/p' "${RUN_SCRIPT}" | head -n 1)"
DATA_PREFIX="${DEFAULT_DATA_PREFIX:-simple}"

RUNS="${1:-20}"
SLEEP_SEC="${2:-0.0}"

OUTPUT_DIR="${SCRIPT_DIR}/statistics"
mkdir -p "${OUTPUT_DIR}"

STAMP="$(date +%Y%m%d-%H%M%S)"
CSV_FILE="${OUTPUT_DIR}/benchmark-${DATA_PREFIX}-simple-${STAMP}.csv"
SUMMARY_FILE="${OUTPUT_DIR}/benchmark-${DATA_PREFIX}-simple-${STAMP}-summary.txt"

printf "run,result,timing_total_sec\n" > "${CSV_FILE}"

for ((i=1; i<=RUNS; i++)); do
    TMP_LOG="$(mktemp)"
    bash "${RUN_SCRIPT}" > "${TMP_LOG}" 2>&1

    RESULT="$(sed -n 's/^Result = \([0-9.][0-9.]*\)$/\1/p' "${TMP_LOG}" | tail -n 1 | awk '{printf "%.6f", $1 / 60}')"
    TOTAL_TIME="$(sed -n 's/^Timing .* total=\([0-9.][0-9.]*\)$/\1/p' "${TMP_LOG}" | tail -n 1)"

    rm -f "${TMP_LOG}"

    printf "%d,%s,%s\n" "${i}" "${RESULT}" "${TOTAL_TIME}" >> "${CSV_FILE}"
    sleep "${SLEEP_SEC}"
done

AVG_RESULT="$(awk -F, 'NR>1 {sum+=$2; n++} END {if (n>0) printf "%.6f", sum/n; else print "NaN"}' "${CSV_FILE}")"
AVG_TIME="$(awk -F, 'NR>1 {sum+=$3; n++} END {if (n>0) printf "%.2f", sum/n; else print "NaN"}' "${CSV_FILE}")"

BEST_LINE="$(awk -F, 'NR==2 {best=$2; run=$1; t=$3} NR>2 && $2<best {best=$2; run=$1; t=$3} END {if (NR>1) printf "%s,%s,%s", run, best, t}' "${CSV_FILE}")"
BEST_RUN="${BEST_LINE%%,*}"
REST="${BEST_LINE#*,}"
BEST_RESULT="${REST%%,*}"
BEST_TIME="${REST#*,}"

{
    echo "data_prefix=${DATA_PREFIX}"
    echo "runs=${RUNS}"
    echo "sleep_sec=${SLEEP_SEC}"

    RUN_SCRIPT_CMD="$(sed -n '/mpirun/,$p' "${RUN_SCRIPT}" | tr '\n' ' ' | sed 's/\\ //g' | sed -E 's/ +/ /g' | sed 's/^ *//; s/ *$//')"
    MPINP="$(echo "${RUN_SCRIPT_CMD}" | awk '{for(i=1;i<=NF;i++) if($i=="-np"){print $(i+1); exit}}')"
    get_opt() {
        echo "${RUN_SCRIPT_CMD}" | awk -v opt="$1" '{for(i=1;i<=NF;i++) if($i==opt){print $(i+1); exit}}'
    }

    ADAPTIVE_ITERATIONS="$(get_opt --adaptive-iterations)"
    ADAPTIVE_PULL_ELITE_SEGMENTS="$(get_opt --adaptive-pull-elite-segments)"
    ELITE_PULL_STRATEGY="$(get_opt --elite-pull-strategy)"
    MIN_PULL_ELITES_PER_WORKER_FACTOR="$(get_opt --min-pull-elites-per-worker-factor)"
    ELITE_POOL_FACTOR="$(get_opt --elite-pool-factor)"
    RANDOMIZE_WORKER_HYPARAMS="$(echo "${RUN_SCRIPT_CMD}" | grep -q -- '--randomize-worker-hyperparams' && echo true || echo false)"
    PREFER_PULLED="$(echo "${RUN_SCRIPT_CMD}" | grep -q -- '--prefer-pulled' && echo true || echo false)"

    echo "run_script_cmd=${RUN_SCRIPT_CMD}"
    echo "mpirun_np=${MPINP}"
    echo "adaptive_iterations=${ADAPTIVE_ITERATIONS}"
    echo "adaptive_pull_elite_segments=${ADAPTIVE_PULL_ELITE_SEGMENTS}"
    echo "elite_pull_strategy=${ELITE_PULL_STRATEGY}"
    echo "min_pull_elites_per_worker_factor=${MIN_PULL_ELITES_PER_WORKER_FACTOR}"
    echo "elite_pool_factor=${ELITE_POOL_FACTOR}"
    echo "randomize_worker_hyperparams=${RANDOMIZE_WORKER_HYPARAMS}"

    echo "average_result=${AVG_RESULT}"
    echo "average_total_time_sec=${AVG_TIME}"
    echo "best_result=${BEST_RESULT}"
    echo "best_result_run=${BEST_RUN}"
    echo "best_result_total_time_sec=${BEST_TIME}"
    echo "csv_file=${CSV_FILE}"
} > "${SUMMARY_FILE}"

echo "Average total time (s): ${AVG_TIME}"
echo "Average result: ${AVG_RESULT}"
echo "Best result: ${BEST_RESULT}"
echo "Saved: ${CSV_FILE}"
echo "Saved: ${SUMMARY_FILE}"

rm -f "${SCRIPT_DIR}/outputs"/*