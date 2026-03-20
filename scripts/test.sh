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

if ! grep -Eq 'TCP/IP stack not available in current kernel build' kernel/shell/shell.c; then
    echo "[test] expected ssh command to report in-kernel TCP/IP stack status"
    exit 1
fi

if ! grep -Eq 'setup-guide' kernel/shell/shell.c; then
    echo "[test] expected setup-guide command support in shell"
    exit 1
fi

if ! grep -Eq 'rm \[-r\] <path>' kernel/shell/shell.c; then
    echo "[test] expected recursive rm help usage in shell"
    exit 1
fi

if ! grep -Eq '\btree\b' kernel/shell/shell.c; then
    echo "[test] expected tree command support in shell"
    exit 1
fi

if ! grep -Eq '\bdu\b' kernel/shell/shell.c; then
    echo "[test] expected du command support in shell"
    exit 1
fi

if ! grep -Eq 'cksh built-in commands' kernel/shell/shell.c; then
    echo "[test] expected structured help banner for shell commands"
    exit 1
fi

if ! grep -Eq '\[ SYSTEM INFO \]' kernel/shell/shell.c; then
    echo "[test] expected SYSTEM INFO section in shell help output"
    exit 1
fi

if ! grep -Eq '\bmotd\b' kernel/shell/shell.c; then
    echo "[test] expected motd command support in shell"
    exit 1
fi

if ! grep -Eq '\bpalette\b' kernel/shell/shell.c; then
    echo "[test] expected palette command support in shell"
    exit 1
fi

if ! grep -Eq '\btips\b' kernel/shell/shell.c; then
    echo "[test] expected tips command support in shell"
    exit 1
fi

if ! grep -Eq '\bbanner\b' kernel/shell/shell.c; then
    echo "[test] expected banner command support in shell"
    exit 1
fi

if ! grep -Eq '\btasks\b' kernel/shell/shell.c; then
    echo "[test] expected tasks command support in shell"
    exit 1
fi

if ! grep -Eq '\bsyscheck\b' kernel/shell/shell.c; then
    echo "[test] expected syscheck command support in shell"
    exit 1
fi

if ! grep -Eq '\bneofetch\b' kernel/shell/shell.c; then
    echo "[test] expected neofetch alias support in shell"
    exit 1
fi

if ! grep -Fq '@@@@@@@@@@          @@@@@@@@@@@@' kernel/shell/shell.c; then
    echo "[test] expected updated fastfetch ASCII art in shell output"
    exit 1
fi

if ! grep -Eq 'Terminal: VGA %ux%u' kernel/shell/shell.c; then
    echo "[test] expected terminal resolution output to use dynamic VGA dimensions"
    exit 1
fi

if ! grep -Eq '\bmouse\b' kernel/shell/shell.c; then
    echo "[test] expected mouse command support in shell"
    exit 1
fi

if ! grep -Eq '\bresolution\b' kernel/shell/shell.c; then
    echo "[test] expected resolution command support in shell"
    exit 1
fi

if ! grep -Eq 'hostfwd=tcp::2222-:22' scripts/run-qemu.sh; then
    echo "[test] expected QEMU launcher to configure SSH TCP port forwarding"
    exit 1
fi

if ! grep -Eq 'hostfwd=tcp::2222-:22' scripts/run-kvm.sh; then
    echo "[test] expected QEMU+KVM launcher to configure SSH TCP port forwarding"
    exit 1
fi

if ! grep -Eq 'make kernel' .github/workflows/build-universal-iso.yml; then
    echo "[test] expected universal ISO workflow to run full kernel build"
    exit 1
fi

if ! grep -Eq 'make lint' .github/workflows/build-universal-iso.yml; then
    echo "[test] expected universal ISO workflow to run lint checks"
    exit 1
fi

if ! grep -Eq 'make test' .github/workflows/build-universal-iso.yml; then
    echo "[test] expected universal ISO workflow to run test checks"
    exit 1
fi

if ! grep -Eq 'kernel/arch/x86_64/mouse.c' Makefile; then
    echo "[test] expected mouse driver to be compiled in kernel build"
    exit 1
fi

if ! grep -Eq 'task_create\("cksh"' kernel/init/main.c; then
    echo "[test] expected kernel boot path to create shell as scheduler task"
    exit 1
fi

if ! grep -Eq 'sched_start\(\)' kernel/init/main.c; then
    echo "[test] expected kernel boot path to start scheduler"
    exit 1
fi

echo "[test] scaffold checks passed"

if [ -f rust/Cargo.toml ]; then
    echo "[test] running rust check"
    make rust-check
fi
