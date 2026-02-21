#include "pmm.h"
#include "memory_layout.h"
#include "../lib/bitmap.h"
#include "../lib/string.h"
#include "../lib/printf.h"
#include "../lib/assert.h"

/*
 * Bitmap-based physical page allocator.
 * 1 bit per 4 KiB page. Bit set = used, bit clear = free.
 */

#define MAX_PHYS_MEMORY  (4ULL * 1024 * 1024 * 1024)  /* 4 GiB max */
#define MAX_PAGES        (MAX_PHYS_MEMORY / PAGE_SIZE)
#define BITMAP_SIZE      (MAX_PAGES / 64)              /* In uint64_t units */

static uint64_t bitmap[BITMAP_SIZE];
static uint64_t total_pages = 0;
static uint64_t used_pages = 0;
static uint64_t highest_page = 0;

void pmm_init(BootInfo *boot_info) {
    /* Mark all pages as used initially */
    memset(bitmap, 0xFF, sizeof(bitmap));
    total_pages = 0;
    used_pages = 0;

    /* Parse UEFI memory map and free usable regions */
    uint8_t *mmap_ptr = (uint8_t *)(uintptr_t)boot_info->mmap_base;
    uint32_t entry_count = boot_info->mmap_entry_count;
    uint64_t desc_size = boot_info->mmap_desc_size;

    for (uint32_t i = 0; i < entry_count; i++) {
        MemoryMapEntry *entry = (MemoryMapEntry *)(mmap_ptr + i * desc_size);

        if (entry->type == MMAP_USABLE) {
            uint64_t base = ALIGN_UP(entry->phys_start, PAGE_SIZE);
            uint64_t end = entry->phys_start + entry->num_pages * PAGE_SIZE;
            uint64_t page_start = base / PAGE_SIZE;
            uint64_t page_end = end / PAGE_SIZE;

            if (page_end > MAX_PAGES) page_end = MAX_PAGES;

            for (uint64_t p = page_start; p < page_end; p++) {
                bitmap_clear(bitmap, p);
                total_pages++;
            }

            if (page_end > highest_page) highest_page = page_end;
        }
    }

    /*
     * Reserve critical regions:
     *   - First 1 MiB (real mode IVT, BIOS data, etc.)
     *   - Kernel physical region (including BSS!)
     *
     * boot_info->kernel_size is just the file size, but the kernel's BSS
     * extends far beyond that. Use the linker symbol _kernel_end instead.
     */
    extern char _kernel_start[], _kernel_end[];
    uint64_t kern_start_addr = (uint64_t)(uintptr_t)_kernel_start;
    uint64_t kern_end_addr   = (uint64_t)(uintptr_t)_kernel_end;
    uint64_t kernel_start = kern_start_addr / PAGE_SIZE;
    uint64_t kernel_pages = ALIGN_UP(kern_end_addr - kern_start_addr, PAGE_SIZE) / PAGE_SIZE;

    /* Reserve first 256 pages (1 MiB) */
    for (uint64_t p = 0; p < 256; p++) {
        if (!bitmap_test(bitmap, p)) {
            bitmap_set(bitmap, p);
            used_pages++;
        }
    }

    /* Reserve kernel pages */
    for (uint64_t p = kernel_start; p < kernel_start + kernel_pages; p++) {
        if (!bitmap_test(bitmap, p)) {
            bitmap_set(bitmap, p);
            used_pages++;
        }
    }

    kprintf("[PMM] Total: %llu pages (%llu MiB), Free: %llu pages (%llu MiB)\n",
            total_pages, total_pages * PAGE_SIZE / (1024 * 1024),
            total_pages - used_pages,
            (total_pages - used_pages) * PAGE_SIZE / (1024 * 1024));
}

uint64_t pmm_alloc_page(void) {
    uint64_t page = bitmap_find_clear(bitmap, highest_page, 256); /* Skip first 1 MiB */
    if (page == UINT64_MAX) {
        kprintf("[PMM] Out of physical memory!\n");
        return 0;
    }
    bitmap_set(bitmap, page);
    used_pages++;
    return page * PAGE_SIZE;
}

void pmm_free_page(uint64_t phys_addr) {
    uint64_t page = phys_addr / PAGE_SIZE;
    ASSERT(page < highest_page);
    ASSERT(bitmap_test(bitmap, page));
    bitmap_clear(bitmap, page);
    used_pages--;
}

uint64_t pmm_alloc_pages(size_t count) {
    uint64_t base = bitmap_find_clear_range(bitmap, highest_page, count, 256);
    if (base == UINT64_MAX) {
        kprintf("[PMM] Cannot allocate %llu contiguous pages!\n", (uint64_t)count);
        return 0;
    }
    bitmap_set_range(bitmap, base, count);
    used_pages += count;
    return base * PAGE_SIZE;
}

void pmm_free_pages(uint64_t phys_addr, size_t count) {
    uint64_t page = phys_addr / PAGE_SIZE;
    bitmap_clear_range(bitmap, page, count);
    used_pages -= count;
}

uint64_t pmm_get_total_pages(void) { return total_pages; }
uint64_t pmm_get_free_pages(void) { return total_pages - used_pages; }
