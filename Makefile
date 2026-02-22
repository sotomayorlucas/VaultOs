# VaultOS - Nanokernel Operating System
# Top-level Makefile
#
# Requires (install via WSL2 apt):
#   apt install gcc-x86-64-linux-gnu nasm mtools qemu-system-x86 ovmf gnu-efi
#

# Toolchain
CC       = x86_64-linux-gnu-gcc
LD       = x86_64-linux-gnu-ld
NASM     = nasm
OBJCOPY  = x86_64-linux-gnu-objcopy

# Paths
BUILD    = build
KERNEL   = kernel
BOOT     = boot
VSHELL   = shell
GUI      = gui
INCLUDE  = include

# Compiler flags (freestanding kernel)
CFLAGS   = -ffreestanding -nostdlib -nostdinc -fno-builtin \
           -fno-stack-protector -mno-red-zone -mcmodel=large \
           -march=x86-64-v2 -mtune=generic -funroll-loops \
           -Wall -Wextra -Wno-unused-parameter -O2 -g \
           -I$(KERNEL) -I$(INCLUDE) -I.

LDFLAGS  = -T $(KERNEL)/linker.ld -nostdlib -z max-page-size=4096

NASMFLAGS = -f elf64

# GNU-EFI paths
EFI_INCLUDE     = /usr/include/efi
EFI_INCLUDE_ARCH = /usr/include/efi/x86_64
EFI_LIB         = /usr/lib
EFI_CRT          = /usr/lib/crt0-efi-x86_64.o
EFI_LDS          = /usr/lib/elf_x86_64_efi.lds

# Bootloader flags
BOOT_CFLAGS = -I$(EFI_INCLUDE) -I$(EFI_INCLUDE_ARCH) \
              -ffreestanding -fno-stack-protector -fpic -fshort-wchar \
              -mno-red-zone -Wall -O2 -DEFI_FUNCTION_WRAPPER

BOOT_LDFLAGS = -nostdlib -znocombreloc -T $(EFI_LDS) \
               -shared -Bsymbolic -L$(EFI_LIB)

# Kernel C sources
KERNEL_C_SRCS = \
	$(KERNEL)/main.c \
	$(KERNEL)/lib/string.c \
	$(KERNEL)/lib/printf.c \
	$(KERNEL)/arch/x86_64/gdt.c \
	$(KERNEL)/arch/x86_64/idt.c \
	$(KERNEL)/arch/x86_64/pic.c \
	$(KERNEL)/arch/x86_64/pit.c \
	$(KERNEL)/arch/x86_64/cpu.c \
	$(KERNEL)/arch/x86_64/syscall.c \
	$(KERNEL)/mm/pmm.c \
	$(KERNEL)/mm/paging.c \
	$(KERNEL)/mm/vmm.c \
	$(KERNEL)/mm/heap.c \
	$(KERNEL)/drivers/serial.c \
	$(KERNEL)/drivers/framebuffer.c \
	$(KERNEL)/drivers/font.c \
	$(KERNEL)/drivers/keyboard.c \
	$(KERNEL)/drivers/mouse.c \
	$(KERNEL)/crypto/random.c \
	$(KERNEL)/crypto/sha256.c \
	$(KERNEL)/crypto/hmac.c \
	$(KERNEL)/crypto/aes.c \
	$(KERNEL)/cap/capability.c \
	$(KERNEL)/cap/cap_table.c \
	$(KERNEL)/db/database.c \
	$(KERNEL)/db/btree.c \
	$(KERNEL)/db/record.c \
	$(KERNEL)/db/record_serde.c \
	$(KERNEL)/db/transaction.c \
	$(KERNEL)/db/query.c \
	$(KERNEL)/proc/process.c \
	$(KERNEL)/proc/scheduler.c \
	$(KERNEL)/proc/ipc.c \
	$(VSHELL)/shell_main.c \
	$(VSHELL)/line_editor.c \
	$(VSHELL)/display.c \
	$(VSHELL)/tui.c \
	$(VSHELL)/history.c \
	$(VSHELL)/complete.c \
	$(VSHELL)/friendly.c \
	$(VSHELL)/script.c \
	$(GUI)/graphics.c \
	$(GUI)/event.c \
	$(GUI)/window.c \
	$(GUI)/compositor.c \
	$(GUI)/widgets.c \
	$(GUI)/desktop.c

# Kernel ASM sources
KERNEL_ASM_SRCS = \
	$(KERNEL)/arch/x86_64/entry.asm \
	$(KERNEL)/arch/x86_64/isr.asm \
	$(KERNEL)/arch/x86_64/context.asm \
	$(KERNEL)/arch/x86_64/syscall_entry.asm

