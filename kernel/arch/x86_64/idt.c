#include "idt.h"
#include "gdt.h"
#include "cpu.h"
#include "../../lib/printf.h"
#include "../../lib/string.h"

static idt_entry_t idt[IDT_ENTRIES] ALIGNED(16);
static idt_descriptor_t idtr;
static irq_handler_t irq_handlers[16];

/* ISR stubs defined in isr.asm */
extern void isr_stub_0(void);
extern void isr_stub_1(void);
extern void isr_stub_2(void);
extern void isr_stub_3(void);
extern void isr_stub_4(void);
extern void isr_stub_5(void);
extern void isr_stub_6(void);
extern void isr_stub_7(void);
extern void isr_stub_8(void);
extern void isr_stub_9(void);
extern void isr_stub_10(void);
extern void isr_stub_11(void);
extern void isr_stub_12(void);
extern void isr_stub_13(void);
extern void isr_stub_14(void);
extern void isr_stub_15(void);
extern void isr_stub_16(void);
extern void isr_stub_17(void);
extern void isr_stub_18(void);
extern void isr_stub_19(void);
extern void isr_stub_20(void);
extern void isr_stub_21(void);
extern void isr_stub_22(void);
extern void isr_stub_23(void);
extern void isr_stub_24(void);
extern void isr_stub_25(void);
extern void isr_stub_26(void);
extern void isr_stub_27(void);
extern void isr_stub_28(void);
extern void isr_stub_29(void);
extern void isr_stub_30(void);
extern void isr_stub_31(void);
/* IRQs 0-15 -> vectors 32-47 */
extern void isr_stub_32(void);
extern void isr_stub_33(void);
extern void isr_stub_34(void);
extern void isr_stub_35(void);
extern void isr_stub_36(void);
extern void isr_stub_37(void);
extern void isr_stub_38(void);
extern void isr_stub_39(void);
extern void isr_stub_40(void);
extern void isr_stub_41(void);
extern void isr_stub_42(void);
extern void isr_stub_43(void);
extern void isr_stub_44(void);
extern void isr_stub_45(void);
extern void isr_stub_46(void);
extern void isr_stub_47(void);

typedef void (*isr_stub_fn)(void);
static isr_stub_fn isr_stubs[48] = {
    isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3,
    isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7,
    isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11,
    isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15,
    isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19,
    isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23,
    isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27,
    isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31,
    isr_stub_32, isr_stub_33, isr_stub_34, isr_stub_35,
    isr_stub_36, isr_stub_37, isr_stub_38, isr_stub_39,
    isr_stub_40, isr_stub_41, isr_stub_42, isr_stub_43,
    isr_stub_44, isr_stub_45, isr_stub_46, isr_stub_47,
};

static const char *exception_names[] = {
    "Division Error", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound Range", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 FP Exception", "Alignment Check", "Machine Check", "SIMD FP Exception",
    "Virtualization Exception", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security Exception", "Reserved"
};

void idt_set_entry(uint8_t vector, uint64_t handler, uint8_t ist, uint8_t type_attr) {
    idt[vector].offset_low  = handler & 0xFFFF;
    idt[vector].selector    = GDT_KERNEL_CODE;
    idt[vector].ist         = ist & 0x7;
    idt[vector].type_attr   = type_attr;
    idt[vector].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[vector].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[vector].reserved    = 0;
}

void idt_init(void) {
    memset(idt, 0, sizeof(idt));
    memset(irq_handlers, 0, sizeof(irq_handlers));

    /* CPU exceptions (vectors 0-31) */
    for (int i = 0; i < 32; i++) {
        idt_set_entry(i, (uint64_t)isr_stubs[i], 0, IDT_INTERRUPT_GATE);
    }

    /* Hardware IRQs (vectors 32-47) */
    for (int i = 32; i < 48; i++) {
        idt_set_entry(i, (uint64_t)isr_stubs[i], 0, IDT_INTERRUPT_GATE);
    }

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)&idt;
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}

void irq_register_handler(uint8_t irq, irq_handler_t handler) {
    if (irq < 16) {
        irq_handlers[irq] = handler;
    }
}

/* Called from isr_common_stub in isr.asm */
void isr_handler(interrupt_frame_t *frame) {
    uint64_t vector = frame->vector;

    if (vector < 32) {
        /* CPU exception */
        kprintf("\n!!! EXCEPTION: %s (vector %llu)\n", exception_names[vector], vector);
        kprintf("    Error code: 0x%016llx\n", frame->error_code);
        kprintf("    RIP: 0x%016llx  CS: 0x%04llx\n", frame->rip, frame->cs);
        kprintf("    RSP: 0x%016llx  SS: 0x%04llx\n", frame->rsp, frame->ss);
        kprintf("    RFLAGS: 0x%016llx\n", frame->rflags);
        kprintf("    RAX: 0x%016llx  RBX: 0x%016llx\n", frame->rax, frame->rbx);
        kprintf("    RCX: 0x%016llx  RDX: 0x%016llx\n", frame->rcx, frame->rdx);
        kprintf("    RSI: 0x%016llx  RDI: 0x%016llx\n", frame->rsi, frame->rdi);
        kprintf("    RBP: 0x%016llx\n", frame->rbp);

        if (vector == 14) {
            kprintf("    CR2 (fault addr): 0x%016llx\n", read_cr2());
        }

        /* Halt on fatal exceptions */
        kprintf("System halted.\n");
        cli();
        for (;;) hlt();
    } else if (vector >= 32 && vector < 48) {
        /* Hardware IRQ */
        uint8_t irq = (uint8_t)(vector - 32);
        if (irq_handlers[irq]) {
            irq_handlers[irq](frame);
        }
        /* Send EOI handled by PIC module */
    }
}
