#!/usr/bin/env sh
set -eu

VM_TARGET="${VM_TARGET:-main}"
ISO_ROOT="build/iso_root"
ISO_PATH="out/computekernel.iso"

if [ "$VM_TARGET" != "main" ]; then
    ISO_PATH="out/computekernel-${VM_TARGET}.iso"
fi

# Assemble the ISO directory tree
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

# Create a bootable El Torito ISO using GRUB
mkdir -p out
if [ "${VM_TARGET}" = "main" ]; then
    # Real-hardware-focused image: force GRUB console mode and hybrid USB layout.
    MKRESCUE_XORRISO_FLAGS="-iso-level 3 -full-iso9660-filenames -volid COMPUTEKERNEL -isohybrid-gpt-basdat"
    GRUB_TERMINAL=console GRUB_TERMINAL_OUTPUT=console \
        grub-mkrescue -o "${ISO_PATH}" "${ISO_ROOT}" \
        -- ${MKRESCUE_XORRISO_FLAGS}
    # If available, post-process for broader BIOS/UEFI USB compatibility.
    if command -v isohybrid >/dev/null 2>&1; then
        if isohybrid --uefi "${ISO_PATH}"; then
            echo "[CK] isohybrid --uefi applied"
        elif isohybrid "${ISO_PATH}"; then
            echo "[CK] isohybrid (BIOS) fallback applied"
        else
            echo "[CK] warning: isohybrid post-processing failed"
        fi
    fi
else
    grub-mkrescue -o "${ISO_PATH}" "${ISO_ROOT}"
fi

echo "[CK] ISO → ${ISO_PATH}"
