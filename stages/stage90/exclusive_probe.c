/* Stage90 exclusive-monitor probe (Phase 1)
 *
 * This probe changes no mapping and no cache bit of its own. It operates entirely on one word
 * of the payload's own .bss and reports what LDREX/STREX actually do here, along with the
 * environment it observed - SCTLR, TTBR0 and whether the MMU was even on.
 *
 * The discriminator, and why it changed (2026-09-17, experiment-96)
 * -----------------------------------------------------------------
 * The original discriminating test was T3: "a plain store to the reserved address between
 * LDREX and STREX clears the exclusive monitor, so a correct monitor must then make STREX
 * fail". That premise is FALSE on MSM8974/Krait. On real hardware the plain store does not
 * clear the local monitor, T3 therefore reports success, and the probe concluded
 * exclusives_usable=0 - for four hardware runs, across Strongly-ordered, Normal-Non-cacheable
 * and Normal-Write-Back-plus-D-cache configurations, all of which were in fact fine.
 *
 * The right discriminator is CLREX, which is the mechanism XNU itself uses
 * (osfmk/arm/atomic.h: clear_exclusive() = __builtin_arm_clrex, and osfmk/arm/locks.h's
 * wait_for_event()). T5 does LDREX -> CLREX -> STREX and it FAILS on this part, which is
 * positive evidence that the local monitor is implemented and tracking. So:
 *
 *   T2 succeeds, T5 fails -> the monitor works. This is the criterion, in monitor_clears.
 *   T3 is now reported as an observation only (monitor_tracks), not as a criterion: it
 *   measures this implementation's clearing rules, not whether exclusivity works.
 *
 * What the phases are for
 * -----------------------
 * Phase 1 runs before mmu_identity_selftest(), i.e. before SCTLR.M is ever set; the first
 * version of this probe did that without recording it, so its result was uninterpretable -
 * with the MMU off ARMv7 treats every access as Strongly-ordered and no descriptor applies.
 * Phase 2 repeats the identical tests with the MMU on and this build's attribute mode live.
 * Phase 3 repeats them once more on a cacheable mapping with the D-cache on and ACTLR.SMP
 * raised, because "the monitor only works on cacheable memory" was the leading explanation
 * for the false negative and had to be tested rather than assumed.
 */

#include "stage90.h"

static struct stage90_exclusive_probe_result g_result_phase1;
static struct stage90_exclusive_probe_result g_result_phase2;
static struct stage90_exclusive_probe_result g_result_phase3;

/* T6's second address. File scope so the compiler cannot elide the store as a dead local. */
static volatile uint32_t g_probe_other_word;

static inline uint32_t probe_read_cpsr(void)
{
    uint32_t v;
    __asm__ volatile ("mrs %0, cpsr" : "=r"(v));
    return v;
}

