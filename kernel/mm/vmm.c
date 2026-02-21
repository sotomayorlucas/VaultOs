#include "vmm.h"
#include "pmm.h"
#include "paging.h"
#include "memory_layout.h"
#include "../lib/printf.h"
#include "../lib/assert.h"
#include "../arch/x86_64/cpu.h"

static uint64_t kernel_pml4_phys = 0;

/* External: notify paging that phys map is ready */
extern void paging_set_phys_map_ready(void);

void vmm_init(BootInfo *boot_info) {
    /* Create kernel page table */
    kernel_pml4_phys = paging_create_address_space();
    ASSERT(kernel_pml4_phys != 0);

    kprintf("[VMM] Kernel PML4 at phys 0x%016llx\n", kernel_pml4_phys);

    /*
     * Map kernel code/data: phys KERNEL_PHYS_BASE -> virt KERNEL_VBASE
     * We map a generous 2 MiB for the kernel binary.
     */
    uint64_t kernel_pages = ALIGN_UP(boot_info->kernel_size, PAGE_SIZE) / PAGE_SIZE;
    if (kernel_pages < 512) kernel_pages = 512; /* At least 2 MiB */

    kprintf("[VMM] Mapping kernel: phys 0x%llx -> virt 0x%llx (%llu pages)\n",
            boot_info->kernel_phys_base, KERNEL_VBASE, kernel_pages);

    paging_map_range(kernel_pml4_phys, KERNEL_VBASE,
                      boot_info->kernel_phys_base, kernel_pages,
                      PTE_PRESENT | PTE_WRITABLE | PTE_GLOBAL);

    /*
     * Identity map the first 4 MiB for early boot compatibility.
     * This will be removed once we're fully in higher-half.
     */
    paging_map_range(kernel_pml4_phys, 0, 0, 1024,
                      PTE_PRESENT | PTE_WRITABLE);

    /*
     * Physical memory direct map: map first 1 GiB at PHYS_MAP_BASE.
     * This allows the kernel to access any physical address easily.
     */
    uint64_t phys_map_pages = (1024ULL * 1024 * 1024) / PAGE_SIZE; /* 1 GiB */
    kprintf("[VMM] Mapping physical memory: 0x0 -> virt 0x%llx (1 GiB)\n", PHYS_MAP_BASE);

    paging_map_range(kernel_pml4_phys, PHYS_MAP_BASE, 0, phys_map_pages,
                      PTE_PRESENT | PTE_WRITABLE | PTE_GLOBAL);

    /*
     * Map framebuffer
     */
    if (boot_info->fb_base) {
        uint64_t fb_size = (uint64_t)boot_info->fb_pitch * boot_info->fb_height;
        uint64_t fb_pages = ALIGN_UP(fb_size, PAGE_SIZE) / PAGE_SIZE;
        kprintf("[VMM] Mapping framebuffer: phys 0x%llx -> virt 0x%llx (%llu pages)\n",
                boot_info->fb_base, FRAMEBUF_VBASE, fb_pages);

        paging_map_range(kernel_pml4_phys, FRAMEBUF_VBASE,
                          boot_info->fb_base, fb_pages,
                          PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD);
    }

    /*
     * Map kernel heap region (initially just a few pages, grows on demand)
     * Pre-map 1 MiB for the heap to start with.
     */
    uint64_t initial_heap_pages = 256; /* 1 MiB */
    kprintf("[VMM] Mapping kernel heap: virt 0x%llx (%llu pages)\n",
            KERNEL_HEAP_BASE, initial_heap_pages);

    for (size_t i = 0; i < initial_heap_pages; i++) {
        uint64_t page = pmm_alloc_page();
        ASSERT(page != 0);
        paging_map_page(kernel_pml4_phys, KERNEL_HEAP_BASE + i * PAGE_SIZE,
                         page, PTE_PRESENT | PTE_WRITABLE | PTE_NX);
    }

    /* Switch to new page tables */
    write_cr3(kernel_pml4_phys);
    kprintf("[VMM] Page tables activated\n");

    /* Now physical map is ready */
    paging_set_phys_map_ready();

    kprintf("[VMM] Initialization complete\n");
}

uint64_t vmm_get_kernel_pml4(void) {
    return kernel_pml4_phys;
}

uint64_t vmm_create_user_space(void) {
    uint64_t pml4 = paging_create_address_space();
    if (!pml4) return 0;

    /*
     * Copy kernel mappings (upper half of PML4 entries 256-511)
     * from the kernel PML4 to the new user PML4.
     */
    uint64_t *kernel_pml4 = (uint64_t *)PHYS_TO_VIRT(kernel_pml4_phys);
    uint64_t *user_pml4   = (uint64_t *)PHYS_TO_VIRT(pml4);

    for (int i = 256; i < 512; i++) {
        user_pml4[i] = kernel_pml4[i];
    }

    return pml4;
}

void vmm_destroy_user_space(uint64_t pml4_phys) {
    paging_destroy_address_space(pml4_phys);
}

void vmm_switch_address_space(uint64_t pml4_phys) {
    write_cr3(pml4_phys);
}
