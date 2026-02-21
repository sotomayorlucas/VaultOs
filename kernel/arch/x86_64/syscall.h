#ifndef VAULTOS_SYSCALL_H
#define VAULTOS_SYSCALL_H

#include "../../lib/types.h"

/* Initialize SYSCALL/SYSRET via MSRs */
void syscall_init(void);

/* Syscall dispatch (called from assembly stub) */
int64_t syscall_dispatch(uint64_t num, uint64_t arg1, uint64_t arg2,
                          uint64_t arg3, uint64_t arg4, uint64_t arg5);

#endif /* VAULTOS_SYSCALL_H */
