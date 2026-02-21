; VaultOS - Interrupt Service Routine stubs
; x86_64 NASM assembly

[bits 64]
[extern isr_handler]

; Macro for ISR without error code (push dummy 0)
%macro ISR_NOERR 1
[global isr_stub_%1]
isr_stub_%1:
    push qword 0            ; Dummy error code
    push qword %1           ; Vector number
    jmp isr_common_stub
%endmacro

; Macro for ISR with error code (CPU pushes it)
%macro ISR_ERR 1
[global isr_stub_%1]
isr_stub_%1:
    push qword %1           ; Vector number
    jmp isr_common_stub
%endmacro

; CPU exceptions
ISR_NOERR 0    ; Division Error
ISR_NOERR 1    ; Debug
ISR_NOERR 2    ; NMI
ISR_NOERR 3    ; Breakpoint
ISR_NOERR 4    ; Overflow
ISR_NOERR 5    ; Bound Range
ISR_NOERR 6    ; Invalid Opcode
ISR_NOERR 7    ; Device Not Available
ISR_ERR   8    ; Double Fault
ISR_NOERR 9    ; Coprocessor Segment Overrun
ISR_ERR   10   ; Invalid TSS
ISR_ERR   11   ; Segment Not Present
ISR_ERR   12   ; Stack-Segment Fault
ISR_ERR   13   ; General Protection Fault
ISR_ERR   14   ; Page Fault
ISR_NOERR 15   ; Reserved
ISR_NOERR 16   ; x87 FP Exception
ISR_ERR   17   ; Alignment Check
ISR_NOERR 18   ; Machine Check
ISR_NOERR 19   ; SIMD FP Exception
ISR_NOERR 20   ; Virtualization Exception
ISR_ERR   21   ; Control Protection
ISR_NOERR 22   ; Reserved
ISR_NOERR 23   ; Reserved
ISR_NOERR 24   ; Reserved
ISR_NOERR 25   ; Reserved
ISR_NOERR 26   ; Reserved
ISR_NOERR 27   ; Reserved
ISR_NOERR 28   ; Hypervisor Injection
ISR_ERR   29   ; VMM Communication
ISR_ERR   30   ; Security Exception
ISR_NOERR 31   ; Reserved

; Hardware IRQs (vectors 32-47)
ISR_NOERR 32   ; IRQ 0  - PIT Timer
ISR_NOERR 33   ; IRQ 1  - Keyboard
ISR_NOERR 34   ; IRQ 2  - Cascade
ISR_NOERR 35   ; IRQ 3  - COM2
ISR_NOERR 36   ; IRQ 4  - COM1
ISR_NOERR 37   ; IRQ 5  - LPT2
ISR_NOERR 38   ; IRQ 6  - Floppy
ISR_NOERR 39   ; IRQ 7  - LPT1 / Spurious
ISR_NOERR 40   ; IRQ 8  - RTC
ISR_NOERR 41   ; IRQ 9
ISR_NOERR 42   ; IRQ 10
ISR_NOERR 43   ; IRQ 11
ISR_NOERR 44   ; IRQ 12 - PS/2 Mouse
ISR_NOERR 45   ; IRQ 13 - FPU
ISR_NOERR 46   ; IRQ 14 - ATA Primary
ISR_NOERR 47   ; IRQ 15 - ATA Secondary

; Common ISR stub: save all registers, call C handler, restore, iretq
isr_common_stub:
    ; Save all general-purpose registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Pass pointer to interrupt_frame_t as argument (rdi)
    mov rdi, rsp
    ; Ensure 16-byte stack alignment for C call
    and rsp, ~0xF
    call isr_handler

    ; Restore stack and registers
    mov rsp, rdi

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16             ; Remove vector and error code
    iretq

; GDT load helper
[global gdt_load]
gdt_load:
    lgdt [rdi]              ; Load GDT from pointer in rdi

    ; Reload code segment via far return
    push qword 0x08         ; Kernel code segment
    lea rax, [rel .reload_cs]
    push rax
    retfq

.reload_cs:
    ; Reload data segments
    mov ax, 0x10            ; Kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Load TSS
    mov ax, 0x28            ; TSS selector
    ltr ax

    ret
