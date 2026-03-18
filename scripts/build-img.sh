#!/usr/bin/env sh
set -eu

# build-img.sh — create a bootable raw disk image for ComputeKERNEL.
#
# This script requires root privileges (for losetup, mount, grub-install).
# Run with:  sudo make img   or   sudo sh scripts/build-img.sh
#
# Tested on Linux with grub-pc-bin, parted, e2fsprogs installed.

IMG="out/computekernel.img"
MOUNT_DIR="$(mktemp -d)"
LOOP=""

cleanup() {
    [ -n "${LOOP}" ] && losetup -d "${LOOP}" 2>/dev/null || true
    umount "${MOUNT_DIR}" 2>/dev/null || true
    rmdir "${MOUNT_DIR}" 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p out

# Create a 64 MiB raw disk image
dd if=/dev/zero of="${IMG}" bs=1M count=64

# Write an MBR partition table with one bootable primary ext2 partition
parted -s "${IMG}" mklabel msdos
parted -s "${IMG}" mkpart primary ext2 1MiB 100%
parted -s "${IMG}" set 1 boot on

# Attach to a loopback device (--partscan creates /dev/loopNp1)
LOOP="$(sudo losetup -f --show --partscan "${IMG}")"

# Format and populate the partition
sudo mkfs.ext2 "${LOOP}p1"
sudo mount "${LOOP}p1" "${MOUNT_DIR}"
sudo mkdir -p "${MOUNT_DIR}/boot/grub"
sudo cp out/computekernel.elf "${MOUNT_DIR}/boot/computekernel.elf"
sudo cp boot/grub/grub.cfg    "${MOUNT_DIR}/boot/grub/grub.cfg"

# Install GRUB bootloader onto the MBR and the partition
sudo grub-install \
    --target=i386-pc \
    --boot-directory="${MOUNT_DIR}/boot" \
    --modules="normal part_msdos ext2 multiboot2" \
    "${LOOP}"

echo "[CK] .img → ${IMG}"
