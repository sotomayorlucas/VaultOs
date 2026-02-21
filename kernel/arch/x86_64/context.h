#ifndef VAULTOS_CONTEXT_H
#define VAULTOS_CONTEXT_H

#include "../../lib/types.h"

/* Saved CPU context for context switching */
typedef struct {
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rflags;
    uint64_t rip;
    uint64_t cr3;   /* Page table base */
} context_t;

/* Assembly function: switch from current context to next */
extern void context_switch(context_t *current, context_t *next);

#endif /* VAULTOS_CONTEXT_H */
