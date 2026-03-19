#!/usr/bin/env sh
set -eu

echo "[test] running scaffold checks"
./scripts/check-env.sh
python3 tools/gen_syscalls.py --out /tmp/computekernel-generated

if ! grep -Eq '^terminal_input[[:space:]]+console$' boot/grub/grub.cfg; then
    echo "[test] expected GRUB terminal_input console setting is missing"
    exit 1
fi

if ! grep -Eq '^terminal_output[[:space:]]+console$' boot/grub/grub.cfg; then
    echo "[test] expected GRUB terminal_output console setting is missing"
    exit 1
fi

if [ ! -f /tmp/computekernel-generated/syscall_nr.h ]; then
    echo "[test] expected generated syscall header missing"
    exit 1
fi

if ! grep -Eq '^menuentry "ComputeKERNEL \(Live Root\)"' boot/grub/grub.cfg; then
    echo "[test] expected live root GRUB menu entry is missing"
    exit 1
fi

if ! grep -Eq '^menuentry "ComputeKERNEL \(Setup Installer\)"' boot/grub/grub.cfg; then
    echo "[test] expected setup installer GRUB menu entry is missing"
    exit 1
fi

if ! grep -Eq '\bkblayout\b' kernel/shell/shell.c; then
    echo "[test] expected keyboard layout command support in shell"
    exit 1
fi

echo "[test] scaffold checks passed"
