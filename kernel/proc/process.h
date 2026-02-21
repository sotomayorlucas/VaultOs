#ifndef VAULTOS_PROCESS_H
#define VAULTOS_PROCESS_H

#include "../lib/types.h"
#include "../lib/list.h"
#include "../arch/x86_64/context.h"

typedef enum {
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_TERMINATED,
} proc_state_t;

#define MAX_PROCESSES    64
#define PROC_STACK_SIZE  (64 * 1024)  /* 64 KiB per process stack */

typedef struct process {
    uint64_t      pid;
    char          name[64];
    proc_state_t  state;
    uint8_t       priority;
    context_t     context;
    uint64_t      page_table;
    uint64_t      stack_base;     /* Physical base of kernel stack */
    uint64_t      entry_point;
    uint64_t      cap_root;
    list_node_t   sched_node;
    list_node_t   all_node;
} process_t;

void       process_init(void);
process_t *process_create(const char *name, void (*entry)(void));
void       process_exit(process_t *proc, int exit_code);
process_t *process_get_current(void);
process_t *process_get_by_pid(uint64_t pid);

#endif /* VAULTOS_PROCESS_H */
