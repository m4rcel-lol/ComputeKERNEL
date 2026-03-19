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

if ! grep -Eq 'GRUB_TERMINAL=console[[:space:]]+GRUB_TERMINAL_OUTPUT=console' scripts/build-iso.sh; then
    echo "[test] expected main ISO build to force GRUB console terminal mode"
    exit 1
fi

if grep -Eq '\-isohybrid-gpt-basdat' scripts/build-iso.sh; then
    echo "[test] unexpected unsupported xorriso option (-isohybrid-gpt-basdat) present in build script"
    exit 1
fi

if ! grep -Eq '^insmod vga$' boot/grub/grub.cfg; then
    echo "[test] expected GRUB vga module to be loaded for text-mode fallback"
    exit 1
fi

if ! grep -Eq '^insmod vbe$' boot/grub/grub.cfg; then
    echo "[test] expected GRUB vbe module to be loaded for text-mode fallback"
    exit 1
fi

if ! grep -Eq '\bkblayout\b' kernel/shell/shell.c; then
    echo "[test] expected keyboard layout command support in shell"
    exit 1
fi

if ! grep -Eq '\bcredits\b' kernel/shell/shell.c; then
    echo "[test] expected credits command support in shell"
    exit 1
fi

if ! grep -Eq '\bnetinfo\b' kernel/shell/shell.c; then
    echo "[test] expected netinfo command support in shell"
    exit 1
fi

if ! grep -Eq '\bmouse\b' kernel/shell/shell.c; then
    echo "[test] expected mouse command support in shell"
    exit 1
fi

if ! grep -Eq 'kernel/arch/x86_64/mouse.c' Makefile; then
    echo "[test] expected mouse driver to be compiled in kernel build"
    exit 1
fi

echo "[test] scaffold checks passed"
