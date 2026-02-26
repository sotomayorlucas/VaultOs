// Process management: creation, exit, lookup.

use crate::mm::vmm;
use crate::mm::pmm;
use crate::mm::paging::*;
use crate::mm::layout::*;
use crate::mm::heap;
use crate::cap;
use crate::arch::x86_64::gdt;
use vaultos_shared::capability_types::*;

pub const MAX_PROCESSES: usize = 64;
pub const PROC_STACK_SIZE: usize = 64 * 1024; // 64 KiB

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ProcState {
    Free = 0,
    Ready,
    Running,
    Blocked,
    Terminated,
}

/// CPU context saved/restored on context switch (matches context.asm layout).
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Context {
    pub rsp: u64,
    pub rbp: u64,
    pub rbx: u64,
    pub r12: u64,
    pub r13: u64,
    pub r14: u64,
    pub r15: u64,
    pub rflags: u64,
    pub rip: u64,
    pub cr3: u64,
}

impl Context {
    pub const fn zero() -> Self {
        Context {
            rsp: 0, rbp: 0, rbx: 0,
            r12: 0, r13: 0, r14: 0, r15: 0,
            rflags: 0, rip: 0, cr3: 0,
        }
    }
}

pub struct Process {
    pub pid: u64,
    pub name: [u8; 64],
    pub state: ProcState,
    pub priority: u8,
    pub context: Context,
    pub page_table: u64,
    pub stack_base: u64,       // physical base of kernel stack allocation
    pub stack_virt: u64,       // virtual pointer returned by alloc
    pub entry_point: u64,
    pub cap_root: u64,
    pub is_user: bool,
    pub user_stack_base: u64,
}

impl Process {
    pub const fn empty() -> Self {
        Process {
            pid: 0,
            name: [0u8; 64],
            state: ProcState::Free,
            priority: 0,
            context: Context::zero(),
            page_table: 0,
            stack_base: 0,
            stack_virt: 0,
            entry_point: 0,
            cap_root: 0,
            is_user: false,
            user_stack_base: 0,
        }
    }
}

// ---------------------------------------------------------------------------
// Static process table
// ---------------------------------------------------------------------------

static mut PROCESSES: [Process; MAX_PROCESSES] = {
    const EMPTY: Process = Process::empty();
    [EMPTY; MAX_PROCESSES]
};

static mut NEXT_PID: u64 = 1;

extern "C" {
    fn context_switch(current: *mut Context, next: *const Context);
    fn enter_usermode(entry: u64, user_stack: u64);
}

// ---------------------------------------------------------------------------
// Process creation
// ---------------------------------------------------------------------------

fn set_name(proc: &mut Process, name: &str) {
    let bytes = name.as_bytes();
    let len = if bytes.len() > 63 { 63 } else { bytes.len() };
    proc.name[..len].copy_from_slice(&bytes[..len]);
    proc.name[len] = 0;
}

/// Find a free slot in the process table.
fn alloc_slot() -> Option<usize> {
    unsafe {
        for i in 0..MAX_PROCESSES {
            if PROCESSES[i].state == ProcState::Free {
                return Some(i);
            }
        }
    }
    None
}

/// Create a kernel-mode process.
pub fn process_create(name: &str, entry: u64) -> Option<&'static mut Process> {
    let slot = alloc_slot()?;

    // Allocate kernel stack via heap
    // SAFETY: PROC_STACK_SIZE (64 KiB) is non-zero and 16 is a valid power-of-two alignment
    let stack = unsafe {
        alloc::alloc::alloc(
            alloc::alloc::Layout::from_size_align(PROC_STACK_SIZE, 16).unwrap()
        )
    };
    if stack.is_null() {
        return None;
    }
    let stack_virt = stack as u64;
    let stack_top = stack_virt + PROC_STACK_SIZE as u64 - 8;

    let pid = unsafe {
        let id = NEXT_PID;
        NEXT_PID += 1;
        id
    };

    unsafe {
        let proc = &mut PROCESSES[slot];
        proc.pid = pid;
        set_name(proc, name);
        proc.state = ProcState::Ready;
        proc.priority = 1;
        proc.is_user = false;
        proc.stack_virt = stack_virt;
        proc.stack_base = stack_virt;
        proc.entry_point = entry;
        proc.page_table = vmm::vmm_get_kernel_pml4();

        proc.context = Context {
            rsp: stack_top,
            rbp: stack_top + 8,
            rbx: 0,
            r12: 0,
            r13: 0,
            r14: 0,
            r15: 0,
            rflags: 0x202, // IF set
            rip: entry,
            cr3: vmm::vmm_get_kernel_pml4(),
        };

        // Create root capability
        let root_cap = cap::cap_create(pid, CapObjectType::Process, pid, CAP_ALL, 0);
        cap::cap_table_insert(&root_cap);
        proc.cap_root = root_cap.cap_id;

        Some(proc)
    }
}

