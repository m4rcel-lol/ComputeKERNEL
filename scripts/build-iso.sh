#!/usr/bin/env sh
# build-iso.sh — builds one universal ComputeKERNEL ISO that boots on any
# hardware (BIOS / UEFI, USB / DVD / optical) and inside any VM
# (QEMU, KVM, VirtualBox, VMware, Hyper-V, …).
set -eu

ISO_ROOT="build/iso_root"
ISO_PATH="out/computekernel.iso"

# Assemble the ISO directory tree
if [ -z "${ISO_ROOT}" ] || [ "${ISO_ROOT}" = "/" ]; then
    echo "[CK] refusing to remove unsafe ISO_ROOT='${ISO_ROOT}'"
    exit 1
fi
rm -rf "${ISO_ROOT}"
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

mkdir -p out

# Universal image: force GRUB plain-text console so it works on bare metal
# (no VESA/GOP firmware required) AND inside every common hypervisor.
GRUB_TERMINAL=console GRUB_TERMINAL_OUTPUT=console \
    grub-mkrescue -o "${ISO_PATH}" "${ISO_ROOT}"

# Post-process into a hybrid image so the same .iso file can be written
# directly to a USB drive (BIOS or UEFI) without a separate tool.
if command -v isohybrid >/dev/null 2>&1; then
    if isohybrid --uefi "${ISO_PATH}" 2>/dev/null; then
        echo "[CK] isohybrid --uefi applied (USB-bootable on UEFI + BIOS)"
    elif isohybrid "${ISO_PATH}" 2>/dev/null; then
        echo "[CK] isohybrid (BIOS) fallback applied (USB-bootable on BIOS)"
    else
        echo "[CK] warning: isohybrid post-processing failed (ISO still usable)"
    fi
fi

echo "[CK] Universal ISO → ${ISO_PATH}"
