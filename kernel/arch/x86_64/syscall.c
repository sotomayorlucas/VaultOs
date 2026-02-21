#include "syscall.h"
#include "cpu.h"
#include "gdt.h"
#include "../../../include/vaultos/syscall_numbers.h"
#include "../../../include/vaultos/error_codes.h"
#include "../../lib/printf.h"

/* Syscall entry point (assembly) */
extern void syscall_entry(void);

void syscall_init(void) {
    /* Enable SCE (System Call Extensions) in EFER MSR */
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);

    /*
     * STAR MSR layout:
     *   [31:0]  = Reserved (EIP for 32-bit SYSCALL, not used in 64-bit)
     *   [47:32] = Kernel CS selector (SYSCALL loads CS from here, SS = CS + 8)
     *   [63:48] = User CS selector base (SYSRET loads CS from here + 16, SS from here + 8)
     *
     * For our GDT: Kernel CS=0x08, Kernel SS=0x10
     *              User Data=0x18, User Code=0x20
     * SYSRET: CS = [63:48] + 16 = 0x18 + 16 = 0x28? No.
     *
     * Actually SYSRET in 64-bit mode:
     *   CS = STAR[63:48] + 16 (so for user CS=0x20, we need STAR[63:48] = 0x10)
     *   SS = STAR[63:48] + 8  (so SS = 0x18 = user data, correct)
     */
    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)GDT_KERNEL_CODE << 32);
    wrmsr(MSR_STAR, star);

    /* LSTAR = address of syscall entry point */
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* SFMASK = mask IF (bit 9) on SYSCALL entry to disable interrupts */
    wrmsr(MSR_SFMASK, 0x200);

    kprintf("[SYSCALL] Initialized (STAR=0x%016llx, LSTAR=0x%p)\n",
            star, (void *)syscall_entry);
}

/* Forward declarations for syscall handlers */
extern int64_t sys_db_query(uint64_t query_str, uint64_t result_buf, uint64_t buf_size);
extern int64_t sys_db_insert(uint64_t table_id, uint64_t record_ptr);
extern int64_t sys_db_delete(uint64_t table_id, uint64_t row_id);
extern int64_t sys_db_update(uint64_t table_id, uint64_t row_id, uint64_t record_ptr);
extern int64_t sys_io_write(uint64_t buf, uint64_t len);
extern int64_t sys_io_read(uint64_t buf, uint64_t len);

int64_t syscall_dispatch(uint64_t num, uint64_t arg1, uint64_t arg2,
                          uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    (void)arg4;
    (void)arg5;

    switch (num) {
    case SYS_DB_QUERY:
        return sys_db_query(arg1, arg2, arg3);
    case SYS_DB_INSERT:
        return sys_db_insert(arg1, arg2);
    case SYS_DB_DELETE:
        return sys_db_delete(arg1, arg2);
    case SYS_DB_UPDATE:
        return sys_db_update(arg1, arg2, arg3);
    case SYS_IO_WRITE:
        return sys_io_write(arg1, arg2);
    case SYS_IO_READ:
        return sys_io_read(arg1, arg2);
    case SYS_PROC_EXIT:
        /* TODO: implement process exit */
        return VOS_OK;
    case SYS_INFO:
        return VOS_OK;
    default:
        kprintf("[SYSCALL] Unknown syscall %llu\n", num);
        return VOS_ERR_NOSYS;
    }
}
