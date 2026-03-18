#!/usr/bin/env sh
set -eu

missing=0

check_bin() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing: $1"
        missing=1
    else
        echo "found: $1"
    fi
}

check_bin python3
check_bin make
check_bin qemu-system-x86_64

if [ "$missing" -ne 0 ]; then
    echo "Environment check failed."
    exit 1
fi

echo "Environment check passed."

