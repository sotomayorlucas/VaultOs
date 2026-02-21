#include "string.h"

void *memcpy(void *dst, const void *src, size_t n) {
    uint64_t *d64 = (uint64_t *)dst;
    const uint64_t *s64 = (const uint64_t *)src;
    while (n >= 8) { *d64++ = *s64++; n -= 8; }
    uint8_t *d8 = (uint8_t *)d64;
    const uint8_t *s8 = (const uint8_t *)s64;
    while (n--) *d8++ = *s8++;
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    uint8_t byte = (uint8_t)c;
    uint64_t word = (uint64_t)byte * 0x0101010101010101ULL;
    uint64_t *d64 = (uint64_t *)dst;
    while (n >= 8) { *d64++ = word; n -= 8; }
    uint8_t *d8 = (uint8_t *)d64;
    while (n--) *d8++ = byte;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    if (dst < src) {
        return memcpy(dst, src, n);
    }
    /* Copy backwards for overlapping regions where dst > src */
    uint8_t *d = (uint8_t *)dst + n;
    const uint8_t *s = (const uint8_t *)src + n;
    while (n && ((uintptr_t)d & 7)) { *--d = *--s; n--; }
    uint64_t *d64 = (uint64_t *)d;
    const uint64_t *s64 = (const uint64_t *)s;
    while (n >= 8) { *--d64 = *--s64; n -= 8; }
    d = (uint8_t *)d64;
    s = (const uint8_t *)s64;
    while (n--) *--d = *--s;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    while (n--) {
        if (*pa != *pb) return *pa - *pb;
        pa++;
        pb++;
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n == 0 ? 0 : (uint8_t)*a - (uint8_t)*b;
}

int strcasecmp(const char *a, const char *b) {
    while (*a && toupper(*a) == toupper(*b)) { a++; b++; }
    return (uint8_t)toupper(*a) - (uint8_t)toupper(*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    while (n && *a && toupper(*a) == toupper(*b)) { a++; b++; n--; }
    return n == 0 ? 0 : (uint8_t)toupper(*a) - (uint8_t)toupper(*b);
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = '\0';
    return dst;
}

char *strcat(char *dst, const char *src) {
    char *d = dst + strlen(dst);
    while ((*d++ = *src++));
    return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
    char *d = dst + strlen(dst);
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dst;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    size_t nlen = strlen(needle);
    while (*haystack) {
        if (strncmp(haystack, needle, nlen) == 0)
            return (char *)haystack;
        haystack++;
    }
    return NULL;
}

int atoi(const char *s) {
    int result = 0;
    int sign = 1;
    while (isspace(*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (isdigit(*s)) {
        result = result * 10 + (*s - '0');
        s++;
    }
    return sign * result;
}

uint64_t strtou64(const char *s, char **endptr, int base) {
    uint64_t result = 0;
    while (isspace(*s)) s++;

    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    while (*s) {
        int digit;
        if (isdigit(*s)) digit = *s - '0';
        else if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * base + digit;
        s++;
    }
    if (endptr) *endptr = (char *)s;
    return result;
}

char toupper(char c) {
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

char tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

bool isdigit(char c) { return c >= '0' && c <= '9'; }
bool isalpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isalnum(char c) { return isdigit(c) || isalpha(c); }
bool isspace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
