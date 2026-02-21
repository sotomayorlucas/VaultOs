#ifndef VAULTOS_SYSCALL_NUMBERS_H
#define VAULTOS_SYSCALL_NUMBERS_H

/* Database operations */
#define SYS_DB_QUERY     0
#define SYS_DB_INSERT    1
#define SYS_DB_DELETE    2
#define SYS_DB_UPDATE    3

/* Capability operations */
#define SYS_CAP_GRANT    10
#define SYS_CAP_REVOKE   11
#define SYS_CAP_DELEGATE 12
#define SYS_CAP_LIST     13

/* Process operations */
#define SYS_PROC_CREATE  20
#define SYS_PROC_EXIT    21
#define SYS_PROC_INFO    22

/* IPC operations */
#define SYS_IPC_SEND     30
#define SYS_IPC_RECV     31

/* I/O operations */
#define SYS_IO_READ      40   /* Read from keyboard buffer */
#define SYS_IO_WRITE     41   /* Write to framebuffer console */

/* System info */
#define SYS_INFO         50

#define SYS_MAX          51

/*
 * Calling convention (x86_64 SYSCALL):
 *   rax = syscall number
 *   rdi = arg1
 *   rsi = arg2
 *   rdx = arg3
 *   r10 = arg4
 *   r8  = arg5
 *   Return value in rax
 */

#endif /* VAULTOS_SYSCALL_NUMBERS_H */
