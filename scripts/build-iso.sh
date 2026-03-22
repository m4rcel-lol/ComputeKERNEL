#!/usr/bin/env sh
# build-iso.sh — builds a Limine-based ISO for ComputeKERNEL
set -eu

ISO_ROOT="build/iso_root"
ISO_PATH="out/computekernel.iso"
LIMINE_DIR="Limine-8.x-binary"

# 1. Clean and recreate ISO root
rm -rf "${ISO_ROOT}"
mkdir -p "${ISO_ROOT}/boot"
mkdir -p "${ISO_ROOT}/EFI/BOOT"

# 2. Copy kernel and config
cp out/computekernel.elf "${ISO_ROOT}/boot/computekernel.elf"
cp limine.conf           "${ISO_ROOT}/boot/limine.conf"

# 3. Copy Limine bootloader files
# BIOS
cp "${LIMINE_DIR}/limine-bios.sys"    "${ISO_ROOT}/boot/"
cp "${LIMINE_DIR}/limine-bios-cd.bin" "${ISO_ROOT}/boot/"
# UEFI
cp "${LIMINE_DIR}/limine-uefi-cd.bin" "${ISO_ROOT}/boot/"
cp "${LIMINE_DIR}/BOOTX64.EFI"        "${ISO_ROOT}/EFI/BOOT/"
cp "${LIMINE_DIR}/BOOTIA32.EFI"       "${ISO_ROOT}/EFI/BOOT/"

mkdir -p out

# 4. Build the ISO using xorriso (if available)
if command -v xorriso >/dev/null 2>&1; then
    xorriso -as mkisofs -b boot/limine-bios-cd.bin \
        -no-emul-boot -boot-load-size 4 -boot-info-table \
        --efi-boot boot/limine-uefi-cd.bin \
        -efi-boot-part --efi-boot-image --protective-msdos-label \
        "${ISO_ROOT}" -o "${ISO_PATH}"
    
    # 5. Enroll the Limine BIOS bootloader (hybrid ISO)
    if [ -f "./${LIMINE_DIR}/limine" ]; then
        ./${LIMINE_DIR}/limine bios-install "${ISO_PATH}"
    elif [ -f "./${LIMINE_DIR}/limine.exe" ]; then
        ./${LIMINE_DIR}/limine.exe bios-install "${ISO_PATH}"
    fi
    echo "[CK] Limine ISO → ${ISO_PATH}"
else
    echo "[CK] error: xorriso not found. Please install xorriso to build the ISO."
    echo "[CK] you can also manually build the ISO from '${ISO_ROOT}' using any ISO tool."
fi
