#ifndef VAULTOS_STRING_H
#define VAULTOS_STRING_H

#include "types.h"

void   *memcpy(void *dst, const void *src, size_t n);
void   *memset(void *dst, int c, size_t n);
void   *memmove(void *dst, const void *src, size_t n);
int     memcmp(const void *a, const void *b, size_t n);

size_t  strlen(const char *s);
int     strcmp(const char *a, const char *b);
int     strncmp(const char *a, const char *b, size_t n);
int     strcasecmp(const char *a, const char *b);
int     strncasecmp(const char *a, const char *b, size_t n);
char   *strcpy(char *dst, const char *src);
char   *strncpy(char *dst, const char *src, size_t n);
char   *strcat(char *dst, const char *src);
char   *strncat(char *dst, const char *src, size_t n);
char   *strchr(const char *s, int c);
char   *strstr(const char *haystack, const char *needle);

int     atoi(const char *s);
uint64_t strtou64(const char *s, char **endptr, int base);

char    toupper(char c);
char    tolower(char c);
bool    isdigit(char c);
bool    isalpha(char c);
bool    isalnum(char c);
bool    isspace(char c);

#endif /* VAULTOS_STRING_H */
