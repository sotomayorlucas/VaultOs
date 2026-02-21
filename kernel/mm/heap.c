#include "heap.h"
#include "memory_layout.h"
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

size_t heap_used(void) { return heap_used_bytes; }
size_t heap_free(void) { return heap_total_size - heap_used_bytes - sizeof(heap_block_t); }
