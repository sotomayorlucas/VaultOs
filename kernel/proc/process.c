#include "process.h"
#include "scheduler.h"
#include "../mm/heap.h"
#include "../mm/vmm.h"
#include "../mm/memory_layout.h"
#include "../cap/capability.h"
#include "../cap/cap_table.h"
#include "../lib/string.h"
#include "../lib/printf.h"
#include "../lib/assert.h"

static process_t processes[MAX_PROCESSES];
static uint64_t next_pid = 1;
static list_node_t all_processes;

void process_init(void) {
    memset(processes, 0, sizeof(processes));
    list_init(&all_processes);
    next_pid = 1;
    kprintf("[PROC] Process subsystem initialized\n");
}

process_t *process_create(const char *name, void (*entry)(void)) {
    /* Find free slot */
    process_t *proc = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == 0) {
            proc = &processes[i];
            break;
        }
    }
    if (!proc) {
        kprintf("[PROC] No free process slots!\n");
        return NULL;
    }

    memset(proc, 0, sizeof(process_t));
    proc->pid = next_pid++;
    strncpy(proc->name, name, sizeof(proc->name) - 1);
    proc->state = PROC_READY;
    proc->priority = 1;

    /* Allocate kernel stack */
    proc->stack_base = (uint64_t)kmalloc(PROC_STACK_SIZE);
    ASSERT(proc->stack_base != 0);

    /* Setup context */
    uint64_t stack_top = proc->stack_base + PROC_STACK_SIZE;
    proc->context.rsp = stack_top - 8; /* Room for alignment */
    proc->context.rbp = stack_top;
    proc->context.rip = (uint64_t)entry;
    proc->context.cr3 = vmm_get_kernel_pml4(); /* Share kernel address space */
    proc->context.rflags = 0x202; /* IF set */

    proc->entry_point = (uint64_t)entry;

    /* Create root capability for this process */
    capability_t cap = cap_create(proc->pid, CAP_OBJ_PROCESS,
                                   proc->pid, CAP_ALL, 0);
    cap_table_insert(&cap);
    proc->cap_root = cap.cap_id;

    list_init(&proc->sched_node);
    list_init(&proc->all_node);
    list_add_tail(&all_processes, &proc->all_node);

    kprintf("[PROC] Created process '%s' (pid=%llu)\n", name, proc->pid);
    return proc;
}

void process_exit(process_t *proc, int exit_code) {
    (void)exit_code;
    proc->state = PROC_TERMINATED;
    scheduler_remove(proc);
    list_del(&proc->all_node);

    if (proc->stack_base) kfree((void *)proc->stack_base);
    proc->pid = 0;
}

process_t *process_get_current(void) {
    return scheduler_get_current();
}

process_t *process_get_by_pid(uint64_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid) return &processes[i];
    }
    return NULL;
}
