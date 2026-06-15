#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
    SUDO=""
else
    if ! command -v sudo >/dev/null 2>&1; then
        echo "Error: this script needs root privileges, but 'sudo' is not installed." >&2
        exit 1
    fi
    SUDO="sudo"
fi

if ! command -v apt-get >/dev/null 2>&1; then
    echo "Error: this script is intended for Debian/Ubuntu systems with 'apt-get'." >&2
    exit 1
fi

${SUDO} apt-get update
${SUDO} DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    cmake \
    git \
    openmpi-bin \
    libopenmpi-dev \
    python3 \
    python3-pip

pip3 install matplotlib

echo "Done."