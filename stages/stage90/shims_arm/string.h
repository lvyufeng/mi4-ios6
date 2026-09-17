#ifndef MI4IOS6_SHIM_STRING_H
#define MI4IOS6_SHIM_STRING_H
/*
 * XNU's kernel sources include <string.h> for the memory- and string-function declarations,
 * which the kernel build provides from libkern. There is no such header for a bare-metal ARM
 * target on this host, and pointing at newlib's is worse than not having one: newlib's
 * sys/reent.h defines _mbstate_t and _off_t, which collide with the Darwin types XNU's own
 * headers bring in.
 *
 * Declarations only. This is the same approach shims/libkern/OSAtomic.h already takes for the
 * atomic layer, and for the same reason: the kernel supplies these, not libc.
 */
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strncpy(char *dst, const char *src, size_t n);
char *strcpy(char *dst, const char *src);

#endif
