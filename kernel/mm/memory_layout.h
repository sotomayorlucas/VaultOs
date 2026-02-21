#ifndef VAULTOS_MEMORY_LAYOUT_H
#define VAULTOS_MEMORY_LAYOUT_H

/*
 * VaultOS Virtual Memory Layout (x86_64, 48-bit canonical)
 *
 * Higher-half kernel layout:
 *   0xFFFFFFFF80000000 - Kernel code/data (loaded at phys 0x100000)
 *   0xFFFFFFFF82000000 - Kernel heap (256 MiB)
 *   0xFFFFFFFF92000000 - Physical memory direct map (up to 4 GiB)
 *   0xFFFFFFFFC0000000 - Framebuffer mapping
 *   0xFFFFFFFFD0000000 - Database B-Tree pool (256 MiB)
 *
 * User space:
 *   0x0000000000200000 - Process code
 *   0x00007FFFFFFFE000 - Process stack top
 */

#define KERNEL_VBASE        0xFFFFFFFF80000000ULL
#define KERNEL_HEAP_BASE    0xFFFFFFFF82000000ULL
#define KERNEL_HEAP_SIZE    (256ULL * 1024 * 1024)   /* 256 MiB */

#define PHYS_MAP_BASE       0xFFFFFFFF92000000ULL
#define PHYS_MAP_SIZE       (4ULL * 1024 * 1024 * 1024) /* 4 GiB */

#define FRAMEBUF_VBASE      0xFFFFFFFFC0000000ULL

#define DB_POOL_VBASE       0xFFFFFFFFD0000000ULL
#define DB_POOL_SIZE        (256ULL * 1024 * 1024)

/* User space */
#define USER_CODE_BASE      0x0000000000200000ULL
#define USER_STACK_TOP      0x00007FFFFFFFE000ULL
#define USER_STACK_SIZE     (64 * 1024)              /* 64 KiB */

/* Kernel physical load address */
#define KERNEL_PHYS_BASE    0x100000ULL              /* 1 MiB */

/* Helper macros */
#define PHYS_TO_VIRT(phys)  ((void *)((uint64_t)(phys) + PHYS_MAP_BASE))
#define VIRT_TO_PHYS(virt)  ((uint64_t)(virt) - PHYS_MAP_BASE)

#endif /* VAULTOS_MEMORY_LAYOUT_H */
