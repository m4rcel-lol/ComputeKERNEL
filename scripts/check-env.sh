#!/usr/bin/env sh
set -eu

missing_required=0
missing_optional=0

check_required_bin() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing required: $1"
        missing_required=1
    else
        echo "found: $1"
    fi
}

check_optional_bin() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing optional: $1"
        missing_optional=1
    else
        echo "found: $1"
    fi
}

check_required_bin python3
check_required_bin make
check_optional_bin qemu-system-x86_64

if [ "$missing_required" -ne 0 ]; then
    echo "Environment check failed: required tools missing."
    exit 1
fi

if [ "$missing_optional" -ne 0 ]; then
    echo "Environment check warning: optional VM tools missing."
fi

echo "Environment check passed."
