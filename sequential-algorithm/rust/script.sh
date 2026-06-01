#!/usr/bin/env bash
set -euo pipefail

# script.sh - build and run the sequence-tabu-search Rust binary
# Usage:
#   ./script.sh [run] -- <args-for-program>
# Examples:
#   ./script.sh run -- run --problem problems/some_problem.json
#   ./script.sh build

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODE="run"

if [ $# -ge 1 ] && [ "$1" != "--" ]; then
  MODE="$1"
  shift || true
fi

# remaining args passed to cargo run's subcommand
PROGRAM_ARGS=("$@")

cd "$PROJECT_DIR"

if ! command -v cargo >/dev/null 2>&1; then
  echo "Error: cargo not found. Run this script inside your Docker container (which has Rust), or install Rust locally."
  exit 1
fi

if [ "$MODE" = "build" ]; then
  RUSTFLAGS="${RUSTFLAGS:-} -C target-cpu=native" cargo build --release
  exit $?
fi

# default: build then run
RUSTFLAGS="${RUSTFLAGS:-} -C target-cpu=native" cargo build --release
if [ "${#PROGRAM_ARGS[@]}" -eq 0 ]; then
  RUSTFLAGS="${RUSTFLAGS:-} -C target-cpu=native" cargo run --release -- run
else
  RUSTFLAGS="${RUSTFLAGS:-} -C target-cpu=native" cargo run --release -- "${PROGRAM_ARGS[@]}"
fi
