#ifndef VAULTOS_PMM_H
#define VAULTOS_PMM_H

#include "../lib/types.h"
#include "../../include/vaultos/boot_info.h"

void     pmm_init(BootInfo *boot_info);
uint64_t pmm_alloc_page(void);
void     pmm_free_page(uint64_t phys_addr);
uint64_t pmm_alloc_pages(size_t count);
void     pmm_free_pages(uint64_t phys_addr, size_t count);
uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_free_pages(void);

#endif /* VAULTOS_PMM_H */
