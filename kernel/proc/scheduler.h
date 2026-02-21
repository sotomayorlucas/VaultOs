#ifndef VAULTOS_SCHEDULER_H
#define VAULTOS_SCHEDULER_H

#include "process.h"

void       scheduler_init(void);
void       scheduler_add(process_t *proc);
void       scheduler_remove(process_t *proc);
void       scheduler_yield(void);
void       scheduler_block(process_t *proc);
void       scheduler_unblock(process_t *proc);
process_t *scheduler_get_current(void);
void       scheduler_start(void) NORETURN;

/* Called from PIT timer interrupt */
void       scheduler_tick(void);

#endif /* VAULTOS_SCHEDULER_H */
