#include "stage90.h"

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
 * Stage3 proved CNTFRQ is 19,200,000 Hz on cancro. us ~= ticks * 1,000,000 /
 * 19,200,000 == ticks * 5 / 96.
 *
 * **The range of the product is load-bearing, and so is the width of the division.**
 * `delta` is the *low 32 bits* of the tick delta, so it runs to 2^32 ticks - 223s at
 * 19.2MHz - and a 32-bit `delta * 5u` wrapped at 858,993,459 ticks, i.e. **44.74s**. The wrap
 * is not monotonic: the product cycles, so nothing on the caller's side can compensate for it.
 * This line used to say "for short intervals used here, low 32-bit deltas are enough", which
 * was true of every caller except the one that mattered - `stage90_selftest_bounded_spin`'s
 * 90s deadline. There the wrap made the guard *unsatisfiable* rather than early: this function
 * could not return any value >= the deadline, so the "bounded" spin never broke and the
 * PS_HOLD fallback behind it was dead code. Measured on the built object, the 32-bit form was
 * `add r0, r0, r0, lsl #2` (a 32-bit *5) then a reciprocal divide, whose largest possible
 * return is 4294967295/96 = 44,739,242us.
 *
 * The obvious repair - `(uint64_t)delta * 5u / 96u` - **does not link here, and that is
 * measured rather than assumed**: GCC 10.3 emits a call to `__aeabi_uldivmod` for a 64-bit
 * dividend, and this payload is freestanding, so the link fails with
 * `undefined reference to __aeabi_uldivmod` in every function that used the widening. The old
 * comment's worry about "a runtime 64-bit division helper" was therefore correct - it was the
 * *product* that never needed the width, not the division.
 *
 * So the division stays 32-bit and is made exact by splitting `delta` at the divisor:
 * `delta = 96q + r` gives `delta * 5 = 480q + 5r`, and `480q / 96 = 5q` exactly, so
 * `floor(delta * 5 / 96) = 5q + floor(5r / 96)`. Both terms are small: `q <= 44,739,242`, so
 * `5q <= 223,696,210`, and `5r < 480`. The result spans the full range the `uint32_t` return
 * can hold - (2^32 - 1) ticks = 223,696,213us, and this decomposition reaches it - while the
 * `/ 96u` and `% 96u` stay the reciprocal sequence GCC already generates. Both claims are
 * measured, not reasoned: the decomposition was checked against a 64-bit reference
 * exhaustively, over all 2^32 inputs, with zero mismatches.
 */
uint32_t timebase_elapsed_us(uint64_t start, uint64_t end)
{
    const uint32_t delta = (uint32_t)(end - start);
    const uint32_t q = delta / 96u;
    const uint32_t r = delta % 96u;
    return q * 5u + (r * 5u) / 96u;
}

/*
 * ticks = usec * 19.2 = usec * 96 / 5, split at the divisor for the same reason and to the
 * same end: `usec * 96u` in 32 bits wraps above 44,739,242us, and `usec` here is a
 * caller-supplied interval. `usec = 5q + r` gives `usec * 96 = 480q + 96r`, and `480q / 5 =
 * 96q` exactly, so `floor(usec * 96 / 5) = 96q + floor(96r / 5)` - `96r < 480` and `96q`
 * reaches 4,294,967,270, which is where the 32-bit tick count runs out. Above 223,696,213us
 * the answer is not representable in the return type at all, which is also where `delay_us`'s
 * own comparison stops meaning anything.
 *
 * **This is the only definition of the conversion.** `gic.c` used to carry a second, identical
 * `timer_usec_to_ticks`, which is how a fix in one place would have left the other wrong.
 */
uint32_t timebase_usec_to_ticks(uint32_t usec)
{
    const uint32_t q = usec / 5u;
    const uint32_t r = usec % 5u;
    return q * 96u + (r * 96u) / 5u;
}

void delay_us(uint32_t usec)
{
    uint32_t target = timebase_usec_to_ticks(usec);
    uint64_t start = timebase_ticks();
    while ((uint32_t)(timebase_ticks() - start) < target) {
        __asm__ volatile ("nop");
    }
}

void run_timebase_selftest(void)
{
    uint64_t a, b, c;

    log_puts("MI4IOS6_STAGE90 timebase selftest begin\n");
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

    if ((uint32_t)(b - a) >= timebase_usec_to_ticks(900) && (uint32_t)(c - b) >= timebase_usec_to_ticks(4500)) {
        log_puts("MI4IOS6_STAGE90 timebase selftest ok\n");
    } else {
        log_puts("MI4IOS6_STAGE90 timebase selftest suspicious\n");
    }
}
