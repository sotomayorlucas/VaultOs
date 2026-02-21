; VaultOS - Kernel entry point
; First code to execute after bootloader jumps to kernel.
; Zeroes BSS, sets up stack, and calls kernel_main.

[bits 64]

[extern kernel_main]
[extern _bss_start]
[extern _bss_end]

section .text.entry
[global _start]
_start:
    ; RDI = pointer to BootInfo (passed by bootloader, preserve it)

    ; Zero BSS section
    push rdi                ; Save BootInfo pointer
    mov rdi, _bss_start
    mov rcx, _bss_end
    sub rcx, rdi
    shr rcx, 3              ; Count in qwords
    xor rax, rax
    rep stosq
    pop rdi                 ; Restore BootInfo pointer

    ; Set up a kernel stack (16 KiB, 16-byte aligned)
    lea rsp, [rel _kernel_stack_top]

    ; Call kernel_main(BootInfo *)
    call kernel_main

    ; Should never return
.halt:
    cli
    hlt
    jmp .halt

section .bss
align 16
_kernel_stack_bottom:
    resb 16384              ; 16 KiB kernel stack
_kernel_stack_top:
