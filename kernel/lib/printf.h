#ifndef VAULTOS_PRINTF_H
#define VAULTOS_PRINTF_H

#include "types.h"

/* Callback-based formatting */
typedef void (*putchar_fn)(char c, void *ctx);

int kvprintf(putchar_fn put, void *ctx, const char *fmt, __builtin_va_list ap);

/* Print to serial + framebuffer */
int kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Print to buffer */
int snprintf(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int vsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap);

#endif /* VAULTOS_PRINTF_H */
