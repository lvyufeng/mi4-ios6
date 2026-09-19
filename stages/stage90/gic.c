#include "stage90.h"

#define GICD_CTLR       0x000u
#define GICD_TYPER      0x004u
#define GICD_IIDR       0x008u
#define GICD_ISENABLER0 0x100u
#define GICD_ICENABLER0 0x180u
#define GICD_ISPENDR0   0x200u
#define GICD_ICPENDR0   0x280u
#define GICD_IPRIORITY0 0x400u
#define GICD_ITARGETS0  0x800u
#define GICD_SGIR       0xf00u

#define GICC_CTLR       0x000u
#define GICC_PMR        0x004u
#define GICC_BPR        0x008u
#define GICC_IAR        0x00cu
#define GICC_EOIR       0x010u
#define GICC_IIDR       0x0fcu

#define GICC_IAR_INTID_MASK 0x3ffu
#define GICC_SPURIOUS_ID    0x3ffu
#define GIC_SGI0_ID         0x000u
#define GICD_SGIR_TARGET_SELF (2u << 24)

/* msm8974.dtsi: timer interrupts = <1 2 0 1 3 0>; GIC xlate maps PPI n to INTID n + 16. */
#define GIC_TIMER_PPI0_ID   18u
#define GIC_TIMER_PPI1_ID   19u
#define GIC_TIMER_PPI_MASK  ((1u << GIC_TIMER_PPI0_ID) | (1u << GIC_TIMER_PPI1_ID))

#define CNTP_CTL_ENABLE     0x1u
#define CNTP_CTL_IMASK      0x2u
#define CNTP_CTL_ISTATUS    0x4u

struct gic_state_snapshot GIC_state_stage90;
volatile uint32_t stage90_irq_count;
volatile uint32_t stage90_last_iar;
volatile uint32_t stage90_last_irq_id;
volatile uint32_t stage90_sgi0_count;
volatile uint32_t stage90_spurious_irq_count;
volatile uint32_t stage90_timer_irq_count;
volatile uint32_t stage90_last_timer_irq_id;
volatile uint32_t stage90_last_timer_ctl;
volatile uint32_t stage90_other_irq_count;
volatile uint32_t stage90_sgi_selftest_passed;

/*
 * Stage90 PC-sampling ring buffer. Each timer/watchdog IRQ during XNU
 * execution records the PC XNU was about to execute (interrupted PC) so we
 * can see what loop XNU is stuck in. stage90_irq_sample_count is the number
 * of samples captured; the ring wraps mod STAGE90_IRQ_SAMPLE_RING_SIZE.
 */
#define STAGE90_IRQ_SAMPLE_RING_SIZE 16u
volatile uint32_t stage90_irq_sample_count;
volatile uint32_t stage90_irq_sample_ring[STAGE90_IRQ_SAMPLE_RING_SIZE];
/*
 * Sampling BUDGET, kept separate from the ring index above.
 *
 * They used to be the same variable, and that was a bug: the ring records every
 * interrupt (useful - it shows the PC at any interrupt), but the budget that decides when
 * the watchdog fires must count only the samples it actually asked for, i.e. timer
 * interrupts while in sample mode. With one variable, any interrupt consumed budget - so
 * a few stray interrupts made the next timer tick fire the watchdog early.
 *
 * The exposure was not theoretical. The handoff's sampling watchdog uses SAMPLE_MAX = 16
 * at 500us, an 8ms budget: sixteen unrelated interrupts would end the sampling and reboot
 * the device before it had sampled the target at all, which would look like "the jump hung
 * instantly". The dead-man uses 600 at 100ms and is less exposed, but wrong the same way.
 */
volatile uint32_t stage90_irq_sample_budget_used;
volatile uint32_t stage90_irq_last_sampled_pc;

/*
 * PC-sampling watchdog mode for the XNU handoff. When armed, the timer IRQ
 * handler does NOT shut the timer down; instead it re-arms the timer for
 * another sample interval until stage90_irq_sample_max samples are captured.
 * This lets us build a trace of where XNU executes across time.
 */
