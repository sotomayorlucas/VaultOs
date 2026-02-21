#include "paging.h"
#include "pmm.h"
#include "memory_layout.h"
#include "../lib/string.h"
#include "../lib/assert.h"

/*
 * x86_64 4-level page table implementation.
 *
 * Before VMM is fully initialized, we use physical addresses directly.
 * After VMM sets up the physical map, we can use PHYS_TO_VIRT().
 *
 * For early boot, the bootloader sets up identity mapping + higher-half,
 * so we can access low physical memory directly.
 */

static bool phys_map_ready = false;

void paging_set_phys_map_ready(void) {
    phys_map_ready = true;
}

static uint64_t *phys_to_ptr(uint64_t phys) {
    if (phys_map_ready) {
        return (uint64_t *)PHYS_TO_VIRT(phys);
    }
    /* Early boot: identity mapping still active */
    return (uint64_t *)(uintptr_t)phys;
}

/* Allocate a zeroed page table page */
static uint64_t alloc_table_page(void) {
    uint64_t phys = pmm_alloc_page();
    ASSERT(phys != 0);
    memset(phys_to_ptr(phys), 0, PAGE_SIZE);
    return phys;
}

/* Get or create a page table entry, returning the next-level table */
static uint64_t *get_or_create_table(uint64_t *table, uint64_t index, uint64_t flags) {
    if (!(table[index] & PTE_PRESENT)) {
        uint64_t new_table = alloc_table_page();
        table[index] = new_table | PTE_PRESENT | PTE_WRITABLE | flags;
    }
    return phys_to_ptr(table[index] & PTE_ADDR_MASK);
}

uint64_t paging_create_address_space(void) {
    return alloc_table_page();
}

void paging_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4 = phys_to_ptr(pml4_phys);

    uint64_t extra_flags = (flags & PTE_USER) ? PTE_USER : 0;
    uint64_t *pdpt = get_or_create_table(pml4, PML4_INDEX(virt), extra_flags);
    uint64_t *pd   = get_or_create_table(pdpt, PDPT_INDEX(virt), extra_flags);
    uint64_t *pt   = get_or_create_table(pd,   PD_INDEX(virt),   extra_flags);

    pt[PT_INDEX(virt)] = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;
}

void paging_unmap_page(uint64_t pml4_phys, uint64_t virt) {
    uint64_t *pml4 = phys_to_ptr(pml4_phys);

    if (!(pml4[PML4_INDEX(virt)] & PTE_PRESENT)) return;
    uint64_t *pdpt = phys_to_ptr(pml4[PML4_INDEX(virt)] & PTE_ADDR_MASK);

    if (!(pdpt[PDPT_INDEX(virt)] & PTE_PRESENT)) return;
    uint64_t *pd = phys_to_ptr(pdpt[PDPT_INDEX(virt)] & PTE_ADDR_MASK);

    if (!(pd[PD_INDEX(virt)] & PTE_PRESENT)) return;
    uint64_t *pt = phys_to_ptr(pd[PD_INDEX(virt)] & PTE_ADDR_MASK);

    pt[PT_INDEX(virt)] = 0;

    /* Invalidate TLB for this page */
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

uint64_t paging_virt_to_phys(uint64_t pml4_phys, uint64_t virt) {
    uint64_t *pml4 = phys_to_ptr(pml4_phys);

    if (!(pml4[PML4_INDEX(virt)] & PTE_PRESENT)) return 0;
    uint64_t *pdpt = phys_to_ptr(pml4[PML4_INDEX(virt)] & PTE_ADDR_MASK);

    if (!(pdpt[PDPT_INDEX(virt)] & PTE_PRESENT)) return 0;
    uint64_t *pd = phys_to_ptr(pdpt[PDPT_INDEX(virt)] & PTE_ADDR_MASK);

    /* Check for 2 MiB huge page */
    if (pd[PD_INDEX(virt)] & PTE_HUGE) {
        return (pd[PD_INDEX(virt)] & PTE_ADDR_MASK) | (virt & 0x1FFFFF);
    }

    if (!(pd[PD_INDEX(virt)] & PTE_PRESENT)) return 0;
    uint64_t *pt = phys_to_ptr(pd[PD_INDEX(virt)] & PTE_ADDR_MASK);

    if (!(pt[PT_INDEX(virt)] & PTE_PRESENT)) return 0;
    return (pt[PT_INDEX(virt)] & PTE_ADDR_MASK) | (virt & 0xFFF);
}

void paging_map_range(uint64_t pml4_phys, uint64_t virt_start,
                       uint64_t phys_start, size_t num_pages, uint64_t flags) {
    for (size_t i = 0; i < num_pages; i++) {
        paging_map_page(pml4_phys, virt_start + i * PAGE_SIZE,
                         phys_start + i * PAGE_SIZE, flags);
    }
}

void paging_destroy_address_space(uint64_t pml4_phys) {
    uint64_t *pml4 = phys_to_ptr(pml4_phys);

    for (int i = 0; i < 256; i++) { /* Only user-space entries (lower half) */
        if (!(pml4[i] & PTE_PRESENT)) continue;
        uint64_t *pdpt = phys_to_ptr(pml4[i] & PTE_ADDR_MASK);

        for (int j = 0; j < 512; j++) {
            if (!(pdpt[j] & PTE_PRESENT)) continue;
            uint64_t *pd = phys_to_ptr(pdpt[j] & PTE_ADDR_MASK);

            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & PTE_PRESENT) || (pd[k] & PTE_HUGE)) continue;
                pmm_free_page(pd[k] & PTE_ADDR_MASK);
            }
            pmm_free_page(pdpt[j] & PTE_ADDR_MASK);
        }
        pmm_free_page(pml4[i] & PTE_ADDR_MASK);
    }
    pmm_free_page(pml4_phys);
}
