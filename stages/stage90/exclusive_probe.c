/* Stage90 exclusive-monitor probe (Phase 1 baseline)
 *
 * ARMv7 defines LDREX/STREX only on Normal memory. Every mapping this project
 * builds is Strongly-Ordered and non-cacheable - there is no Normal descriptor
 * anywhere in the tree - so before changing the attribute map (roadmap Phase 1)
 * it is worth knowing what exclusives actually do here today, rather than
 * assuming the architecture's "UNPREDICTABLE" means "always broken".
 *
 * This probe changes no mapping and no cache bit. It operates entirely on one
 * word of the payload's own .bss, under whatever mapping is already live, and
 * reports what happened. It is the baseline the attribute-map change has to be
 * compared against.
 *
 * The discriminating test is T3. A naive implementation could return "success"
 * from STREX unconditionally; T3 clears the exclusive monitor with a plain store
 * between LDREX and STREX, so a correct exclusive monitor must then make STREX
 * fail. If T3 reports success, STREX is not actually tracking exclusivity and
 * nothing built on it is safe.
 */

#include "stage90.h"

static struct stage90_exclusive_probe_result g_result;

static inline uint32_t probe_read_cpsr(void)
{
    uint32_t v;
    __asm__ volatile ("mrs %0, cpsr" : "=r"(v));
    return v;
}

static inline void probe_disable_irq(void)
{
    __asm__ volatile ("cpsid i\n\tisb" ::: "memory");
}

static inline void probe_enable_irq(void)
{
    __asm__ volatile ("cpsie i\n\tisb" ::: "memory");
}

static inline uint32_t probe_ldrex(volatile uint32_t *p)
{
    uint32_t v;
    __asm__ volatile ("ldrex %0, [%1]" : "=r"(v) : "r"(p) : "memory");
    return v;
}

static inline uint32_t probe_strex(uint32_t value, volatile uint32_t *p)
{
    uint32_t status;

    /* Early-clobber: STREX requires Rd != Rt, so the status output must not
     * share a register with the value input. */
    __asm__ volatile ("strex %0, %1, [%2]"
                      : "=&r"(status)
                      : "r"(value), "r"(p)
                      : "cc", "memory");
    return status;
}

static uint32_t stage90_exclusive_probe_checksum(const struct stage90_exclusive_probe_result *r)
{
    const uint32_t *words = (const uint32_t *)r;
    uint32_t count = (uint32_t)(offsetof(struct stage90_exclusive_probe_result, checksum) / sizeof(uint32_t));
    uint32_t chk = 0u;

    for (uint32_t i = 0u; i < count; i++) {
        chk ^= words[i];
    }
    return chk;
}

static void stage90_exclusive_probe_log(const struct stage90_exclusive_probe_result *r)
{
    xnu_log_puts("stage90 exclusive probe result:\n");
    xnu_log_kv32("exclusive_probe_version", r->version);
    xnu_log_kv32("exclusive_probe_status", r->status);
    xnu_log_kv32("exclusive_probe_target_addr", r->target_addr);
    xnu_log_kv32("exclusive_probe_cpsr_entry", r->cpsr_entry);
    xnu_log_kv32("exclusive_probe_ldrex_reads_word", r->ldrex_reads_word);
    xnu_log_kv32("exclusive_probe_undisrupted_strex_status", r->undisrupted_strex_status);
    xnu_log_kv32("exclusive_probe_disrupted_strex_status", r->disrupted_strex_status);
    xnu_log_kv32("exclusive_probe_monitor_tracks", r->monitor_tracks);
    xnu_log_kv32("exclusive_probe_iterations", r->iterations);
    xnu_log_kv32("exclusive_probe_success_count", r->success_count);
    xnu_log_kv32("exclusive_probe_fail_count", r->fail_count);
    xnu_log_kv32("exclusive_probe_value_final", r->value_final);
    xnu_log_kv32("exclusive_probe_value_expected", r->value_expected);
    xnu_log_kv32("exclusive_probe_exclusives_usable", r->exclusives_usable);
    xnu_log_kv32("exclusive_probe_checksum", r->checksum);
}

