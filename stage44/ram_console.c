#include "stage44.h"

static volatile struct persistent_ram_buffer *const rc =
    (volatile struct persistent_ram_buffer *)RAM_CONSOLE_BASE;

static void barrier(void)
{
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
}

void log_init(void)
{
    rc->sig = RAM_CONSOLE_SIG;
    rc->start = 0;
    rc->size = 0;
    barrier();
}

void log_puts(const char *s)
{
    uint32_t size = rc->size;
    uint32_t max = RAM_CONSOLE_SIZE - 12u;

    while (*s && size < max) {
        rc->data[size++] = (uint8_t)*s++;
    }

    rc->size = size;
    barrier();
}

void log_nl(void)
{
    log_puts("\n");
}

void log_hex32(uint32_t v)
{
    static const char hex[] = "0123456789abcdef";
    char buf[11];

    buf[0] = '0';
    buf[1] = 'x';
    for (unsigned i = 0; i < 8; i++) {
        unsigned shift = 28u - (i * 4u);
        buf[2 + i] = hex[(v >> shift) & 0xfu];
    }
    buf[10] = 0;
    log_puts(buf);
}

void log_kv32(const char *key, uint32_t value)
{
    log_puts("MI4IOS6_STAGE44 ");
    log_puts(key);
    log_puts("=");
    log_hex32(value);
    log_puts("\n");
}
