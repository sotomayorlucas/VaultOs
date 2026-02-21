#include "cpu.h"

bool cpu_has_rdrand(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    return (ecx >> 30) & 1;  /* CPUID.01H:ECX.RDRAND[bit 30] */
}

uint64_t cpu_rdrand64(void) {
    uint64_t val;
    uint8_t ok;
    /* Retry up to 10 times (Intel recommends retrying on failure) */
    for (int i = 0; i < 10; i++) {
        __asm__ volatile (
            "rdrand %0\n\t"
            "setc %1"
            : "=r"(val), "=qm"(ok)
        );
        if (ok) return val;
    }
    /* Fallback: use TSC (not cryptographically secure) */
    return rdtsc();
}
