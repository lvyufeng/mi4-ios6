#include <stddef.h>
#include <stdint.h>

/*
 * Stage61 host-link support only.
 *
 * This object closes freestanding libc-like references in the tiny public-XNU
 * object subset so the host can produce a controlled ARM ELF link proof. It is
 * not a pexpert, pmap, scheduler, IOKit, or XNU runtime environment, and the
 * linked artifact is never executed on hardware.
 */

void *memset(void *dst, int c, size_t n)
{
    uint8_t *p = (uint8_t *)dst;
    while (n != 0u) {
        *p++ = (uint8_t)c;
        n--;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n != 0u) {
        *d++ = *s++;
        n--;
    }
    return dst;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n != 0u) {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (ca != cb || ca == '\0') {
            return (int)ca - (int)cb;
        }
        a++;
        b++;
        n--;
    }
    return 0;
}
