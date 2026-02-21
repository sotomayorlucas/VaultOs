#ifndef VAULTOS_ASSERT_H
#define VAULTOS_ASSERT_H

#include "types.h"

/* Forward declaration - implemented in kernel/main.c */
void kernel_panic(const char *msg, const char *file, int line) NORETURN;

#define PANIC(msg) kernel_panic(msg, __FILE__, __LINE__)

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            kernel_panic("ASSERT failed: " #cond, __FILE__, __LINE__); \
        } \
    } while (0)

#define ASSERT_MSG(cond, msg) \
    do { \
        if (!(cond)) { \
            kernel_panic(msg, __FILE__, __LINE__); \
        } \
    } while (0)

/* Static assert (compile-time) */
#define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)

#endif /* VAULTOS_ASSERT_H */
