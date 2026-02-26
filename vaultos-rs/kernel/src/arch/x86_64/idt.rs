use core::mem;
use crate::arch::x86_64::cpu;
use crate::arch::x86_64::gdt::GDT_KERNEL_CODE;

// IDT gate types
const IDT_INTERRUPT_GATE: u8 = 0x8E;
const IDT_ENTRIES: usize = 256;

#[repr(C, packed)]
#[derive(Clone, Copy)]
struct IdtEntry {
    offset_low: u16,
    selector: u16,
    ist: u8,
    type_attr: u8,
    offset_mid: u16,
    offset_high: u32,
    reserved: u32,
}

impl IdtEntry {
    const fn zero() -> Self {
        Self {
            offset_low: 0,
            selector: 0,
            ist: 0,
            type_attr: 0,
            offset_mid: 0,
            offset_high: 0,
            reserved: 0,
        }
    }
}

#[repr(C, packed)]
struct IdtDescriptor {
    limit: u16,
    base: u64,
}

/// Interrupt frame pushed by ISR stubs + CPU.
/// Must match isr.asm push order exactly.
#[repr(C)]
#[derive(Debug)]
pub struct InterruptFrame {
    // Pushed by ISR stubs (in reverse order)
    pub r15: u64, pub r14: u64, pub r13: u64, pub r12: u64,
    pub r11: u64, pub r10: u64, pub r9: u64, pub r8: u64,
    pub rbp: u64, pub rdi: u64, pub rsi: u64, pub rdx: u64,
    pub rcx: u64, pub rbx: u64, pub rax: u64,
    // Pushed by ISR stub
    pub vector: u64,
    pub error_code: u64,
    // Pushed by CPU
    pub rip: u64,
    pub cs: u64,
    pub rflags: u64,
    pub rsp: u64,
    pub ss: u64,
}

pub type IrqHandler = fn(&mut InterruptFrame);

#[repr(C, align(16))]
struct IdtTable {
    entries: [IdtEntry; IDT_ENTRIES],
}

static mut IDT: IdtTable = IdtTable {
    entries: [IdtEntry::zero(); IDT_ENTRIES],
};

static mut IDTR: IdtDescriptor = IdtDescriptor { limit: 0, base: 0 };
static mut IRQ_HANDLERS: [Option<IrqHandler>; 16] = [None; 16];

// ISR stubs from isr.asm
extern "C" {
    fn isr_stub_0(); fn isr_stub_1(); fn isr_stub_2(); fn isr_stub_3();
    fn isr_stub_4(); fn isr_stub_5(); fn isr_stub_6(); fn isr_stub_7();
    fn isr_stub_8(); fn isr_stub_9(); fn isr_stub_10(); fn isr_stub_11();
    fn isr_stub_12(); fn isr_stub_13(); fn isr_stub_14(); fn isr_stub_15();
    fn isr_stub_16(); fn isr_stub_17(); fn isr_stub_18(); fn isr_stub_19();
    fn isr_stub_20(); fn isr_stub_21(); fn isr_stub_22(); fn isr_stub_23();
    fn isr_stub_24(); fn isr_stub_25(); fn isr_stub_26(); fn isr_stub_27();
    fn isr_stub_28(); fn isr_stub_29(); fn isr_stub_30(); fn isr_stub_31();
    fn isr_stub_32(); fn isr_stub_33(); fn isr_stub_34(); fn isr_stub_35();
    fn isr_stub_36(); fn isr_stub_37(); fn isr_stub_38(); fn isr_stub_39();
    fn isr_stub_40(); fn isr_stub_41(); fn isr_stub_42(); fn isr_stub_43();
    fn isr_stub_44(); fn isr_stub_45(); fn isr_stub_46(); fn isr_stub_47();
    fn unhandled_interrupt_stub();
}

fn get_isr_stubs() -> [unsafe extern "C" fn(); 48] {
    [
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
    ]
}

fn idt_set_entry(vector: u8, handler: u64, ist: u8, type_attr: u8) {
    unsafe {
        let i = vector as usize;
        IDT.entries[i].offset_low = (handler & 0xFFFF) as u16;
        IDT.entries[i].selector = GDT_KERNEL_CODE;
        IDT.entries[i].ist = ist & 0x7;
        IDT.entries[i].type_attr = type_attr;
        IDT.entries[i].offset_mid = ((handler >> 16) & 0xFFFF) as u16;
        IDT.entries[i].offset_high = ((handler >> 32) & 0xFFFFFFFF) as u32;
        IDT.entries[i].reserved = 0;
    }
}