# Object files
KERNEL_C_OBJS   = $(patsubst %.c, $(BUILD)/%.o, $(KERNEL_C_SRCS))
KERNEL_ASM_OBJS = $(patsubst %.asm, $(BUILD)/%.o, $(KERNEL_ASM_SRCS))
KERNEL_OBJS     = $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)

# Output
KERNEL_ELF   = $(BUILD)/VAULTOS.ELF
KERNEL_BIN   = $(BUILD)/VAULTOS.BIN
BOOTLOADER   = $(BUILD)/BOOTX64.EFI
DISK_IMG     = $(BUILD)/disk.img

# OVMF firmware
OVMF = third_party/ovmf/OVMF.fd

# ============ Targets ============

.PHONY: all kernel bootloader disk run debug clean dirs

all: dirs bootloader kernel disk

dirs:
	@mkdir -p $(BUILD)/$(KERNEL)/lib
	@mkdir -p $(BUILD)/$(KERNEL)/arch/x86_64
	@mkdir -p $(BUILD)/$(KERNEL)/mm
	@mkdir -p $(BUILD)/$(KERNEL)/drivers
	@mkdir -p $(BUILD)/$(KERNEL)/crypto
	@mkdir -p $(BUILD)/$(KERNEL)/cap
	@mkdir -p $(BUILD)/$(KERNEL)/db
	@mkdir -p $(BUILD)/$(KERNEL)/proc
	@mkdir -p $(BUILD)/$(VSHELL)
	@mkdir -p $(BUILD)/$(GUI)
	@mkdir -p $(BUILD)/$(BOOT)

# ============ UEFI Bootloader ============

bootloader: dirs $(BOOTLOADER)

$(BUILD)/$(BOOT)/bootloader.o: $(BOOT)/bootloader.c
	$(CC) $(BOOT_CFLAGS) -c $< -o $@

$(BUILD)/$(BOOT)/bootloader.so: $(BUILD)/$(BOOT)/bootloader.o
	$(LD) $(BOOT_LDFLAGS) $(EFI_CRT) $< -o $@ -lgnuefi -lefi

$(BOOTLOADER): $(BUILD)/$(BOOT)/bootloader.so
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	           -j .rel -j .rela -j .reloc \
	           --target efi-app-x86_64 $< $@
	@echo "=== Bootloader built: $@ ==="

# ============ Kernel ============

kernel: dirs $(KERNEL_BIN)

$(KERNEL_ELF): $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo "=== Kernel built: $@ ==="
	@ls -la $@

# C compilation (kernel sources only - exclude boot/)
$(BUILD)/$(KERNEL)/%.o: $(KERNEL)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(VSHELL)/%.o: $(VSHELL)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(GUI)/%.o: $(GUI)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# NASM assembly
$(BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(NASM) $(NASMFLAGS) $< -o $@

# ============ Disk Image ============

disk: $(KERNEL_BIN) $(BOOTLOADER)
	@echo "=== Creating disk image ==="
	dd if=/dev/zero of=$(DISK_IMG) bs=1M count=64 2>/dev/null
	mkfs.fat -F 32 $(DISK_IMG) >/dev/null
	mmd -i $(DISK_IMG) ::/EFI
	mmd -i $(DISK_IMG) ::/EFI/BOOT
	mcopy -i $(DISK_IMG) $(BOOTLOADER) ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $(DISK_IMG) $(KERNEL_BIN) ::/VAULTOS.BIN
	@echo "  Bootloader: OK"
	@echo "  Kernel: OK"
	@echo "=== Disk image created: $(DISK_IMG) ==="

# ============ Run ============

run: disk
	qemu-system-x86_64 \
		-cpu Haswell \
		-bios $(OVMF) \
		-drive file=$(DISK_IMG),format=raw \
		-m 256M \
		-serial stdio \
		-no-reboot \
		-no-shutdown

debug: disk
	qemu-system-x86_64 \
		-cpu Haswell \
		-bios $(OVMF) \
		-drive file=$(DISK_IMG),format=raw \
		-m 256M \
		-serial stdio \
		-no-reboot \
		-no-shutdown \
		-s -S

clean:
	rm -rf $(BUILD)
	@echo "=== Clean ==="

stats:
	@echo "=== VaultOS Source Statistics ==="
	@echo -n "C files: " && find . -name '*.c' -not -path './build/*' -not -path './third_party/*' | wc -l
	@echo -n "H files: " && find . -name '*.h' -not -path './build/*' -not -path './third_party/*' | wc -l
	@echo -n "ASM files: " && find . -name '*.asm' -not -path './build/*' | wc -l
	@echo -n "Total C LOC: " && find . -name '*.[ch]' -not -path './build/*' -not -path './third_party/*' | xargs wc -l | tail -1
