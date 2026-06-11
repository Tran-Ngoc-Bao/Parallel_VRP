#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_SCRIPT="${SCRIPT_DIR}/script.sh"

DEFAULT_DATA_PREFIX="$(sed -n 's/^DEFAULT_DATA_PREFIX="\([^"]\+\)"$/\1/p' "${RUN_SCRIPT}" | head -n 1)"
DATA_PREFIX="${1:-${DEFAULT_DATA_PREFIX}.}"
RUNS="${2:-5}"
SLEEP_SEC="${3:-0.0}"

DATA_FILES=( $(ls -1 ${SCRIPT_DIR}/../../data/soict-2025/${DATA_PREFIX}*.txt 2>/dev/null | sort) )

if [ ${#DATA_FILES[@]} -eq 0 ]; then
    echo "No data files found for prefix ${DATA_PREFIX}" >&2
    exit 1
fi

OUTPUT_DIR="${SCRIPT_DIR}/statistics"
mkdir -p "${OUTPUT_DIR}"

STAMP="$(date +%Y%m%d-%H%M%S)"
CSV_FILE="${OUTPUT_DIR}/benchmark-${DATA_PREFIX}-${STAMP}.csv"
SUMMARY_FILE="${OUTPUT_DIR}/benchmark-${DATA_PREFIX}-${STAMP}-summary.txt"

printf "data_file,run,result,timing_total_sec\n" > "${CSV_FILE}"

for DATA_FILE in "${DATA_FILES[@]}"; do
    DATA_FILE_NAME="$(basename "${DATA_FILE}" .txt)"
    for ((i=1; i<=RUNS; i++)); do
        TMP_LOG="$(mktemp)"
        bash "${RUN_SCRIPT}" "${DATA_FILE}" > "${TMP_LOG}" 2>&1

        RESULT="$(sed -n 's/.*Result = \([0-9.][0-9.]*\).*/\1/p' "${TMP_LOG}" | tail -n 1 | awk '{printf "%.6f", $1 / 60}')"
        TOTAL_TIME="$(sed -n 's/^\[Timing\] Total runtime: \([0-9.][0-9.]*\) s$/\1/p' "${TMP_LOG}" | tail -n 1)"

        rm -f "${TMP_LOG}"

        printf "%s,%d,%s,%s\n" "${DATA_FILE_NAME}" "${i}" "${RESULT}" "${TOTAL_TIME}" >> "${CSV_FILE}"
        sleep "${SLEEP_SEC}"
    done
done

AVG_RESULT="$(awk -F, 'NR>1 {sum+=$3; n++} END {if (n>0) printf "%.6f", sum/n; else print "NaN"}' "${CSV_FILE}")"
AVG_TIME="$(awk -F, 'NR>1 {sum+=$4; n++} END {if (n>0) printf "%.2f", sum/n; else print "NaN"}' "${CSV_FILE}")"

BEST_INFO="$(awk -F, '
    NR>1 {
        f=$1
        val=$3+0
        t=$4+0
        if ((f in minval) == 0) { minval[f]=val; mintime[f]=t }
        else if (val < minval[f]) { minval[f]=val; mintime[f]=t }
    }
    END {
        sum=0; sumt=0; m=0
        for (f in minval) { sum += minval[f]; sumt += mintime[f]; m++ }
        if (m>0) printf "%d,%.6f,%.2f", m, sum/m, sumt/m
    }' "${CSV_FILE}")"

FILE_COUNT="${BEST_INFO%%,*}"
REST="${BEST_INFO#*,}"
BEST_RESULT="${REST%%,*}"
BEST_TIME="${REST#*,}"
BEST_FILE="${DATA_PREFIX}"

{
    echo "data_prefix=${DATA_PREFIX}"
    echo "runs=${RUNS}"
    echo "sleep_sec=${SLEEP_SEC}"
    echo "average_result=${AVG_RESULT}"
    echo "average_total_time_sec=${AVG_TIME}"
    echo "best_result=${BEST_RESULT}"
    echo "best_result_data_file=${BEST_FILE}"
    echo "file_count=${FILE_COUNT}"
    echo "best_result_total_time_sec=${BEST_TIME}"
    echo "csv_file=${CSV_FILE}"
} > "${SUMMARY_FILE}"

echo "Average total time (s): ${AVG_TIME}"
echo "Average result: ${AVG_RESULT}"
echo "Best result: ${BEST_RESULT}"
echo "Saved: ${CSV_FILE}"
echo "Saved: ${SUMMARY_FILE}"

rm -f "${SCRIPT_DIR}/outputs"/*
