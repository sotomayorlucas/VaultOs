#include "random.h"
#include "../arch/x86_64/cpu.h"
#include "../lib/printf.h"

/* xorshift128+ PRNG state (fallback when no RDRAND) */
static uint64_t prng_s0, prng_s1;
static bool hw_rng = false;

void random_init(void) {
    hw_rng = cpu_has_rdrand();

    if (hw_rng) {
        kprintf("[RNG] Hardware RDRAND available\n");
        /* Seed PRNG with RDRAND too for fallback */
        prng_s0 = cpu_rdrand64();
        prng_s1 = cpu_rdrand64();
    } else {
        kprintf("[RNG] No RDRAND, using TSC-seeded xorshift128+\n");
        prng_s0 = rdtsc();
        prng_s1 = rdtsc() ^ 0x5DEECE66DULL;
    }

    /* Warm up PRNG */
    for (int i = 0; i < 20; i++) random_u64();
}

bool random_hw_available(void) {
    return hw_rng;
}

static uint64_t xorshift128plus(void) {
    uint64_t s1 = prng_s0;
    uint64_t s0 = prng_s1;
    prng_s0 = s0;
    s1 ^= s1 << 23;
    s1 ^= s1 >> 17;
    s1 ^= s0;
    s1 ^= s0 >> 26;
    prng_s1 = s1;
    return prng_s0 + prng_s1;
}

uint64_t random_u64(void) {
    if (hw_rng) {
        return cpu_rdrand64();
    }
    return xorshift128plus();
}

void random_bytes(uint8_t *buf, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint64_t val = random_u64();
        size_t to_copy = len - i;
        if (to_copy > 8) to_copy = 8;
        for (size_t j = 0; j < to_copy; j++) {
            buf[i++] = (uint8_t)(val >> (j * 8));
        }
    }
}
