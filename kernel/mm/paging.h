#ifndef VAULTOS_PAGING_H
#define VAULTOS_PAGING_H

#include "../lib/types.h"

/* Page table entry flags */
#define PTE_PRESENT   (1ULL << 0)
#define PTE_WRITABLE  (1ULL << 1)
#define PTE_USER      (1ULL << 2)
#define PTE_PWT       (1ULL << 3)
#define PTE_PCD       (1ULL << 4)
#define PTE_ACCESSED  (1ULL << 5)
#define PTE_DIRTY     (1ULL << 6)
#define PTE_HUGE      (1ULL << 7)   /* 2 MiB page (at PD level) */
#define PTE_GLOBAL    (1ULL << 8)
#define PTE_NX        (1ULL << 63)

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

/* Extract page table indices from virtual address */
#define PML4_INDEX(va) (((va) >> 39) & 0x1FF)
#define PDPT_INDEX(va) (((va) >> 30) & 0x1FF)
#define PD_INDEX(va)   (((va) >> 21) & 0x1FF)
#define PT_INDEX(va)   (((va) >> 12) & 0x1FF)

/* Create a new PML4 (returns physical address) */
uint64_t paging_create_address_space(void);

/* Destroy address space (free all page table pages) */
void paging_destroy_address_space(uint64_t pml4_phys);

/* Map a single 4 KiB page */
void paging_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);

/* Unmap a single page */
void paging_unmap_page(uint64_t pml4_phys, uint64_t virt);

/* Walk page tables to translate virtual -> physical */
uint64_t paging_virt_to_phys(uint64_t pml4_phys, uint64_t virt);

/* Map a range of contiguous pages */
void paging_map_range(uint64_t pml4_phys, uint64_t virt_start,
                       uint64_t phys_start, size_t num_pages, uint64_t flags);

#endif /* VAULTOS_PAGING_H */
