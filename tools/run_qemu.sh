#!/bin/bash
# VaultOS - Run in QEMU with UEFI
set -e

DISK_IMG="build/disk.img"
OVMF="third_party/ovmf/OVMF.fd"

if [ ! -f "${DISK_IMG}" ]; then
    echo "Error: Disk image not found. Run 'make disk' first."
    exit 1
fi

if [ ! -f "${OVMF}" ]; then
    echo "Error: OVMF firmware not found at ${OVMF}"
    echo "Install with: apt install ovmf && cp /usr/share/OVMF/OVMF_CODE.fd ${OVMF}"
    exit 1
fi

echo "=== Launching VaultOS in QEMU ==="
qemu-system-x86_64 \
    -bios "${OVMF}" \
    -drive file="${DISK_IMG}",format=raw \
    -m 256M \
    -serial stdio \
    -no-reboot \
    -no-shutdown \
    "$@"
