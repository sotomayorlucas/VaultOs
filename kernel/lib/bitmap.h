#ifndef VAULTOS_BITMAP_H
#define VAULTOS_BITMAP_H

#include "types.h"

static inline void bitmap_set(uint64_t *bitmap, uint64_t bit) {
    bitmap[bit / 64] |= (1ULL << (bit % 64));
}

static inline void bitmap_clear(uint64_t *bitmap, uint64_t bit) {
    bitmap[bit / 64] &= ~(1ULL << (bit % 64));
}

static inline bool bitmap_test(const uint64_t *bitmap, uint64_t bit) {
    return (bitmap[bit / 64] >> (bit % 64)) & 1;
}

/* Set a range of bits */
static inline void bitmap_set_range(uint64_t *bitmap, uint64_t start, uint64_t count) {
    for (uint64_t i = start; i < start + count; i++)
        bitmap_set(bitmap, i);
}

/* Clear a range of bits */
static inline void bitmap_clear_range(uint64_t *bitmap, uint64_t start, uint64_t count) {
    for (uint64_t i = start; i < start + count; i++)
        bitmap_clear(bitmap, i);
}

/* Find first clear bit starting from 'start' */
static inline uint64_t bitmap_find_clear(const uint64_t *bitmap, uint64_t total_bits, uint64_t start) {
    for (uint64_t i = start; i < total_bits; i++) {
        if (!bitmap_test(bitmap, i)) return i;
    }
    return UINT64_MAX; /* Not found */
}

/* Find 'count' contiguous clear bits starting from 'start' */
static inline uint64_t bitmap_find_clear_range(const uint64_t *bitmap, uint64_t total_bits,
                                                 uint64_t count, uint64_t start) {
    uint64_t found = 0;
    uint64_t base = start;
    for (uint64_t i = start; i < total_bits; i++) {
        if (bitmap_test(bitmap, i)) {
            found = 0;
            base = i + 1;
        } else {
            found++;
            if (found == count) return base;
        }
    }
    return UINT64_MAX;
}

#endif /* VAULTOS_BITMAP_H */
