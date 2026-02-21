/*
 * VaultOS Kernel Linker Script
 * Higher-half kernel mapped at virtual address 0xFFFFFFFF80000000
 * Loaded at physical address 0x100000 (1 MiB)
 */

ENTRY(_start)

KERNEL_VBASE = 0xFFFFFFFF80000000;
KERNEL_PHYS  = 0x100000;

SECTIONS
{
    . = KERNEL_PHYS;

    .text : AT(KERNEL_PHYS)
    {
        _kernel_start = .;
        *(.text.entry)
        *(.text .text.*)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4096)
    {
        *(.data .data.*)
    }

    .bss : ALIGN(4096)
    {
        _bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        _bss_end = .;
    }

    _kernel_end = .;
    _kernel_size = _kernel_end - _kernel_start;

    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
