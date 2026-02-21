#!/bin/bash
# VaultOS - Debug with QEMU + GDB
# Connect GDB: target remote localhost:1234
set -e

DISK_IMG="build/disk.img"
OVMF="third_party/ovmf/OVMF.fd"

echo "=== VaultOS Debug Mode ==="
echo "  QEMU will wait for GDB connection on localhost:1234"
echo "  Connect with: gdb -ex 'target remote :1234' build/VAULTOS.BIN"

qemu-system-x86_64 \
    -bios "${OVMF}" \
    -drive file="${DISK_IMG}",format=raw \
    -m 256M \
    -serial stdio \
    -no-reboot \
    -no-shutdown \
    -s -S \
    -d int,cpu_reset \
    "$@"
