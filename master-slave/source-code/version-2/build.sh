#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Usage:
#   ./build.sh          -- normal release build
#   ./build.sh pgo      -- two-pass PGO build (instrument → profile → optimized)
#   ./build.sh pgo FILE -- PGO using FILE as the profiling workload
#                          (default: ../../data/soict-2025/100.40.2.txt)

MODE="${1:-}"
PROFILE_PROBLEM="${2:-${SCRIPT_DIR}/../../data/soict-2025/100.40.2.txt}"

if [ "${MODE}" = "pgo" ]; then
    if [ ! -f "${PROFILE_PROBLEM}" ]; then
        echo "Error: profile workload not found: ${PROFILE_PROBLEM}" >&2
        exit 1
    fi

    echo "=== PGO pass 1: instrumented build ==="
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DPGO_GENERATE=ON -DPGO_USE=OFF
    cmake --build "${BUILD_DIR}" --clean-first

    echo "=== PGO: collecting profile data ==="
    mpirun -np 2 "${BUILD_DIR}/tabu_search" run "${PROFILE_PROBLEM}" \
        --disable-logging > /dev/null 2>&1
    echo "    profile data written to ${BUILD_DIR}/pgo-data/"

    echo "=== PGO pass 2: optimized build ==="
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DPGO_GENERATE=OFF -DPGO_USE=ON
    cmake --build "${BUILD_DIR}" --clean-first

    echo "=== PGO build complete ==="
else
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
    cmake --build "${BUILD_DIR}"
fi
