/*
 * Minimal freestanding 32-bit runtime for host-side execution of payload modules.
 *
 * Why 32-bit: the payload's own _Static_asserts encode the ARM ILP32 ABI (boot_args is 320
 * bytes with CommandLine at offset 56). Building the same translation units at -m32 makes
 * those asserts hold, so "it compiled" is itself part of the check rather than a formality
 * to be worked around.
 *
 * Why no libc: this host has no 32-bit libc headers, and none is needed. The modules under
 * test are freestanding, and the only things they want are memset, memcpy and the payload's
 * two logging calls - which are provided here, the last two writing to stdout through the
 * write syscall so their output is the test's output.
 */

#include <stddef.h>

typedef unsigned int u32;

static void sys_write(int fd, const void *buf, u32 n)
{
    __asm__ volatile ("int $0x80" :: "a"(4), "b"(fd), "c"(buf), "d"(n) : "memory");
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = d; const unsigned char *q = s;
    while (n--) *p++ = *q++;
    return d;
}

/* The payload's logging, to stdout. */
void xnu_log_puts(const char *s)
{
    u32 n = 0; while (s[n]) n++;
    sys_write(1, s, n);
}

static char kbuf[128];
void xnu_log_kv32(const char *k, u32 v)
{
    u32 i = 0, j;
    static const char hex[] = "0123456789abcdef";
    while (*k && i < 60) kbuf[i++] = *k++;
    kbuf[i++] = '='; kbuf[i++] = '0'; kbuf[i++] = 'x';
    for (j = 0; j < 8; j++) kbuf[i++] = hex[(v >> (28 - j * 4)) & 0xf];
    kbuf[i++] = '\n';
    sys_write(1, kbuf, i);
}

/* Returning from main falls off the end of a -nostdlib image; exit explicitly. */
void _exit_now(int code)
{
    __asm__ volatile ("int $0x80" :: "a"(1), "b"(code) : "memory");
    for (;;) { }
}
