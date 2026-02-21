; VaultOS - SYSCALL entry point
; On SYSCALL: RCX = user RIP, R11 = user RFLAGS, RSP = user RSP (unchanged!)
; Convention: RAX = syscall num, RDI = arg1, RSI = arg2, RDX = arg3, R10 = arg4, R8 = arg5

[bits 64]
[extern syscall_dispatch]

[global syscall_entry]
syscall_entry:
    ; Swap to kernel stack (use TSS RSP0)
    ; Save user RSP in R11 is not available (it has RFLAGS), use a scratch area
    ; Actually: RCX = user RIP, R11 = user RFLAGS
    ; We need to save user RSP somewhere

    ; Use swapgs to get kernel data if needed (MVP: skip, we're in Ring 0)
    ; For MVP where shell runs in Ring 0, SYSCALL won't actually be used
    ; This is prepared for future Ring 3 support

    ; Save user stack
    mov [rel user_rsp], rsp

    ; Load kernel stack from TSS (simplified for MVP)
    ; In real implementation, read from TSS or per-CPU data
    ; For now, use a static kernel stack
    mov rsp, [rel kernel_rsp]

    ; Save callee-saved registers
    push rcx            ; User RIP
    push r11            ; User RFLAGS
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    ; Setup arguments for syscall_dispatch
    ; RAX already has syscall number
    ; RDI = arg1 (already there)
    ; RSI = arg2 (already there)
    ; RDX = arg3 (already there)
    ; R10 = arg4 -> move to RCX (C calling convention)
    ; R8  = arg5 (already there)
    mov rcx, r10

    ; Preserve RAX across the call setup
    ; Move syscall number to RDI, shift args
    ; Actually: syscall_dispatch(num, arg1, arg2, arg3, arg4, arg5)
    ; So we need: RDI=num, RSI=arg1, RDX=arg2, RCX=arg3, R8=arg4, R9=arg5
    push rdi            ; Save original arg1
    push rsi            ; Save original arg2
    push rdx            ; Save original arg3

    mov rdi, rax        ; num
    pop rcx             ; arg3 (was rdx)
    pop rdx             ; arg2 (was rsi)
    ; arg1 is now on stack
    mov r9, r8          ; arg5
    mov r8, r10         ; arg4
    pop rsi             ; arg1 (was rdi)

    ; Align stack for C call
    and rsp, ~0xF

    call syscall_dispatch

    ; RAX now has return value

    ; Restore callee-saved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    pop r11             ; User RFLAGS
    pop rcx             ; User RIP

    ; Restore user stack
    mov rsp, [rel user_rsp]

    ; Return to user
    o64 sysret

section .data
align 8
user_rsp:   dq 0
kernel_rsp: dq 0       ; Set during init

[global syscall_set_kernel_stack]
syscall_set_kernel_stack:
    mov [rel kernel_rsp], rdi
    ret
