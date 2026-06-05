#include "stage27.h"

void xnu_log_puts(const char *s)
{
    log_puts("MI4IOS6_STAGE27_XNU ");
    log_puts(s);
}

void xnu_log_kv32(const char *key, uint32_t value)
{
    log_puts("MI4IOS6_STAGE27_XNU ");
    log_puts(key);
    log_puts("=");
    log_hex32(value);
    log_puts("\n");
}

static void log_hex32_raw(uint32_t v)
{
    static const char hex[] = "0123456789abcdef";
    char buf[9];
    for (unsigned i = 0; i < 8; i++) {
        unsigned shift = 28u - (i * 4u);
        buf[i] = hex[(v >> shift) & 0xfu];
    }
    buf[8] = 0;
    log_puts(buf);
}

void xnu_log_kv64(const char *key, uint64_t value)
{
    log_puts("MI4IOS6_STAGE27_XNU ");
    log_puts(key);
    log_puts("=0x");
    log_hex32_raw((uint32_t)(value >> 32));
    log_hex32_raw((uint32_t)value);
    log_puts("\n");
}
