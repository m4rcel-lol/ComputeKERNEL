#!/usr/bin/env sh
set -eu

if [ ! -f out/computekernel.iso ]; then
    echo "out/computekernel.iso missing. Run: make iso"
    exit 1
fi

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "qemu-system-x86_64 not found in PATH"
    exit 1
fi

echo "Launching QEMU with KVM acceleration..."
qemu-system-x86_64 \
  -enable-kvm \
  -cpu host \
  -m 1024 \
  -cdrom out/computekernel.iso \
  -serial stdio \
  -no-reboot