pub fn idt_init() {
    let stubs = get_isr_stubs();

    // CPU exceptions (0-31) and IRQs (32-47)
    for i in 0..48 {
        idt_set_entry(i as u8, stubs[i] as u64, 0, IDT_INTERRUPT_GATE);
    }

    // Fill remaining entries (48-255) with catch-all handler to prevent triple faults
    let catch_all = unhandled_interrupt_stub as *const () as u64;
    for i in 48..=255u16 {
        idt_set_entry(i as u8, catch_all, 0, IDT_INTERRUPT_GATE);
    }

    unsafe {
        IDTR.limit = (mem::size_of::<[IdtEntry; IDT_ENTRIES]>() - 1) as u16;
        IDTR.base = IDT.entries.as_ptr() as u64;
        core::arch::asm!("lidt [{}]", in(reg) &IDTR, options(nostack, preserves_flags));
    }
}

pub fn irq_register_handler(irq: u8, handler: IrqHandler) {
    if (irq as usize) < 16 {
        unsafe {
            IRQ_HANDLERS[irq as usize] = Some(handler);
        }
    }
}

static EXCEPTION_NAMES: [&str; 32] = [
    "Division Error", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound Range", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 FP Exception", "Alignment Check", "Machine Check", "SIMD FP Exception",
    "Virtualization Exception", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security Exception", "Reserved",
];

/// Called from isr_common_stub in isr.asm
#[no_mangle]
pub extern "C" fn isr_handler(frame: *mut InterruptFrame) {
    let frame = unsafe { &mut *frame };
    let vector = frame.vector;

    if vector < 32 {
        // CPU exception
        let from_user = (frame.cs & 3) != 0;

        if from_user {
            // Ring 3 fault — will be handled properly when process mgmt is implemented
            unsafe {
                crate::drivers::serial::serial_print("[FAULT] User exception: ");
                crate::drivers::serial::serial_print(EXCEPTION_NAMES[vector as usize]);
                crate::drivers::serial::serial_putchar(b'\n');
            }
            return;
        }

        // Kernel panic
        unsafe {
            crate::drivers::serial::serial_print("\n!!! EXCEPTION: ");
            crate::drivers::serial::serial_print(EXCEPTION_NAMES[vector as usize]);
            crate::drivers::serial::serial_print(" !!!\nRIP: ");
            crate::drivers::serial::serial_print_hex(frame.rip);
            crate::drivers::serial::serial_print("\nError code: ");
            crate::drivers::serial::serial_print_hex(frame.error_code);
            crate::drivers::serial::serial_print("\nRAX: ");
            crate::drivers::serial::serial_print_hex(frame.rax);
            crate::drivers::serial::serial_print("  RDI: ");
            crate::drivers::serial::serial_print_hex(frame.rdi);
            crate::drivers::serial::serial_print("\nRSP: ");
            crate::drivers::serial::serial_print_hex(frame.rsp);
            crate::drivers::serial::serial_print("  RBP: ");
            crate::drivers::serial::serial_print_hex(frame.rbp);
            if vector == 14 {
                crate::drivers::serial::serial_print("\nCR2: ");
                crate::drivers::serial::serial_print_hex(cpu::read_cr2());
            }
            crate::drivers::serial::serial_print("\nSystem halted.\n");
            cpu::cli();
            loop { cpu::hlt(); }
        }
    } else if vector >= 32 && vector < 48 {
        // Hardware IRQ
        let irq = (vector - 32) as u8;
        unsafe {
            if let Some(handler) = IRQ_HANDLERS[irq as usize] {
                handler(frame);
            }
        }
    } else {
        // Unhandled vector (48-255) — send EOI if it might be a spurious IRQ
        unsafe {
            crate::drivers::serial::serial_print("[IDT] Unhandled vector: ");
            crate::drivers::serial::serial_print_hex(vector);
            crate::drivers::serial::serial_putchar(b'\n');
        }
    }
}