int stage90_exclusive_probe_run(void)
{
    struct stage90_exclusive_probe_result *r = &g_result;
    static volatile uint32_t probe_word;
    uint32_t i;

    memset(r, 0, sizeof(*r));
    r->version = STAGE90_EXCLUSIVE_PROBE_VERSION;
    r->size = sizeof(*r);
    r->status = STAGE90_STATUS_BASE;
    r->magic = STAGE90_EXCLUSIVE_PROBE_MAGIC;
    r->iterations = STAGE90_EXCLUSIVE_PROBE_ITERATIONS;
    r->target_addr = (uint32_t)(uintptr_t)&probe_word;

    xnu_log_puts("stage90 exclusive probe: begin (no mapping or cache change; payload .bss only)\n");

    /*
     * Run the tight sequences with IRQs masked. A timer tick landing between
     * LDREX and STREX legitimately clears the exclusive monitor, which is real
     * behaviour but would confound T2/T3 - the point here is the monitor itself.
     * The dead-man budget is 10s and these loops take microseconds.
     */
    r->cpsr_entry = probe_read_cpsr();
    probe_disable_irq();

    /* T1: LDREX must actually read the stored word. */
    probe_word = STAGE90_EXCLUSIVE_PROBE_SEED;
    __asm__ volatile ("dsb sy" ::: "memory");
    r->ldrex_reads_word = (probe_ldrex(&probe_word) == STAGE90_EXCLUSIVE_PROBE_SEED) ? 1u : 0u;

    /* T2: an undisrupted LDREX/STREX pair must succeed and update the word. */
    {
        uint32_t observed = probe_ldrex(&probe_word);
        uint32_t next = observed + 1u;
        r->undisrupted_strex_status = probe_strex(next, &probe_word);
    }

    /* T3: a plain store between LDREX and STREX must make STREX fail. */
    {
        uint32_t observed = probe_ldrex(&probe_word);
        uint32_t next = observed + 1u;
        probe_word = STAGE90_EXCLUSIVE_PROBE_DISRUPT; /* plain store clears the monitor */
        __asm__ volatile ("dsb sy" ::: "memory");
        r->disrupted_strex_status = probe_strex(next, &probe_word);
    }

    /* T4: count successes over the loop and cross-check against the final value. */
    probe_word = 0u;
    __asm__ volatile ("dsb sy" ::: "memory");
    for (i = 0u; i < r->iterations; i++) {
        uint32_t observed = probe_ldrex(&probe_word);
        uint32_t status = probe_strex(observed + 1u, &probe_word);

        if (status == 0u) {
            r->success_count++;
        } else {
            r->fail_count++;
        }
    }
    __asm__ volatile ("dsb sy" ::: "memory");
    r->value_final = probe_word;
    r->value_expected = r->success_count;

    /*
     * Restore the IRQ mask exactly as it was: only unmask if it was unmasked on
     * entry, so the probe can never leave interrupts in a state the caller did
     * not set up.
     */
    if ((r->cpsr_entry & STAGE90_EXCLUSIVE_PROBE_CPSR_I) == 0u) {
        probe_enable_irq();
    }

    /*
     * The monitor tracks exclusivity only if the disrupted pair failed and the
     * undisrupted one succeeded. T4 is consistent only if the final word equals
     * the number of successful increments, and every increment that reported
     * success must have landed.
     */
    r->monitor_tracks = ((r->undisrupted_strex_status == 0u) &&
                         (r->disrupted_strex_status != 0u)) ? 1u : 0u;
    r->exclusives_usable = (r->ldrex_reads_word == 1u &&
                            r->monitor_tracks == 1u &&
                            r->value_final == r->value_expected) ? 1u : 0u;

    /*
     * Status describes whether the probe ran and produced a self-consistent
     * observation - not whether exclusives work. Whether they work is the
     * finding, and it is recorded in exclusives_usable and monitor_tracks.
     */
    r->status = (r->ldrex_reads_word == 1u && r->value_final == r->value_expected) ?
        STAGE90_STATUS_OK : STAGE90_STATUS_BASE;

    r->checksum = stage90_exclusive_probe_checksum(r);
    stage90_exclusive_probe_log(r);

    return (r->status == STAGE90_STATUS_OK) ? 0 : -1;
}

const struct stage90_exclusive_probe_result *stage90_exclusive_probe_result(void)
{
    return &g_result;
}
