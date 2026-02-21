#ifndef VAULTOS_IDT_H
#define VAULTOS_IDT_H

#include "../../lib/types.h"

/* IDT gate types */
#define IDT_INTERRUPT_GATE  0x8E  /* P=1, DPL=0, 64-bit interrupt gate */
#define IDT_TRAP_GATE       0x8F  /* P=1, DPL=0, 64-bit trap gate */
#define IDT_USER_GATE       0xEE  /* P=1, DPL=3, 64-bit interrupt gate */

#define IDT_ENTRIES 256

/* IDT entry (16 bytes each in long mode) */
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;          /* IST index (0 = don't use IST) */
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} PACKED idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} PACKED idt_descriptor_t;

/* Interrupt frame pushed by CPU */
typedef struct {
    /* Pushed by ISR stubs */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    /* Pushed by ISR stub (or CPU) */
    uint64_t vector;
    uint64_t error_code;
    /* Pushed by CPU on interrupt */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} PACKED interrupt_frame_t;

/* Interrupt handler function type */
typedef void (*irq_handler_t)(interrupt_frame_t *frame);

void idt_init(void);
void idt_set_entry(uint8_t vector, uint64_t handler, uint8_t ist, uint8_t type_attr);
void irq_register_handler(uint8_t irq, irq_handler_t handler);

#endif /* VAULTOS_IDT_H */
