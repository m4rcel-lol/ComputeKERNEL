#!/usr/bin/env sh
set -eu

VM_TARGET="${VM_TARGET:-main}"
ISO_ROOT="build/iso_root"
ISO_PATH="out/computekernel.iso"

if [ "$VM_TARGET" != "main" ]; then
    ISO_PATH="out/computekernel-${VM_TARGET}.iso"
fi

# Assemble the ISO directory tree
mkdir -p "${ISO_ROOT}/boot/grub"
cp out/computekernel.elf "${ISO_ROOT}/boot/computekernel.elf"
cp boot/grub/grub.cfg    "${ISO_ROOT}/boot/grub/grub.cfg"

mkdir -p "${ISO_ROOT}/etc"
cat > "${ISO_ROOT}/etc/live.conf" <<'EOF'
USER=root
# WARNING: test-only defaults for local live media validation; do not ship to production.
ROOT_PASSWORD_DEFAULTS=toor,password
DEFAULT_KEYBOARD_LAYOUT=us
EOF

# Create a bootable El Torito ISO using GRUB
mkdir -p out
grub-mkrescue -o "${ISO_PATH}" "${ISO_ROOT}"

echo "[CK] ISO → ${ISO_PATH}"