volatile uint32_t stage90_irq_sample_mode;        /* 0 = one-shot watchdog, 1 = periodic sampling */
volatile uint32_t stage90_irq_sample_interval_ticks;
volatile uint32_t stage90_irq_sample_max;
volatile uint32_t stage90_irq_sample_watchdog_fired;
volatile uint32_t stage90_sgi_irq_count_observed;
volatile uint32_t stage90_sgi_sgi0_count_observed;
volatile uint32_t stage90_sgi_last_irq_id_observed;
volatile uint32_t stage90_timer_selftest_passed;
volatile uint32_t stage90_timer_irq_count_observed;
volatile uint32_t stage90_timer_timer_count_observed;
volatile uint32_t stage90_timer_last_irq_id_observed;
volatile uint32_t stage90_timer_last_ctl_observed;

static inline uint32_t mmio_read32(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static inline void mmio_write32(uint32_t addr, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

static inline void barrier_dsb_isb(void)
{
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
}

static inline uint32_t read_cpsr(void)
{
    uint32_t v;
    __asm__ volatile ("mrs %0, cpsr" : "=r"(v));
    return v;
}

static inline void enable_irq_delivery(void)
{
    __asm__ volatile ("cpsie i\n\tisb" ::: "memory");
}

static inline void disable_irq_delivery(void)
{
    __asm__ volatile ("cpsid i\n\tisb" ::: "memory");
}

static inline uint32_t read_cntp_ctl(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c14, c2, 1" : "=r"(v));
    return v;
}

static inline void write_cntp_ctl(uint32_t v)
{
    __asm__ volatile ("mcr p15, 0, %0, c14, c2, 1" :: "r"(v) : "memory");
    __asm__ volatile ("isb" ::: "memory");
}

static inline uint32_t read_cntp_tval(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c14, c2, 0" : "=r"(v));
    return v;
}

static inline void write_cntp_tval(uint32_t v)
{
    __asm__ volatile ("mcr p15, 0, %0, c14, c2, 0" :: "r"(v) : "memory");
    __asm__ volatile ("isb" ::: "memory");
}

static uint32_t timer_usec_to_ticks(uint32_t usec)
{
    /* CNTFRQ is 19.2 MHz on cancro: ticks = usec * 19.2 = usec * 96 / 5. */
    return (usec * 96u) / 5u;
}

static void generic_timer_shutdown(void)
{
    uint32_t ctl = read_cntp_ctl();
    ctl |= CNTP_CTL_IMASK;
    ctl &= ~CNTP_CTL_ENABLE;
    write_cntp_ctl(ctl);
    barrier_dsb_isb();
}

void gic_readonly_snapshot(uint32_t dist_base, uint32_t cpu_base)
{
    memset(&GIC_state_stage90, 0, sizeof(GIC_state_stage90));
    GIC_state_stage90.distBase = dist_base;
    GIC_state_stage90.cpuBase = cpu_base;

    xnu_log_puts("gic snapshot begin (read-only)\n");

    xnu_log_puts("gic pre read GICD_CTLR\n");
    GIC_state_stage90.distCtlr = mmio_read32(dist_base + GICD_CTLR);
    xnu_log_puts("gic pre read GICD_TYPER\n");
    GIC_state_stage90.distTyper = mmio_read32(dist_base + GICD_TYPER);
    xnu_log_puts("gic pre read GICD_IIDR\n");
    GIC_state_stage90.distIidr = mmio_read32(dist_base + GICD_IIDR);

    GIC_state_stage90.irqCount = ((GIC_state_stage90.distTyper & 0x1fu) + 1u) * 32u;
    GIC_state_stage90.cpuInterfaceCount = ((GIC_state_stage90.distTyper >> 5) & 0x7u) + 1u;

    xnu_log_puts("gic pre read GICD_ISENABLER0\n");
    GIC_state_stage90.isenabler0 = mmio_read32(dist_base + GICD_ISENABLER0);
    xnu_log_puts("gic pre read GICD_ISPENDR0\n");
    GIC_state_stage90.ispendr0 = mmio_read32(dist_base + GICD_ISPENDR0);
    xnu_log_puts("gic pre read GICD_IPRIORITY0\n");
    GIC_state_stage90.priority0 = mmio_read32(dist_base + GICD_IPRIORITY0);
    xnu_log_puts("gic pre read GICD_ITARGETS0\n");
    GIC_state_stage90.targets0 = mmio_read32(dist_base + GICD_ITARGETS0);

    xnu_log_puts("gic pre read GICC_CTLR\n");
    GIC_state_stage90.cpuCtlr = mmio_read32(cpu_base + GICC_CTLR);
    xnu_log_puts("gic pre read GICC_PMR\n");
    GIC_state_stage90.cpuPmr = mmio_read32(cpu_base + GICC_PMR);
    xnu_log_puts("gic pre read GICC_BPR\n");
    GIC_state_stage90.cpuBpr = mmio_read32(cpu_base + GICC_BPR);
    xnu_log_puts("gic pre read GICC_IIDR\n");
    GIC_state_stage90.cpuIidr = mmio_read32(cpu_base + GICC_IIDR);

    xnu_log_puts("gic snapshot end (read-only)\n");
}

void gic_log_snapshot(void)
{
    xnu_log_puts("gic state summary begin\n");
    xnu_log_kv32("gic_distBase", GIC_state_stage90.distBase);
    xnu_log_kv32("gic_cpuBase", GIC_state_stage90.cpuBase);
    xnu_log_kv32("gic_distCtlr", GIC_state_stage90.distCtlr);
    xnu_log_kv32("gic_distTyper", GIC_state_stage90.distTyper);
    xnu_log_kv32("gic_distIidr", GIC_state_stage90.distIidr);
    xnu_log_kv32("gic_cpuIidr", GIC_state_stage90.cpuIidr);
    xnu_log_kv32("gic_cpuCtlr", GIC_state_stage90.cpuCtlr);
    xnu_log_kv32("gic_cpuPmr", GIC_state_stage90.cpuPmr);
    xnu_log_kv32("gic_cpuBpr", GIC_state_stage90.cpuBpr);
    xnu_log_kv32("gic_irqCount", GIC_state_stage90.irqCount);
    xnu_log_kv32("gic_cpuInterfaceCount", GIC_state_stage90.cpuInterfaceCount);
    xnu_log_kv32("gic_isenabler0", GIC_state_stage90.isenabler0);
    xnu_log_kv32("gic_ispendr0", GIC_state_stage90.ispendr0);
    xnu_log_kv32("gic_priority0", GIC_state_stage90.priority0);
    xnu_log_kv32("gic_targets0", GIC_state_stage90.targets0);
    xnu_log_puts("gic state summary end\n");
}

int gic_validate_snapshot(void)
{
    int ok = 1;
    ok &= (GIC_state_stage90.distBase == 0xf9000000u);
    ok &= (GIC_state_stage90.cpuBase == 0xf9002000u);
    ok &= (GIC_state_stage90.irqCount >= 288u);
    ok &= (GIC_state_stage90.cpuInterfaceCount == 4u);
    ok &= (GIC_state_stage90.distIidr != 0u);
    ok &= (GIC_state_stage90.cpuIidr != 0u);

    if (ok) {
        xnu_log_puts("gic validate ok\n");
    } else {
        xnu_log_puts("gic validate failed\n");
    }
    return ok;
}

void stage90_irq_c_handler(uint32_t interrupted_pc)
{
    const uint32_t cpu_base = GIC_state_stage90.cpuBase;
    const uint32_t iar = mmio_read32(cpu_base + GICC_IAR);
    const uint32_t intid = iar & GICC_IAR_INTID_MASK;
    const uint32_t count = stage90_irq_count + 1u;
    uint32_t sample_idx;

    stage90_irq_count = count;
    stage90_last_iar = iar;
    stage90_last_irq_id = intid;

    /*
     * Record the interrupted PC in the sampling ring buffer regardless of the
     * interrupt source. This is how we observe where XNU is executing when the
     * timer/watchdog fires.
     */
    stage90_irq_last_sampled_pc = interrupted_pc;
    sample_idx = stage90_irq_sample_count % STAGE90_IRQ_SAMPLE_RING_SIZE;
    stage90_irq_sample_ring[sample_idx] = interrupted_pc;
    stage90_irq_sample_count = stage90_irq_sample_count + 1u;

    if (intid == GIC_SGI0_ID) {
        stage90_sgi0_count++;
    } else if (intid == GIC_TIMER_PPI0_ID || intid == GIC_TIMER_PPI1_ID) {
        uint32_t ctl = read_cntp_ctl();
        stage90_timer_irq_count++;
        stage90_last_timer_irq_id = intid;
        stage90_last_timer_ctl = ctl;
        if (stage90_irq_sample_mode == 1u) {
            stage90_irq_sample_budget_used = stage90_irq_sample_budget_used + 1u;
            if (stage90_irq_sample_budget_used < stage90_irq_sample_max) {
                /* Periodic PC-sampling: re-arm for the next sample window. */
                write_cntp_tval(stage90_irq_sample_interval_ticks);
                write_cntp_ctl(CNTP_CTL_ENABLE);
                barrier_dsb_isb();
            } else {
                /*
                 * Sampling budget exhausted: stop the timer. Because the
                 * handoff target may be in a non-returning loop, this IRQ
                 * handler is the ONLY Stage code that runs after the jump, so
                 * dump the samples here and now, then reboot through the proven
                 * PS_HOLD path. The warm reboot preserves the RAM-console/
                 * ramoops buffer so Android can expose the dump through
                 * /proc/last_kmsg.
                 */
                stage90_irq_sample_watchdog_fired = 1u;
                generic_timer_shutdown();
                disable_irq_delivery();
                stage90_dump_pc_samples();
                mmio_write32(cpu_base + GICC_EOIR, iar);
                barrier_dsb_isb();
                xnu_log_puts("stage90 pc-sampling watchdog: rebooting after sample dump\n");
                platform_reboot();
            }
        } else {
            /* Normal timer selftest/non-sampling timer IRQ path. */
            generic_timer_shutdown();
        }
    } else if (intid == GICC_SPURIOUS_ID) {
        stage90_spurious_irq_count++;
    } else {
        stage90_other_irq_count++;
    }

    if (count <= 8u) {
        log_puts("MI4IOS6_STAGE90 irq handler iar=");
        log_hex32(iar);
        log_puts(" id=");
        log_hex32(intid);
        log_puts(" count=");
        log_hex32(count);
        log_puts(" timer_count=");
        log_hex32(stage90_timer_irq_count);
        log_puts("\n");
    }

    if (intid != GICC_SPURIOUS_ID) {
        mmio_write32(cpu_base + GICC_EOIR, iar);
        barrier_dsb_isb();
    }
}

static void reset_irq_counters(void)
{
    stage90_irq_count = 0;
    stage90_last_iar = 0xffffffffu;
    stage90_last_irq_id = 0xffffffffu;
    stage90_sgi0_count = 0;
    stage90_spurious_irq_count = 0;
    stage90_timer_irq_count = 0;
    stage90_last_timer_irq_id = 0xffffffffu;
    stage90_last_timer_ctl = 0xffffffffu;
    stage90_other_irq_count = 0;
    stage90_irq_sample_count = 0;
    stage90_irq_sample_budget_used = 0;
    stage90_irq_last_sampled_pc = 0xffffffffu;
    for (uint32_t i = 0u; i < STAGE90_IRQ_SAMPLE_RING_SIZE; i++) {
        stage90_irq_sample_ring[i] = 0u;
    }
}

int gic_sgi_selftest(void)
{
    const uint32_t dist_base = GIC_state_stage90.distBase;
    const uint32_t cpu_base = GIC_state_stage90.cpuBase;
    uint64_t start;

    xnu_log_puts("gic SGI selftest begin\n");

    if (!dist_base || !cpu_base) {
        xnu_log_puts("gic SGI selftest bad: missing bases\n");
        return 0;
    }

    reset_irq_counters();

    xnu_log_kv32("gic_sgi_distCtlr_before", mmio_read32(dist_base + GICD_CTLR));
    xnu_log_kv32("gic_sgi_cpuCtlr_before", mmio_read32(cpu_base + GICC_CTLR));
    xnu_log_kv32("gic_sgi_cpuPmr_before", mmio_read32(cpu_base + GICC_PMR));
    xnu_log_kv32("gic_sgi_isenabler0_before", mmio_read32(dist_base + GICD_ISENABLER0));
    xnu_log_kv32("gic_sgi_cpsr_before", read_cpsr());

    if ((mmio_read32(dist_base + GICD_CTLR) & 1u) == 0u) {
        xnu_log_puts("gic SGI selftest bad: distributor disabled\n");
        return 0;
    }
    if ((mmio_read32(cpu_base + GICC_CTLR) & 1u) == 0u) {
        xnu_log_puts("gic SGI selftest bad: cpu interface disabled\n");
        return 0;
    }

    /* Ensure SGI0 is enabled in the banked local SGI/PPI enable group. */
    mmio_write32(dist_base + GICD_ISENABLER0, 1u << GIC_SGI0_ID);
    barrier_dsb_isb();
    xnu_log_kv32("gic_sgi_isenabler0_after", mmio_read32(dist_base + GICD_ISENABLER0));

    /* Queue SGI0 to this CPU while CPSR.I is still masked, then open a short IRQ window. */
    xnu_log_puts("gic SGI selftest send SGI0 to self\n");
    mmio_write32(dist_base + GICD_SGIR, GICD_SGIR_TARGET_SELF | GIC_SGI0_ID);
    barrier_dsb_isb();

    enable_irq_delivery();
    start = timebase_ticks();
    while (stage90_sgi0_count == 0u && timebase_elapsed_us(start, timebase_ticks()) < 20000u) {
        __asm__ volatile ("nop" ::: "memory");
    }
    disable_irq_delivery();
    barrier_dsb_isb();

    xnu_log_kv32("gic_sgi_cpsr_after", read_cpsr());
    xnu_log_kv32("gic_sgi_irq_count", stage90_irq_count);
    xnu_log_kv32("gic_sgi_sgi0_count", stage90_sgi0_count);
    xnu_log_kv32("gic_sgi_spurious_count", stage90_spurious_irq_count);
    xnu_log_kv32("gic_sgi_last_iar", stage90_last_iar);
    xnu_log_kv32("gic_sgi_last_irq_id", stage90_last_irq_id);

    stage90_sgi_irq_count_observed = stage90_irq_count;
    stage90_sgi_sgi0_count_observed = stage90_sgi0_count;
    stage90_sgi_last_irq_id_observed = stage90_last_irq_id;

    if (stage90_sgi0_count > 0u) {
        stage90_sgi_selftest_passed = 1u;
        xnu_log_puts("gic SGI selftest ok\n");
        return 1;
    }

    stage90_sgi_selftest_passed = 0u;
    xnu_log_puts("gic SGI selftest failed: no SGI0 IRQ observed\n");
    return 0;
}

int gic_timer_selftest(void)
{
    const uint32_t dist_base = GIC_state_stage90.distBase;
    const uint32_t cpu_base = GIC_state_stage90.cpuBase;
    const uint32_t timer_ticks = timer_usec_to_ticks(5000u);
    const uint32_t enable_before = mmio_read32(dist_base + GICD_ISENABLER0);
    uint64_t start;

    xnu_log_puts("gic timer selftest begin\n");

    if (!dist_base || !cpu_base) {
        xnu_log_puts("gic timer selftest bad: missing bases\n");
        return 0;
    }
    if ((mmio_read32(dist_base + GICD_CTLR) & 1u) == 0u) {
        xnu_log_puts("gic timer selftest bad: distributor disabled\n");
        return 0;
    }
    if ((mmio_read32(cpu_base + GICC_CTLR) & 1u) == 0u) {
        xnu_log_puts("gic timer selftest bad: cpu interface disabled\n");
        return 0;
    }

    disable_irq_delivery();
    generic_timer_shutdown();
    reset_irq_counters();

    xnu_log_kv32("gic_timer_ppi0_id", GIC_TIMER_PPI0_ID);
    xnu_log_kv32("gic_timer_ppi1_id", GIC_TIMER_PPI1_ID);
    xnu_log_kv32("gic_timer_ppi_mask", GIC_TIMER_PPI_MASK);
    xnu_log_kv32("gic_timer_isenabler0_before", enable_before);
    xnu_log_kv32("gic_timer_ispendr0_before", mmio_read32(dist_base + GICD_ISPENDR0));
    xnu_log_kv32("gic_timer_priority_word", mmio_read32(dist_base + GICD_IPRIORITY0 + ((GIC_TIMER_PPI0_ID & ~3u))));
    xnu_log_kv32("gic_timer_cntp_ctl_before", read_cntp_ctl());
    xnu_log_kv32("gic_timer_cntp_tval_before", read_cntp_tval());
    xnu_log_kv32("gic_timer_ticks", timer_ticks);
    xnu_log_kv32("gic_timer_cpsr_before", read_cpsr());

    mmio_write32(dist_base + GICD_ISENABLER0, GIC_TIMER_PPI_MASK);
    barrier_dsb_isb();
    xnu_log_kv32("gic_timer_isenabler0_after", mmio_read32(dist_base + GICD_ISENABLER0));

    write_cntp_tval(timer_ticks);
    write_cntp_ctl(CNTP_CTL_ENABLE);
    barrier_dsb_isb();
    xnu_log_kv32("gic_timer_cntp_ctl_armed", read_cntp_ctl());
    xnu_log_kv32("gic_timer_cntp_tval_armed", read_cntp_tval());

    enable_irq_delivery();
    start = timebase_ticks();
    while (stage90_timer_irq_count == 0u && timebase_elapsed_us(start, timebase_ticks()) < 50000u) {
        __asm__ volatile ("nop" ::: "memory");
    }
    disable_irq_delivery();
    generic_timer_shutdown();
    barrier_dsb_isb();

    if ((enable_before & (1u << GIC_TIMER_PPI0_ID)) == 0u ||
        (enable_before & (1u << GIC_TIMER_PPI1_ID)) == 0u) {
        mmio_write32(dist_base + GICD_ICENABLER0, GIC_TIMER_PPI_MASK & ~enable_before);
        barrier_dsb_isb();
    }

    xnu_log_kv32("gic_timer_cpsr_after", read_cpsr());
    xnu_log_kv32("gic_timer_irq_count", stage90_irq_count);
    xnu_log_kv32("gic_timer_timer_count", stage90_timer_irq_count);
    xnu_log_kv32("gic_timer_other_count", stage90_other_irq_count);
    xnu_log_kv32("gic_timer_spurious_count", stage90_spurious_irq_count);
    xnu_log_kv32("gic_timer_last_iar", stage90_last_iar);
    xnu_log_kv32("gic_timer_last_irq_id", stage90_last_irq_id);
    xnu_log_kv32("gic_timer_last_timer_id", stage90_last_timer_irq_id);
    xnu_log_kv32("gic_timer_last_timer_ctl", stage90_last_timer_ctl);
    xnu_log_kv32("gic_timer_cntp_ctl_after", read_cntp_ctl());
    xnu_log_kv32("gic_timer_isenabler0_restored", mmio_read32(dist_base + GICD_ISENABLER0));

    stage90_timer_irq_count_observed = stage90_irq_count;
    stage90_timer_timer_count_observed = stage90_timer_irq_count;
    stage90_timer_last_irq_id_observed = stage90_last_timer_irq_id;
    stage90_timer_last_ctl_observed = stage90_last_timer_ctl;

    if (stage90_timer_irq_count > 0u &&
        (stage90_last_timer_irq_id == GIC_TIMER_PPI0_ID || stage90_last_timer_irq_id == GIC_TIMER_PPI1_ID)) {
        stage90_timer_selftest_passed = 1u;
        xnu_log_puts("gic timer selftest ok\n");
        return 1;
    }

    stage90_timer_selftest_passed = 0u;
    xnu_log_puts("gic timer selftest failed: no timer IRQ observed\n");
    return 0;
}

/*
 * Stage90 PC-sampling watchdog for the XNU handoff. Call arm() just before
 * jumping to XNU: it enables the GIC timer PPI, installs a periodic timer
 * that will fire every interval_us, and enables IRQ delivery so the timer
 * interrupts XNU and records its interrupted PC into the sample ring.
 *
 * The handoff runs with IRQs enabled. After max_samples PC samples (or if the
 * one-shot watchdog fires), the timer is shut down by the IRQ handler. stop()
 * unconditionally disables the timer and IRQs; dump() emits the captured
 * samples to the log.
 */
int stage90_arm_pc_sampling_watchdog(uint32_t interval_us, uint32_t max_samples)
{
    const uint32_t dist_base = GIC_state_stage90.distBase;
    const uint32_t cpu_base = GIC_state_stage90.cpuBase;
    const uint32_t interval_ticks = timer_usec_to_ticks(interval_us);

    xnu_log_puts("stage90 pc-sampling watchdog: arming\n");
    xnu_log_kv32("watchdog_interval_us", interval_us);
    xnu_log_kv32("watchdog_max_samples", max_samples);
    xnu_log_kv32("watchdog_interval_ticks", interval_ticks);

    /* Clear prior IRQ/sample state so the final dump only describes the handoff window. */
    disable_irq_delivery();
    generic_timer_shutdown();
    reset_irq_counters();
    stage90_irq_sample_watchdog_fired = 0u;
    stage90_irq_sample_interval_ticks = interval_ticks;
    stage90_irq_sample_max = max_samples;
    stage90_irq_sample_mode = 0u;

    if (!dist_base || !cpu_base) {
        xnu_log_puts("stage90 pc-sampling watchdog: ERROR missing GIC bases\n");
        return 0;
    }

    stage90_irq_sample_mode = 1u;

    /* Enable the timer PPIs at the distributor. */
    mmio_write32(dist_base + GICD_ISENABLER0, GIC_TIMER_PPI_MASK);
    barrier_dsb_isb();

    /* Arm the first sample window. */
    write_cntp_tval(interval_ticks);
    write_cntp_ctl(CNTP_CTL_ENABLE);
    barrier_dsb_isb();

    /* Enable IRQ delivery last so XNU runs interruptible. */
    enable_irq_delivery();
    xnu_log_kv32("watchdog_cntp_ctl_armed", read_cntp_ctl());
    return 1;
}

void stage90_stop_pc_sampling_watchdog(void)
{
    disable_irq_delivery();
    generic_timer_shutdown();
    stage90_irq_sample_mode = 0u;
    barrier_dsb_isb();
}

void stage90_dump_pc_samples(void)
{
    uint32_t total = stage90_irq_sample_count;
    uint32_t to_print = (total < STAGE90_IRQ_SAMPLE_RING_SIZE) ? total : STAGE90_IRQ_SAMPLE_RING_SIZE;

    xnu_log_puts("stage90 pc-samples: begin\n");
    xnu_log_kv32("pc_sample_total", total);
    xnu_log_kv32("pc_sample_budget_used", stage90_irq_sample_budget_used);
    xnu_log_kv32("pc_sample_watchdog_fired", stage90_irq_sample_watchdog_fired);
    xnu_log_kv32("pc_sample_timer_irq_count", stage90_timer_irq_count);
    xnu_log_kv32("pc_sample_last_pc", stage90_irq_last_sampled_pc);

    for (uint32_t i = 0u; i < to_print; i++) {
        xnu_log_kv32("pc_sample", stage90_irq_sample_ring[i]);
    }
    xnu_log_puts("stage90 pc-samples: end\n");
}

/*
 * Turn the instrument's own interrupt source off, immediately before the entry image takes over.
 *
 * `stage90_arm_deadman_reset` arms the dead-man by calling `stage90_arm_pc_sampling_watchdog`,
 * and that function does three things the *payload's* vector table is required to service: it
 * enables the timer PPIs at the distributor, starts the physical timer, and unmasks IRQs at the
 * CPU. Its IRQ path in this file is what records a sample and re-arms the window - and the moment
 * `_start` switches tables, that path is gone. The entry image says so about its own net, in the
 * header of its stub file:
 *
 *   "It does not arm the software dead-man across the jump. The dead-man needs the payload's GIC
 *    and vector state, and both are gone the moment `_start` switches tables; leaving it armed
 *    would be a claim that cannot be honoured."
 *
 * Experiment 308 is what leaving it armed actually costs, and the cost is worse than an
 * unhonourable claim: the next timer tick lands on the entry image's `fleh_irq`, which reports one
 * line and stops the machine. The run therefore ended three statements after the first real
 * context switch - the first interrupt the payload's own timer produced, not anything XNU did -
 * and the report could not say which interrupt it was.
 *
 * Disarming here removes nothing that was ever working. The dead-man is not a net across the jump
 * and cannot be: it needs a vector table and a GIC that the entry image replaces. The net across
 * the jump is the hardware watchdog, which is armed earlier in the same boot, is never touched
 * here, and - as its own log line says when it arms - is "independent of GIC, timer and IRQ
 * state". All this removes is an interrupt source belonging to *this project*, so that whatever
 * the run stops on next is XNU's and not the instrument's.
 *
 * Three levels, because each alone leaves the tick a way in:
 *
 *   1. the timer's own control - `CNTP_CTL.ENABLE` cleared and `IMASK` set, so it stops asserting
 *      at all (`generic_timer_shutdown` does this);
 *   2. the distributor's enable bit - cleared, so a tick already asserted cannot be delivered;
 *   3. the distributor's pending bit - cleared, so one latched before the timer stopped is not
 *      delivered either.
 *
 * CPU-level IRQ delivery is masked as well, but that is the one level that *cannot* be relied on
 * across the jump: the entry image's exception return restores the CPSR of the interrupted
 * context, so the I bit comes back from XNU's own state rather than from anything set here. The
 * first two levels are the ones that hold, which is why they are not optional.
 *
 * The before/after values are logged because "the interrupt stopped" and "the disarm did nothing"
 * produce the same log line otherwise - the run's next line would say `exception: irq` either way.
 */
int stage90_disarm_deadman_timer(void)
{
    const uint32_t dist_base = GIC_state_stage90.distBase;
    uint32_t enable_before;
    uint32_t pending_before;
    uint32_t cntp_before;

    if (!dist_base) {
        xnu_log_puts("stage90 disarm: no GIC distributor base; nothing to do\n");
        return 0;
    }

    enable_before = mmio_read32(dist_base + GICD_ISENABLER0);
    pending_before = mmio_read32(dist_base + GICD_ISPENDR0);
    cntp_before = read_cntp_ctl();

    /* 1. the timer itself, then 2 and 3 at the distributor. */
    stage90_stop_pc_sampling_watchdog();
    mmio_write32(dist_base + GICD_ICENABLER0, GIC_TIMER_PPI_MASK);
    mmio_write32(dist_base + GICD_ICPENDR0, GIC_TIMER_PPI_MASK);
    barrier_dsb_isb();

    xnu_log_puts("stage90 disarm: the instrument's timer PPI is off before the jump\n");
    xnu_log_kv32("disarm_ppi_mask", GIC_TIMER_PPI_MASK);
    xnu_log_kv32("disarm_isenabler0_before", enable_before);
    xnu_log_kv32("disarm_isenabler0_after", mmio_read32(dist_base + GICD_ISENABLER0));
    xnu_log_kv32("disarm_ispendr0_before", pending_before);
    xnu_log_kv32("disarm_ispendr0_after", mmio_read32(dist_base + GICD_ISPENDR0));
    xnu_log_kv32("disarm_cntp_ctl_before", cntp_before);
    xnu_log_kv32("disarm_cntp_ctl_after", read_cntp_ctl());
    /*
     * The hardware watchdog is the net that covers the jump, so whether it is still armed is part
     * of this step's own record rather than an assumption. It lives at a fixed SoC address and
     * `hw_watchdog.c` is what armed it; this reads its enable bit back through the same helper the
     * payload used to arm it, so the value is the SoC's and not a shadow copy.
     */
    xnu_log_kv32("disarm_hw_watchdog_en", stage90_hw_watchdog_enabled_readback());
    return 1;
}
