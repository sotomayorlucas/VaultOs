#ifndef VAULTOS_HEAP_H
#define VAULTOS_HEAP_H

#include "../lib/types.h"

void  heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t new_size);
void  kfree(void *ptr);

/* Heap statistics */
size_t heap_used(void);
size_t heap_free(void);

#endif /* VAULTOS_HEAP_H */