/// Create a user-mode (Ring 3) process.
pub fn process_create_user(name: &str, entry: u64) -> Option<&'static mut Process> {
    let slot = alloc_slot()?;

    // Allocate kernel stack
    // SAFETY: PROC_STACK_SIZE (64 KiB) is non-zero and 16 is a valid power-of-two alignment
    let stack = unsafe {
        alloc::alloc::alloc(
            alloc::alloc::Layout::from_size_align(PROC_STACK_SIZE, 16).unwrap()
        )
    };
    if stack.is_null() {
        return None;
    }
    let stack_virt = stack as u64;
    let stack_top = stack_virt + PROC_STACK_SIZE as u64 - 8;

    // Create user address space
    let user_pml4 = vmm::vmm_create_user_space();
    if user_pml4 == 0 {
        // SAFETY: same Layout as the alloc above — infallible for these constants
        unsafe {
            alloc::alloc::dealloc(
                stack,
                alloc::alloc::Layout::from_size_align(PROC_STACK_SIZE, 16).unwrap(),
            );
        }
        return None;
    }

    // Map user stack pages
    let user_stack_pages = USER_STACK_SIZE / PAGE_SIZE;
    let user_stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;
    for i in 0..user_stack_pages {
        let phys = pmm::pmm_alloc_page();
        if phys == 0 {
            vmm::vmm_destroy_user_space(user_pml4);
            return None;
        }
        paging_map_page(
            user_pml4,
            user_stack_bottom + i * PAGE_SIZE,
            phys,
            PTE_PRESENT | PTE_USER | PTE_WRITABLE | PTE_NX,
        );
    }

    let pid = unsafe {
        let id = NEXT_PID;
        NEXT_PID += 1;
        id
    };

    unsafe {
        let proc = &mut PROCESSES[slot];
        proc.pid = pid;
        set_name(proc, name);
        proc.state = ProcState::Ready;
        proc.priority = 1;
        proc.is_user = true;
        proc.stack_virt = stack_virt;
        proc.stack_base = stack_virt;
        proc.entry_point = entry;
        proc.page_table = user_pml4;
        proc.user_stack_base = user_stack_bottom;

        proc.context = Context {
            rsp: stack_top,
            rbp: stack_top + 8,
            rbx: 0,
            r12: 0,
            r13: 0,
            r14: 0,
            r15: 0,
            rflags: 0x202,
            rip: entry,
            cr3: user_pml4,
        };

        // Create root capability
        let root_cap = cap::cap_create(pid, CapObjectType::Process, pid, CAP_ALL, 0);
        cap::cap_table_insert(&root_cap);
        proc.cap_root = root_cap.cap_id;

        Some(proc)
    }
}

// ---------------------------------------------------------------------------
// Process exit
// ---------------------------------------------------------------------------

/// Terminate a process: free stack, destroy user address space.
pub fn process_exit(pid: u64, _exit_code: i32) {
    unsafe {
        for i in 0..MAX_PROCESSES {
            if PROCESSES[i].pid == pid && PROCESSES[i].state != ProcState::Free {
                PROCESSES[i].state = ProcState::Terminated;

                // Free user address space if applicable
                if PROCESSES[i].is_user && PROCESSES[i].page_table != 0 {
                    vmm::vmm_destroy_user_space(PROCESSES[i].page_table);
                }

                // Free kernel stack
                // SAFETY: Layout is infallible for these constants (64 KiB, align 16)
                if PROCESSES[i].stack_virt != 0 {
                    alloc::alloc::dealloc(
                        PROCESSES[i].stack_virt as *mut u8,
                        alloc::alloc::Layout::from_size_align(PROC_STACK_SIZE, 16).unwrap(),
                    );
                }

                PROCESSES[i].state = ProcState::Free;
                PROCESSES[i].pid = 0;
                return;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

pub fn process_get_by_pid(pid: u64) -> Option<&'static mut Process> {
    unsafe {
        for i in 0..MAX_PROCESSES {
            if PROCESSES[i].pid == pid && PROCESSES[i].state != ProcState::Free {
                return Some(&mut PROCESSES[i]);
            }
        }
    }
    None
}

pub fn process_get_current() -> Option<&'static mut Process> {
    let pid = super::scheduler::current_pid();
    if pid == 0 { return None; }
    process_get_by_pid(pid)
}

/// Get mutable reference to process by table index (for scheduler).
pub fn process_by_index(idx: usize) -> &'static mut Process {
    unsafe { &mut PROCESSES[idx] }
}

/// Find table index for a pid.
pub fn process_index_of(pid: u64) -> Option<usize> {
    unsafe {
        for i in 0..MAX_PROCESSES {
            if PROCESSES[i].pid == pid && PROCESSES[i].state != ProcState::Free {
                return Some(i);
            }
        }
    }
    None
}
