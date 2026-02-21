#include "scheduler.h"
#include "../arch/x86_64/context.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/cpu.h"
#include "../lib/printf.h"
#include "../lib/assert.h"

static list_node_t ready_queue;
static process_t  *current_process = NULL;
static uint64_t    tick_count = 0;
static bool        scheduler_active = false;

#define TIMESLICE 10 /* Ticks per timeslice */

void scheduler_init(void) {
    list_init(&ready_queue);
    current_process = NULL;
    tick_count = 0;
    scheduler_active = false;
    kprintf("[SCHED] Scheduler initialized\n");
}

void scheduler_add(process_t *proc) {
    proc->state = PROC_READY;
    list_add_tail(&ready_queue, &proc->sched_node);
}

void scheduler_remove(process_t *proc) {
    list_del(&proc->sched_node);
    list_init(&proc->sched_node); /* Reset to safe state */
}

process_t *scheduler_get_current(void) {
    return current_process;
}

static void schedule(void) {
    if (list_empty(&ready_queue)) return;
    if (!scheduler_active) return;

    process_t *prev = current_process;

    /* Get next process from ready queue */
    list_node_t *next_node = ready_queue.next;
    process_t *next = list_entry(next_node, process_t, sched_node);

    if (next == prev) return; /* Same process, no switch needed */

    /* Move current to back of queue if it's still running */
    if (prev && prev->state == PROC_RUNNING) {
        prev->state = PROC_READY;
        list_del(&prev->sched_node);
        list_add_tail(&ready_queue, &prev->sched_node);
    }

    /* Switch to next process */
    list_del(&next->sched_node);
    list_add_tail(&ready_queue, &next->sched_node);
    next->state = PROC_RUNNING;
    current_process = next;

    /* Update TSS RSP0 for ring transitions */
    gdt_set_tss_rsp0(next->stack_base + PROC_STACK_SIZE);

    if (prev) {
        context_switch(&prev->context, &next->context);
    }
}

void scheduler_tick(void) {
    tick_count++;
    if (!scheduler_active) return;
    if (tick_count % TIMESLICE == 0) {
        schedule();
    }
}

void scheduler_yield(void) {
    schedule();
}

void scheduler_block(process_t *proc) {
    proc->state = PROC_BLOCKED;
    scheduler_remove(proc);
    if (proc == current_process) {
        schedule();
    }
}

void scheduler_unblock(process_t *proc) {
    proc->state = PROC_READY;
    scheduler_add(proc);
}

void scheduler_start(void) {
    ASSERT(!list_empty(&ready_queue));

    list_node_t *first_node = ready_queue.next;
    process_t *first = list_entry(first_node, process_t, sched_node);

    first->state = PROC_RUNNING;
    current_process = first;
    scheduler_active = true;

    gdt_set_tss_rsp0(first->stack_base + PROC_STACK_SIZE);

    kprintf("[SCHED] Starting first process: '%s' (pid=%llu)\n",
            first->name, first->pid);

    /* Jump to first process entry point */
    sti();
    void (*entry)(void) = (void (*)(void))first->entry_point;
    entry();

    /* Should never return */
    PANIC("scheduler_start returned!");
}
