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

echo "Launching QEMU with out/computekernel.iso..."
# Set OVMF_PATH environment variable to point to your OVMF.fd for UEFI testing
UEFI_FLAGS=""
if [ -n "${OVMF_PATH:-}" ]; then
    UEFI_FLAGS="-bios ${OVMF_PATH}"
    echo "[CK] UEFI mode enabled (using OVMF)"
fi

qemu-system-x86_64 \
  ${UEFI_FLAGS} \
  -m 1024 \
  -cdrom out/computekernel.iso \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device e1000,netdev=net0 \
  -serial stdio \
  -no-reboot