static inline uint32_t probe_read_sctlr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t probe_read_ttbr0(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
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

static void stage90_exclusive_probe_log(const struct stage90_exclusive_probe_result *r,
                                        const char *prefix)
{
    xnu_log_puts("stage90 exclusive probe result:\n");
    xnu_log_kv32_p(prefix, "version", r->version);
    xnu_log_kv32_p(prefix, "status", r->status);
    xnu_log_kv32_p(prefix, "phase", r->phase);
    xnu_log_kv32_p(prefix, "mmu_enabled", r->mmu_enabled);
    xnu_log_kv32_p(prefix, "sctlr", r->sctlr);
    xnu_log_kv32_p(prefix, "ttbr0", r->ttbr0);
    xnu_log_kv32_p(prefix, "target_addr", r->target_addr);
    xnu_log_kv32_p(prefix, "cpsr_entry", r->cpsr_entry);
    xnu_log_kv32_p(prefix, "ldrex_reads_word", r->ldrex_reads_word);
    xnu_log_kv32_p(prefix, "undisrupted_strex_status", r->undisrupted_strex_status);
    xnu_log_kv32_p(prefix, "disrupted_strex_status", r->disrupted_strex_status);
    xnu_log_kv32_p(prefix, "clrex_strex_status", r->clrex_strex_status);
    xnu_log_kv32_p(prefix, "other_addr_strex_status", r->other_addr_strex_status);
    xnu_log_kv32_p(prefix, "monitor_tracks", r->monitor_tracks);
    xnu_log_kv32_p(prefix, "monitor_clears", r->monitor_clears);
    xnu_log_kv32_p(prefix, "iterations", r->iterations);
    xnu_log_kv32_p(prefix, "success_count", r->success_count);
    xnu_log_kv32_p(prefix, "fail_count", r->fail_count);
    xnu_log_kv32_p(prefix, "value_final", r->value_final);
    xnu_log_kv32_p(prefix, "value_expected", r->value_expected);
    xnu_log_kv32_p(prefix, "exclusives_usable", r->exclusives_usable);
    xnu_log_kv32_p(prefix, "checksum", r->checksum);
}

static int probe_run_into(struct stage90_exclusive_probe_result *r, uint32_t phase,
                          const char *prefix, const char *banner, volatile uint32_t *probe_word)
{
    uint32_t i;

    memset(r, 0, sizeof(*r));
    r->version = STAGE90_EXCLUSIVE_PROBE_VERSION;
    r->size = sizeof(*r);
    r->status = STAGE90_STATUS_BASE;
    r->magic = STAGE90_EXCLUSIVE_PROBE_MAGIC;
    r->iterations = STAGE90_EXCLUSIVE_PROBE_ITERATIONS;
    r->target_addr = (uint32_t)(uintptr_t)probe_word;
    r->phase = phase;

    xnu_log_puts(banner);

    /*
     * Record the environment before touching anything: whether the MMU is on,
     * which table is live, and the cache bits. A probe result without these is
     * not interpretable - that is the mistake this field set exists to prevent.
     */
    r->sctlr = probe_read_sctlr();
    r->ttbr0 = probe_read_ttbr0();
    r->mmu_enabled = (r->sctlr & 1u) ? 1u : 0u;

    /*
     * Run the tight sequences with IRQs masked. A timer tick landing between
     * LDREX and STREX legitimately clears the exclusive monitor, which is real
     * behaviour but would confound T2/T3 - the point here is the monitor itself.
     * The dead-man budget is 10s and these loops take microseconds.
     */
    r->cpsr_entry = probe_read_cpsr();
    probe_disable_irq();

    /* T1: LDREX must actually read the stored word. */
    *probe_word = STAGE90_EXCLUSIVE_PROBE_SEED;
    __asm__ volatile ("dsb sy" ::: "memory");
    r->ldrex_reads_word = (probe_ldrex(probe_word) == STAGE90_EXCLUSIVE_PROBE_SEED) ? 1u : 0u;

    /* T2: an undisrupted LDREX/STREX pair must succeed and update the word. */
    {
        uint32_t observed = probe_ldrex(probe_word);
        uint32_t next = observed + 1u;
        r->undisrupted_strex_status = probe_strex(next, probe_word);
    }

    /* T3: a plain store between LDREX and STREX must make STREX fail. */
    {
        uint32_t observed = probe_ldrex(probe_word);
        uint32_t next = observed + 1u;
        *probe_word = STAGE90_EXCLUSIVE_PROBE_DISRUPT; /* plain store clears the monitor */
        __asm__ volatile ("dsb sy" ::: "memory");
        r->disrupted_strex_status = probe_strex(next, probe_word);
    }

    /*
     * T5: CLREX between LDREX and STREX must make STREX fail. This is the test that decides
     * between two very different readings of a T3 failure:
     *
     *   T3 fails, T5 fails -> no local monitor at all: nothing built on exclusives can work.
     *   T3 fails, T5 passes -> the monitor IS implemented and CLREX clears it (which is the
     *                         mechanism XNU uses, via clear_exclusive()), and the only wrong
     *                         thing is T3's premise, that a plain store to the reserved address
     *                         clears the monitor on this implementation.
     *
     * XNU's own atomics use CLREX (osfmk/arm/atomic.h: clear_exclusive()), so T5 is the
     * test that corresponds to what the kernel actually does.
     */
    {
        uint32_t observed = probe_ldrex(probe_word);
        uint32_t next = observed + 1u;
        __asm__ volatile ("clrex" ::: "memory");
        r->clrex_strex_status = probe_strex(next, probe_word);
    }

    /*
     * T6: informational. A store to a different address between LDREX and STREX; whether that
     * clears the local monitor is implementation-defined, so this is reported and not judged.
     */
    {
        uint32_t observed = probe_ldrex(probe_word);
        uint32_t next = observed + 1u;
        g_probe_other_word = 0x1234u;
        __asm__ volatile ("dsb sy" ::: "memory");
        r->other_addr_strex_status = probe_strex(next, probe_word);
    }

    /* T4: count successes over the loop and cross-check against the final value. */
    *probe_word = 0u;
    __asm__ volatile ("dsb sy" ::: "memory");
    for (i = 0u; i < r->iterations; i++) {
        uint32_t observed = probe_ldrex(probe_word);
        uint32_t status = probe_strex(observed + 1u, probe_word);

        if (status == 0u) {
            r->success_count++;
        } else {
            r->fail_count++;
        }
    }
    __asm__ volatile ("dsb sy" ::: "memory");
    r->value_final = *probe_word;
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
    r->monitor_clears = ((r->undisrupted_strex_status == 0u) &&
                         (r->clrex_strex_status != 0u)) ? 1u : 0u;
    r->exclusives_usable = (r->ldrex_reads_word == 1u &&
                            r->monitor_clears == 1u &&
                            r->value_final == r->value_expected) ? 1u : 0u;

    /*
     * Status describes whether the probe ran and produced a self-consistent
     * observation - not whether exclusives work. Whether they work is the
     * finding, and it is recorded in exclusives_usable and monitor_tracks.
     */
    r->status = (r->ldrex_reads_word == 1u && r->value_final == r->value_expected) ?
        STAGE90_STATUS_OK : STAGE90_STATUS_BASE;

    r->checksum = stage90_exclusive_probe_checksum(r);
    stage90_exclusive_probe_log(r, prefix);

    return (r->status == STAGE90_STATUS_OK) ? 0 : -1;
}

int stage90_exclusive_probe_run(void)
{
    static volatile uint32_t probe_word;

    return probe_run_into(&g_result_phase1, 1u, "exclusive_probe_",
                          "stage90 exclusive probe: begin, phase 1 (before the MMU is enabled; "
                          "all accesses are Strongly-ordered, so the attribute mode cannot apply)\n",
                          &probe_word);
}

int stage90_exclusive_probe_run_mmu_on(void)
{
    static volatile uint32_t probe_word;

    return probe_run_into(&g_result_phase2, 2u, "exclusive_probe_mmu_on_",
                          "stage90 exclusive probe: begin, phase 2 (MMU on, identity table, "
                          "this build's attribute mode is live)\n",
                          &probe_word);
}

/*
 * Phase 3: the same four tests on a cacheable mapping, with the D-cache enabled.
 *
 * What this is for. Phase 2 settled that Normal-Non-cacheable DRAM does not make the monitor
 * track, with the descriptor verified live in the log. The remaining explanation for "STREX
 * always succeeds" is that the implementation only monitors cacheable accesses - so the test
 * is to give the probe word a Normal Write-Back Write-Allocate mapping and turn SCTLR.C on.
 *
 * Blast radius. Exactly one 1 MB section (PA 0x00200000, which the identity table does not
 * map) is made cacheable; everything else stays Normal-Non-cacheable, so nothing else can be
 * cached and no other part of the payload needs cache maintenance. The mapping and SCTLR.C
 * are both restored before returning, so the payload after this point is exactly as it was.
 *
 * Fail-closed. The mapping is verified by write-read-back before any test runs, and every step
 * that changes state records what it changed. If the mapping does not take, the tests are
 * skipped and the result says so rather than reporting a measurement taken through a fault.
 */
static uint32_t probe_read_actlr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 1" : "=r"(v));
    return v;
}

