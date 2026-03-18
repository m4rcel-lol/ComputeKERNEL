#!/usr/bin/env sh
set -eu

echo "[test] running scaffold checks"
./scripts/check-env.sh
python3 tools/gen_syscalls.py --out /tmp/computekernel-generated

if [ ! -f /tmp/computekernel-generated/syscall_nr.h ]; then
    echo "[test] expected generated syscall header missing"
    exit 1
fi

echo "[test] scaffold checks passed"
