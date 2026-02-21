#include "gdt.h"
#include "../../lib/string.h"

/*
 * GDT layout for VaultOS (x86_64):
 *   Entry 0: Null descriptor
 *   Entry 1: Kernel Code (CS=0x08) - 64-bit, DPL=0
 *   Entry 2: Kernel Data (SS=0x10) - DPL=0
 *   Entry 3: User Data   (SS=0x18) - DPL=3  (must come before user code for SYSRET)
 *   Entry 4: User Code   (CS=0x20) - 64-bit, DPL=3
 *   Entry 5-6: TSS descriptor (16 bytes, spans two entries)
 */

#define GDT_ENTRIES 7

static gdt_entry_t gdt[GDT_ENTRIES] ALIGNED(16);
tss_t kernel_tss ALIGNED(16);
static gdt_descriptor_t gdtr;

/* External assembly to load GDT and reload segments */
extern void gdt_load(uint64_t gdtr_ptr);

/* Encode a GDT entry */
static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                            uint8_t access, uint8_t flags) {
    gdt[index].limit_low       = limit & 0xFFFF;
    gdt[index].base_low        = base & 0xFFFF;
    gdt[index].base_mid        = (base >> 16) & 0xFF;
    gdt[index].access          = access;
    gdt[index].flags_limit_high = ((limit >> 16) & 0x0F) | (flags & 0xF0);
    gdt[index].base_high       = (base >> 24) & 0xFF;
}

void gdt_init(void) {
    memset(&gdt, 0, sizeof(gdt));
    memset(&kernel_tss, 0, sizeof(kernel_tss));

    /* Null */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Kernel Code: Present, DPL=0, Code, Read, Long mode */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);

    /* Kernel Data: Present, DPL=0, Data, Write */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);

    /* User Data: Present, DPL=3, Data, Write */
    gdt_set_entry(3, 0, 0xFFFFF, 0xF2, 0xC0);

    /* User Code: Present, DPL=3, Code, Read, Long mode */
    gdt_set_entry(4, 0, 0xFFFFF, 0xFA, 0xA0);

    /* TSS descriptor (16 bytes, occupies entries 5 and 6) */
    uint64_t tss_base = (uint64_t)&kernel_tss;
    uint32_t tss_limit = sizeof(tss_t) - 1;

    /* First 8 bytes of TSS descriptor */
    gdt[5].limit_low       = tss_limit & 0xFFFF;
    gdt[5].base_low        = tss_base & 0xFFFF;
    gdt[5].base_mid        = (tss_base >> 16) & 0xFF;
    gdt[5].access          = 0x89;  /* Present, TSS Available */
    gdt[5].flags_limit_high = ((tss_limit >> 16) & 0x0F);
    gdt[5].base_high       = (tss_base >> 24) & 0xFF;

    /* Second 8 bytes: upper 32 bits of base + reserved */
    uint32_t *tss_high = (uint32_t *)&gdt[6];
    tss_high[0] = (uint32_t)(tss_base >> 32);
    tss_high[1] = 0;

    /* Set IOMap base to end of TSS (no IO bitmap) */
    kernel_tss.iomap_base = sizeof(tss_t);

    /* Load GDT */
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint64_t)&gdt;
    gdt_load((uint64_t)&gdtr);
}

void gdt_set_tss_rsp0(uint64_t rsp0) {
    kernel_tss.rsp0 = rsp0;
}
