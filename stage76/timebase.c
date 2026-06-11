#include "stage76.h"

static uint32_t g_cntfrq;
static uint64_t g_boot_ticks;

static inline uint32_t read_cntfrq_reg(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r"(v));
    return v;
}

uint64_t timebase_ticks(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("mrrc p15, 0, %0, %1, c14" : "=r"(lo), "=r"(hi));
    return ((uint64_t)hi << 32) | lo;
}

uint32_t timebase_freq_hz(void)
{
    return g_cntfrq;
}

void timebase_init(void)
{
    g_cntfrq = read_cntfrq_reg();
    g_boot_ticks = timebase_ticks();
    log_kv32("timebase_cntfrq", g_cntfrq);
    log_kv32("timebase_boot_ticks_lo", (uint32_t)g_boot_ticks);
    log_kv32("timebase_boot_ticks_hi", (uint32_t)(g_boot_ticks >> 32));
}

/*
 * Stage3 proved CNTFRQ is 19,200,000 Hz on cancro. For short intervals used
 * here, low 32-bit deltas are enough. us ~= ticks * 1,000,000 / 19,200,000
 * == ticks * 5 / 96. This avoids a runtime 64-bit division helper.
 */
uint32_t timebase_elapsed_us(uint64_t start, uint64_t end)
{
    uint32_t delta = (uint32_t)(end - start);
    return (delta * 5u) / 96u;
}

static uint32_t usec_to_ticks(uint32_t usec)
{
    /* ticks = usec * 19.2 = usec * 96 / 5 */
    return (usec * 96u) / 5u;
}

void delay_us(uint32_t usec)
{
    uint32_t target = usec_to_ticks(usec);
    uint64_t start = timebase_ticks();
    while ((uint32_t)(timebase_ticks() - start) < target) {
        __asm__ volatile ("nop");
    }
}

void run_timebase_selftest(void)
{
    uint64_t a, b, c;

    log_puts("MI4IOS6_STAGE76 timebase selftest begin\n");
    log_kv32("timebase_freq_hz", timebase_freq_hz());

    a = timebase_ticks();
    delay_us(1000);
    b = timebase_ticks();
    delay_us(5000);
    c = timebase_ticks();

    log_kv32("timebase_a_lo", (uint32_t)a);
    log_kv32("timebase_b_lo", (uint32_t)b);
    log_kv32("timebase_c_lo", (uint32_t)c);
    log_kv32("delay_1000us_measured_us", timebase_elapsed_us(a, b));
    log_kv32("delay_5000us_measured_us", timebase_elapsed_us(b, c));

    if ((uint32_t)(b - a) >= usec_to_ticks(900) && (uint32_t)(c - b) >= usec_to_ticks(4500)) {
        log_puts("MI4IOS6_STAGE76 timebase selftest ok\n");
    } else {
        log_puts("MI4IOS6_STAGE76 timebase selftest suspicious\n");
    }
}
