#!/usr/bin/env sh
set -eu

mkdir -p out
VM_TARGET="${VM_TARGET:-main}"
ISO_PATH="out/computekernel.iso"

if [ "$VM_TARGET" != "main" ]; then
    ISO_PATH="out/computekernel-${VM_TARGET}.iso"
fi

echo "ISO assembly placeholder: create ${ISO_PATH} in later phase."
echo "ComputeKERNEL scaffold ISO marker (${VM_TARGET})" > "${ISO_PATH}"
echo "Created ${ISO_PATH} (placeholder text file)."
