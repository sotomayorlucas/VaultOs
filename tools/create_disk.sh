#!/bin/bash
# VaultOS - Create bootable disk image
# Creates a FAT32 disk image with UEFI bootloader and kernel

set -e

BUILD_DIR="build"
DISK_IMG="${BUILD_DIR}/disk.img"
KERNEL_BIN="${BUILD_DIR}/VAULTOS.BIN"
BOOTLOADER="${BUILD_DIR}/BOOTX64.EFI"

echo "=== VaultOS Disk Image Creator ==="

# Create 64 MiB disk image
dd if=/dev/zero of="${DISK_IMG}" bs=1M count=64 2>/dev/null

# Format as FAT32
mkfs.fat -F 32 "${DISK_IMG}" >/dev/null

# Create EFI directory structure
mmd -i "${DISK_IMG}" ::/EFI
mmd -i "${DISK_IMG}" ::/EFI/BOOT

# Copy bootloader if it exists
if [ -f "${BOOTLOADER}" ]; then
    mcopy -i "${DISK_IMG}" "${BOOTLOADER}" ::/EFI/BOOT/BOOTX64.EFI
    echo "  Bootloader: OK"
else
    echo "  WARNING: No bootloader found at ${BOOTLOADER}"
fi

# Copy kernel
if [ -f "${KERNEL_BIN}" ]; then
    mcopy -i "${DISK_IMG}" "${KERNEL_BIN}" ::/VAULTOS.BIN
    echo "  Kernel: OK ($(stat -f%z "${KERNEL_BIN}" 2>/dev/null || stat --format=%s "${KERNEL_BIN}" 2>/dev/null) bytes)"
else
    echo "  ERROR: Kernel not found at ${KERNEL_BIN}"
    exit 1
fi

echo "  Disk image: ${DISK_IMG} (64 MiB)"
echo "=== Done ==="
