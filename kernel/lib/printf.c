#include "printf.h"
#include "string.h"

/* Forward declarations for output backends */
extern void serial_putchar(char c);
extern bool fb_ready;
extern void fb_putchar(char c);

/* Internal: format a number */
static int print_num(putchar_fn put, void *ctx, uint64_t num, int base,
                     bool is_signed, bool uppercase, int width, char pad, int *count) {
    char buf[24];
    int i = 0;
    bool negative = false;

    if (is_signed && (int64_t)num < 0) {
        negative = true;
        num = -(int64_t)num;
    }

    if (num == 0) {
        buf[i++] = '0';
    } else {
        const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
        while (num > 0) {
            buf[i++] = digits[num % base];
            num /= base;
        }
    }

    int total = i + (negative ? 1 : 0);
    int padding = (width > total) ? width - total : 0;

    if (pad == '0' && negative) { put('-', ctx); (*count)++; }
    for (int p = 0; p < padding; p++) { put(pad, ctx); (*count)++; }
    if (pad != '0' && negative) { put('-', ctx); (*count)++; }

    while (i > 0) { put(buf[--i], ctx); (*count)++; }
    return 0;
}

int kvprintf(putchar_fn put, void *ctx, const char *fmt, __builtin_va_list ap) {
    int count = 0;

    while (*fmt) {
        if (*fmt != '%') {
            put(*fmt++, ctx);
            count++;
            continue;
        }
        fmt++; /* skip '%' */

        /* Flags */
        char pad = ' ';
        if (*fmt == '0') { pad = '0'; fmt++; }

        /* Width */
        int width = 0;
        while (isdigit(*fmt)) {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Length modifier */
        int length = 0; /* 0=int, 1=long, 2=long long */
        if (*fmt == 'l') { length++; fmt++; }
        if (*fmt == 'l') { length++; fmt++; }

        /* Specifier */
        switch (*fmt) {
        case 'd':
        case 'i': {
            int64_t val;
            if (length >= 2) val = __builtin_va_arg(ap, int64_t);
            else val = __builtin_va_arg(ap, int);
            print_num(put, ctx, (uint64_t)val, 10, true, false, width, pad, &count);
            break;
        }
        case 'u': {
            uint64_t val;
            if (length >= 2) val = __builtin_va_arg(ap, uint64_t);
            else val = __builtin_va_arg(ap, unsigned int);
            print_num(put, ctx, val, 10, false, false, width, pad, &count);
            break;
        }
        case 'x': {
            uint64_t val;
            if (length >= 2) val = __builtin_va_arg(ap, uint64_t);
            else val = __builtin_va_arg(ap, unsigned int);
            print_num(put, ctx, val, 16, false, false, width, pad, &count);
            break;
        }
        case 'X': {
            uint64_t val;
            if (length >= 2) val = __builtin_va_arg(ap, uint64_t);
            else val = __builtin_va_arg(ap, unsigned int);
            print_num(put, ctx, val, 16, false, true, width, pad, &count);
            break;
        }
        case 'p': {
            uint64_t val = (uint64_t)__builtin_va_arg(ap, void *);
            put('0', ctx); count++;
            put('x', ctx); count++;
            print_num(put, ctx, val, 16, false, false, 16, '0', &count);
            break;
        }
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            int slen = (int)strlen(s);
            int spad = (width > slen) ? width - slen : 0;
            for (int p = 0; p < spad; p++) { put(' ', ctx); count++; }
            while (*s) { put(*s++, ctx); count++; }
            break;
        }
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            put(c, ctx);
            count++;
            break;
        }
        case '%':
            put('%', ctx);
            count++;
            break;
        default:
            put('%', ctx);
            put(*fmt, ctx);
            count += 2;
            break;
        }
        fmt++;
    }
    return count;
}

/* Print to serial + framebuffer */
static void kprintf_putchar(char c, void *ctx UNUSED) {
    serial_putchar(c);
    if (fb_ready) fb_putchar(c);
}

int kprintf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int ret = kvprintf(kprintf_putchar, NULL, fmt, ap);
    __builtin_va_end(ap);
    return ret;
}

/* Print to buffer */
typedef struct {
    char   *buf;
    size_t  pos;
    size_t  max;
} snprintf_ctx_t;

static void snprintf_putchar(char c, void *ctx) {
    snprintf_ctx_t *s = (snprintf_ctx_t *)ctx;
    if (s->pos < s->max - 1) {
        s->buf[s->pos] = c;
    }
    s->pos++;
}

int vsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap) {
    snprintf_ctx_t ctx = { .buf = buf, .pos = 0, .max = n };
    int ret = kvprintf(snprintf_putchar, &ctx, fmt, ap);
    if (n > 0) buf[MIN(ctx.pos, n - 1)] = '\0';
    return ret;
}

int snprintf(char *buf, size_t n, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int ret = vsnprintf(buf, n, fmt, ap);
    __builtin_va_end(ap);
    return ret;
}
