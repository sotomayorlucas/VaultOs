#!/bin/bash
# VaultOS - Toolchain Setup Script (for WSL2 Ubuntu)
set -e

echo "=== VaultOS Toolchain Setup ==="
echo "Installing required packages..."

sudo apt update
sudo apt install -y \
    build-essential \
    nasm \
    mtools \
    qemu-system-x86 \
    ovmf \
    gnu-efi \
    xorriso \
    gdb

# Check for cross-compiler
if ! command -v x86_64-elf-gcc &> /dev/null; then
    echo ""
    echo "NOTE: x86_64-elf-gcc not found."
    echo "You can either:"
    echo "  1. Use x86_64-linux-gnu-gcc (update CC in Makefile)"
    echo "  2. Build a cross-compiler from source"
    echo ""
    echo "For option 1, the system GCC will work with our Makefile flags."
    echo "Just change CC=x86_64-elf-gcc to CC=gcc in the Makefile."
fi

# Copy OVMF firmware
if [ -f /usr/share/OVMF/OVMF_CODE.fd ]; then
    mkdir -p third_party/ovmf
    cp /usr/share/OVMF/OVMF_CODE.fd third_party/ovmf/OVMF.fd
    echo "OVMF firmware copied to third_party/ovmf/"
elif [ -f /usr/share/ovmf/OVMF.fd ]; then
    mkdir -p third_party/ovmf
    cp /usr/share/ovmf/OVMF.fd third_party/ovmf/OVMF.fd
    echo "OVMF firmware copied to third_party/ovmf/"
else
    echo "WARNING: OVMF not found. You may need to manually download it."
fi

echo ""
echo "=== Setup complete! ==="
echo "Next steps:"
echo "  1. make all     - Build everything"
echo "  2. make run     - Run in QEMU"
echo "  3. make debug   - Run with GDB"
