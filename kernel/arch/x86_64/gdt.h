#ifndef VAULTOS_GDT_H
#define VAULTOS_GDT_H

#include "../../lib/types.h"

/* GDT segment selectors */
#define GDT_NULL         0x00
#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10
#define GDT_USER_DATA    0x18
#define GDT_USER_CODE    0x20
#define GDT_TSS          0x28

/* GDT entry */
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit_high;
    uint8_t  base_high;
} PACKED gdt_entry_t;

/* TSS - Task State Segment */
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;       /* Ring 0 stack pointer */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];     /* Interrupt Stack Table entries */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} PACKED tss_t;

/* GDT descriptor (GDTR value) */
typedef struct {
    uint16_t limit;
    uint64_t base;
} PACKED gdt_descriptor_t;

void gdt_init(void);
void gdt_set_tss_rsp0(uint64_t rsp0);

extern tss_t kernel_tss;

#endif /* VAULTOS_GDT_H */
