#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_SCRIPT="${SCRIPT_DIR}/script.sh"
DATA_FILE_NAME="$(sed -n 's|.*data/\([^/" ]\+\)\.txt.*|\1|p' "${RUN_SCRIPT}" | head -n 1)"

RUNS="${1:-10}"
SLEEP_SEC="${2:-3.0}"

OUTPUT_DIR="${SCRIPT_DIR}/statistics"
mkdir -p "${OUTPUT_DIR}"

STAMP="$(date +%Y%m%d-%H%M%S)"
CSV_FILE="${OUTPUT_DIR}/benchmark-${DATA_FILE_NAME}-${STAMP}.csv"
SUMMARY_FILE="${OUTPUT_DIR}/benchmark-${DATA_FILE_NAME}-${STAMP}-summary.txt"

printf "run,result,timing_total_sec\n" > "${CSV_FILE}"

for ((i=1; i<=RUNS; i++)); do
    TMP_LOG="$(mktemp)"
    bash "${RUN_SCRIPT}" > "${TMP_LOG}" 2>&1

    RESULT="$(sed -n 's/^Result = \([0-9.][0-9.]*\)$/\1/p' "${TMP_LOG}" | tail -n 1)"
    TOTAL_TIME="$(sed -n 's/^Timing .* total=\([0-9.][0-9.]*\)$/\1/p' "${TMP_LOG}" | tail -n 1)"

    rm -f "${TMP_LOG}"

    printf "%d,%s,%s\n" "${i}" "${RESULT}" "${TOTAL_TIME}" >> "${CSV_FILE}"
    sleep "${SLEEP_SEC}"
done

AVG_RESULT="$(awk -F, 'NR>1 {sum+=$2; n++} END {if (n>0) printf "%.2f", sum/n; else print "NaN"}' "${CSV_FILE}")"
AVG_TIME="$(awk -F, 'NR>1 {sum+=$3; n++} END {if (n>0) printf "%.2f", sum/n; else print "NaN"}' "${CSV_FILE}")"

BEST_LINE="$(awk -F, 'NR==2 {best=$2; run=$1; t=$3} NR>2 && $2<best {best=$2; run=$1; t=$3} END {if (NR>1) printf "%s,%s,%s", run, best, t}' "${CSV_FILE}")"
BEST_RUN="${BEST_LINE%%,*}"
REST="${BEST_LINE#*,}"
BEST_RESULT="${REST%%,*}"
BEST_TIME="${REST#*,}"

{
    echo "runs=${RUNS}"
    echo "sleep_sec=${SLEEP_SEC}"
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
