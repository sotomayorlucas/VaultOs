; VaultOS - Context switch
; void context_switch(context_t *current, context_t *next)
;   rdi = pointer to current context (save here)
;   rsi = pointer to next context (restore from here)

[bits 64]
[global context_switch]

context_switch:
    ; Save callee-saved registers into current context
    mov [rdi + 0],  rsp
    mov [rdi + 8],  rbp
    mov [rdi + 16], rbx
    mov [rdi + 24], r12
    mov [rdi + 32], r13
    mov [rdi + 40], r14
    mov [rdi + 48], r15

    ; Save rflags
    pushfq
    pop qword [rdi + 56]

    ; Save return address (rip)
    lea rax, [rel .return_point]
    mov [rdi + 64], rax

    ; Save CR3
    mov rax, cr3
    mov [rdi + 72], rax

    ; Restore next context
    ; Switch page tables if different
    mov rax, [rsi + 72]     ; next CR3
    mov rcx, cr3
    cmp rax, rcx
    je .same_cr3
    mov cr3, rax
.same_cr3:

    ; Restore rflags
    push qword [rsi + 56]
    popfq

    ; Restore callee-saved registers
    mov r15, [rsi + 48]
    mov r14, [rsi + 40]
    mov r13, [rsi + 32]
    mov r12, [rsi + 24]
    mov rbx, [rsi + 16]
    mov rbp, [rsi + 8]
    mov rsp, [rsi + 0]

    ; Jump to saved rip
    jmp [rsi + 64]

.return_point:
    ret