static void probe_write_actlr(uint32_t v)
{
    __asm__ volatile ("mcr p15, 0, %0, c1, c0, 1" :: "r"(v) : "memory");
}

static void probe_write_sctlr(uint32_t v)
{
    __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r"(v) : "memory");
}

static void probe_tlb_invalidate_va(uint32_t va)
{
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 1" :: "r"(va) : "memory");
}

static void probe_dcache_clean_invalidate_va(uint32_t va)
{
    __asm__ volatile ("mcr p15, 0, %0, c7, c14, 1" :: "r"(va) : "memory");
}

int stage90_exclusive_probe_run_dcache(void)
{
    struct stage90_exclusive_probe_result *r = &g_result_phase3;
    volatile uint32_t *carve = (volatile uint32_t *)(uintptr_t)STAGE90_EXCLUSIVE_PROBE_DC_PA;
    uint32_t *l1 = mmu_l1_table();
    const uint32_t index = STAGE90_EXCLUSIVE_PROBE_DC_PA >> 20;
    uint32_t saved_entry;
    uint32_t sctlr_before, sctlr_after;
    uint32_t actlr_before, actlr_after;
    uint32_t actlr_raised = 0u;
    uint32_t readback;

    xnu_log_puts("stage90 exclusive probe: begin, phase 3 (cacheable mapping + D-cache on)\n");
    xnu_log_kv32("exclusive_probe_dcache_carve_pa", STAGE90_EXCLUSIVE_PROBE_DC_PA);
    xnu_log_kv32("exclusive_probe_dcache_l1_index", index);
    xnu_log_kv32("exclusive_probe_dcache_actlr", probe_read_actlr());
    xnu_log_kv32("exclusive_probe_dcache_actlr_smp_bit", (probe_read_actlr() >> 6) & 1u);

    if ((probe_read_sctlr() & 1u) == 0u) {
        xnu_log_puts("exclusive_probe_dcache: the MMU is off - phase 3 needs a live mapping, skipped\n");
        return -1;
    }

    saved_entry = l1[index];
    xnu_log_kv32("exclusive_probe_dcache_l1_entry_before", saved_entry);
    if (saved_entry != 0u) {
        /*
         * Something already maps VA 0x00200000. Overwriting it would change a mapping the rest
         * of the payload may be using, so do not: report and leave.
         */
        xnu_log_puts("exclusive_probe_dcache: VA already mapped - refusing to touch it, skipped\n");
        return -1;
    }

    l1[index] = (STAGE90_EXCLUSIVE_PROBE_DC_PA & 0xfff00000u) | STAGE90_EXCLUSIVE_PROBE_DC_DESC;
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
    probe_tlb_invalidate_va(STAGE90_EXCLUSIVE_PROBE_DC_PA);
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");

    /*
     * Verify the mapping the way the loader preflight verifies its own: by using it. A wrong
     * descriptor would fault, and the data-abort handler resumes by skipping the faulting
     * instruction - so without this check the tests below would report on nothing.
     */
    *carve = STAGE90_EXCLUSIVE_PROBE_DC_MAGIC;
    __asm__ volatile ("dsb sy" ::: "memory");
    readback = *carve;
    xnu_log_kv32("exclusive_probe_dcache_readback", readback);
    if (readback != STAGE90_EXCLUSIVE_PROBE_DC_MAGIC) {
        xnu_log_puts("exclusive_probe_dcache: mapping did not take (write/read mismatch), skipped\n");
        l1[index] = saved_entry;
        __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
        probe_tlb_invalidate_va(STAGE90_EXCLUSIVE_PROBE_DC_PA);
        __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
        return -1;
    }

    /*
     * ACTLR.SMP. The first phase-3 run reported actlr=0x00000000 - the whole register is zero,
     * so bit 6 (SMP) is clear. That is the state aboot hands over in, and it is what a payload
     * that never touches ACTLR inherits. It matters for two reasons: on ARMv7 the SMP bit must
     * be set before the caches are enabled for them to be coherent, and it is the last standing
     * candidate for why the exclusive monitor does not track.
     *
     * So it is set here for the duration of the test and put back afterwards. Setting it is
     * required for a correct cache enable anyway; the report distinguishes the two runs so the
     * exclusive-monitor answer does not have to be inferred from a combined change.
     */
    actlr_before = probe_read_actlr();
    if ((actlr_before & STAGE90_EXCLUSIVE_PROBE_DC_ACTLR_SMP) == 0u) {
        __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
        probe_write_actlr(actlr_before | STAGE90_EXCLUSIVE_PROBE_DC_ACTLR_SMP);
        __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
        actlr_raised = 1u;
    }
    actlr_after = probe_read_actlr();
    xnu_log_kv32("exclusive_probe_dcache_actlr_before", actlr_before);
    xnu_log_kv32("exclusive_probe_dcache_actlr_after", actlr_after);
    xnu_log_kv32("exclusive_probe_dcache_actlr_raised", actlr_raised);

    sctlr_before = probe_read_sctlr();
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
    probe_write_sctlr(sctlr_before | STAGE90_EXCLUSIVE_PROBE_DC_SCTLR_D);
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
    sctlr_after = probe_read_sctlr();
    xnu_log_kv32("exclusive_probe_dcache_sctlr_before", sctlr_before);
    xnu_log_kv32("exclusive_probe_dcache_sctlr_after", sctlr_after);
    (void)probe_run_into(r, 3u, "exclusive_probe_dcache_",
                         "stage90 exclusive probe: phase 3 tests (cacheable word, D-cache on, "
                         "ACTLR.SMP set)\n",
                         carve);

    /* Restore: clean the dirty line out, drop the cache bit, drop ACTLR.SMP, drop the mapping. */
    probe_dcache_clean_invalidate_va(STAGE90_EXCLUSIVE_PROBE_DC_PA);
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
    probe_write_sctlr(sctlr_before);
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
    if (actlr_raised != 0u) {
        probe_write_actlr(actlr_before);
        __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
    }
    l1[index] = saved_entry;
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
    probe_tlb_invalidate_va(STAGE90_EXCLUSIVE_PROBE_DC_PA);
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");

    xnu_log_kv32("exclusive_probe_dcache_sctlr_restored", probe_read_sctlr());
    xnu_log_kv32("exclusive_probe_dcache_actlr_restored", probe_read_actlr());
    xnu_log_kv32("exclusive_probe_dcache_l1_entry_restored", l1[index]);
    xnu_log_puts("exclusive_probe_dcache: mapping, D-cache and ACTLR restored\n");

    return (r->status == STAGE90_STATUS_OK) ? 0 : -1;
}

const struct stage90_exclusive_probe_result *stage90_exclusive_probe_result(void)
{
    return &g_result_phase1;
}

const struct stage90_exclusive_probe_result *stage90_exclusive_probe_result_mmu_on(void)
{
    return &g_result_phase2;
}

const struct stage90_exclusive_probe_result *stage90_exclusive_probe_result_dcache(void)
{
    return &g_result_phase3;
}
