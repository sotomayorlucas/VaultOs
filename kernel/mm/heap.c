#include "heap.h"
#include "memory_layout.h"
#include "pmm.h"
#include "paging.h"
#include "vmm.h"
#include "../lib/string.h"
#include "../lib/printf.h"
#include "../lib/assert.h"

/*
 * Simple first-fit heap allocator for the kernel.
 * Uses a linked list of blocks with header containing size and free flag.
 * Coalesces adjacent free blocks on free.
 */

#define HEAP_MAGIC 0xDEADBEEFULL
#define MIN_BLOCK_SIZE 32

typedef struct heap_block {
    uint64_t           magic;
    size_t             size;    /* Size of data area (not including header) */
    bool               free;
    struct heap_block *next;
    struct heap_block *prev;
} heap_block_t;

static heap_block_t *heap_head = NULL;
static size_t heap_total_size = 0;
static size_t heap_used_bytes = 0;

void heap_init(void) {
    /* Heap region is pre-mapped by VMM at KERNEL_HEAP_BASE (1 MiB initially) */
    size_t initial_size = 256 * PAGE_SIZE; /* 1 MiB */

    heap_head = (heap_block_t *)KERNEL_HEAP_BASE;
    heap_head->magic = HEAP_MAGIC;
    heap_head->size = initial_size - sizeof(heap_block_t);
    heap_head->free = true;
    heap_head->next = NULL;
    heap_head->prev = NULL;

    heap_total_size = initial_size;
    heap_used_bytes = 0;

    kprintf("[HEAP] Initialized at 0x%llx, size=%llu KiB\n",
            KERNEL_HEAP_BASE, initial_size / 1024);
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Align to 16 bytes */
    size = ALIGN_UP(size, 16);

    heap_block_t *block = heap_head;
    while (block) {
        ASSERT(block->magic == HEAP_MAGIC);

        if (block->free && block->size >= size) {
            /* Found a free block large enough */

            /* Split if there's enough space for another block */
            if (block->size >= size + sizeof(heap_block_t) + MIN_BLOCK_SIZE) {
                heap_block_t *new_block = (heap_block_t *)((uint8_t *)block +
                                           sizeof(heap_block_t) + size);
                new_block->magic = HEAP_MAGIC;
                new_block->size = block->size - size - sizeof(heap_block_t);
                new_block->free = true;
                new_block->next = block->next;
                new_block->prev = block;

                if (block->next) block->next->prev = new_block;
                block->next = new_block;
                block->size = size;
            }

            block->free = false;
            heap_used_bytes += block->size;
            return (void *)((uint8_t *)block + sizeof(heap_block_t));
        }
        block = block->next;
    }

    kprintf("[HEAP] Out of memory! Requested %llu bytes\n", (uint64_t)size);
    return NULL;
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    ASSERT(block->magic == HEAP_MAGIC);
    ASSERT(!block->free);

    block->free = true;
    heap_used_bytes -= block->size;

    /* Coalesce with next block if free */
    if (block->next && block->next->free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
    }

    /* Coalesce with previous block if free */
    if (block->prev && block->prev->free) {
        block->prev->size += sizeof(heap_block_t) + block->size;
        block->prev->next = block->next;
        if (block->next) block->next->prev = block->prev;
    }
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return NULL; }

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    ASSERT(block->magic == HEAP_MAGIC);

    if (block->size >= new_size) return ptr;

    void *new_ptr = kmalloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        kfree(ptr);
    }
    return new_ptr;
}

int heap_expand(size_t bytes) {
    if (bytes == 0) return 0;

    /* Check we won't exceed the virtual address space reserved for heap */
    if (heap_total_size + bytes > KERNEL_HEAP_SIZE) {
        kprintf("[HEAP] Cannot expand: would exceed max heap size\n");
        return -1;
    }

    size_t num_pages = ALIGN_UP(bytes, PAGE_SIZE) / PAGE_SIZE;
    uint64_t phys = pmm_alloc_pages(num_pages);
    if (!phys) {
        kprintf("[HEAP] Cannot expand: out of physical memory\n");
        return -1;
    }

    uint64_t virt_start = KERNEL_HEAP_BASE + heap_total_size;
    uint64_t pml4 = vmm_get_kernel_pml4();

    paging_map_range(pml4, virt_start, phys, num_pages,
                     PTE_PRESENT | PTE_WRITABLE | PTE_NX);

    /* Create a new free block at the expansion region */
    size_t expand_size = num_pages * PAGE_SIZE;
    heap_block_t *new_block = (heap_block_t *)virt_start;
    new_block->magic = HEAP_MAGIC;
    new_block->size = expand_size - sizeof(heap_block_t);
    new_block->free = true;
    new_block->next = NULL;
    new_block->prev = NULL;

    /* Find the last block in the list and link it */
    heap_block_t *last = heap_head;
    while (last->next) last = last->next;

    last->next = new_block;
    new_block->prev = last;

    heap_total_size += expand_size;

    /* Try to coalesce with previous block if it's free and adjacent */
    if (last->free) {
        uint8_t *last_end = (uint8_t *)last + sizeof(heap_block_t) + last->size;
        if (last_end == (uint8_t *)new_block) {
            last->size += sizeof(heap_block_t) + new_block->size;
            last->next = new_block->next;
            if (new_block->next) new_block->next->prev = last;
        }
    }

    kprintf("[HEAP] Expanded by %llu KiB (total: %llu KiB)\n",
            expand_size / 1024, heap_total_size / 1024);
    return 0;
}

size_t heap_used(void) { return heap_used_bytes; }
size_t heap_free(void) { return heap_total_size - heap_used_bytes - sizeof(heap_block_t); }
