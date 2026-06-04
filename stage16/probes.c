#include "stage16.h"

/*
 * Stage16 read-only hardware probes.
 *
 * Every probe is strictly READ-ONLY. We never write GIC/timer/clock registers
 * here. The goal is to confirm we can observe real MSM8974 hardware state from
 * our own C runtime before any later stage attempts to *initialize* it.
 *
 * Safety strategy: before each MMIO read we emit a "pre" breadcrumb naming the
 * register. If a particular physical address faults or hangs, the recovered
 * /proc/last_kmsg will show exactly which read was attempted last. We also do
 * not touch any register whose read has side effects (for example the GIC
 * interrupt-acknowledge register GICC_IAR, which would ack/consume an IRQ).
 *
 * GIC (qcom,msm-qgic2 / GICv2):
 *   GICD distributor base  0xf9000000
 *   GICC cpu interface base 0xf9002000
 * Timer (qcom msm/arch timer block):
 *   0xf9020000 control, 0xf9021000/0xf9022000 frames
 */

#define GICD_BASE 0xf9000000u
#define GICC_BASE 0xf9002000u

#define GICD_CTLR   0x000u
#define GICD_TYPER  0x004u
#define GICD_IIDR   0x008u
#define GICC_IIDR   0x0fcu

static inline uint32_t mmio_r32(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

/* CP15 / system register reads (ARMv7-A). */
static inline uint32_t read_midr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c0, c0, 0" : "=r"(v));
    return v;
}
static inline uint32_t read_mpidr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c0, c0, 5" : "=r"(v));
    return v;
}
static inline uint32_t read_sctlr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
    return v;
}
static inline uint32_t read_cpsr(void)
{
    uint32_t v;
    __asm__ volatile ("mrs %0, cpsr" : "=r"(v));
    return v;
}
static inline uint32_t read_vbar(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c12, c0, 0" : "=r"(v));
    return v;
}
static inline uint32_t read_ttbr0(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
    return v;
}
static inline uint32_t read_id_pfr1(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c0, c1, 1" : "=r"(v));
    return v;
}
/* ARMv7 generic timer: CNTFRQ (read-only frequency) and CNTPCT (counter). */
static inline uint32_t read_cntfrq(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r"(v));
    return v;
}
static inline void read_cntpct(uint32_t *lo, uint32_t *hi)
{
    __asm__ volatile ("mrrc p15, 0, %0, %1, c14" : "=r"(*lo), "=r"(*hi));
}

static void probe_cp15(void)
{
    uint32_t sctlr;

    log_puts("MI4IOS6_STAGE16 probe cp15 begin\n");
    log_kv32("cp15_midr", read_midr());
    log_kv32("cp15_mpidr", read_mpidr());
    log_kv32("cp15_id_pfr1", read_id_pfr1());

    sctlr = read_sctlr();
    log_kv32("cp15_sctlr", sctlr);
    log_kv32("cp15_sctlr_mmu_on", sctlr & 0x1u);
    log_kv32("cp15_sctlr_icache_on", (sctlr >> 12) & 0x1u);
    log_kv32("cp15_sctlr_dcache_on", (sctlr >> 2) & 0x1u);

    log_kv32("cp15_ttbr0", read_ttbr0());
    log_kv32("cp15_vbar", read_vbar());
    log_kv32("cpsr", read_cpsr());
    log_kv32("cpsr_mode", read_cpsr() & 0x1fu);
    log_puts("MI4IOS6_STAGE16 probe cp15 end\n");
}

static void probe_generic_timer(void)
{
    uint32_t freq, a_lo, a_hi, b_lo, b_hi;
    uint32_t pfr1 = read_id_pfr1();
    uint32_t generic_timer_field = (pfr1 >> 16) & 0xfu;

    log_puts("MI4IOS6_STAGE16 probe arch-timer begin\n");
    log_kv32("id_pfr1_generic_timer_field", generic_timer_field);
    if (!generic_timer_field) {
        log_puts("MI4IOS6_STAGE16 arch-timer skipped: ID_PFR1 says absent\n");
        log_puts("MI4IOS6_STAGE16 probe arch-timer end\n");
        return;
    }

    log_puts("MI4IOS6_STAGE16 pre read CNTFRQ\n");
    freq = read_cntfrq();
    log_kv32("cntfrq_hz", freq);

    log_puts("MI4IOS6_STAGE16 pre read CNTPCT sample A\n");
    read_cntpct(&a_lo, &a_hi);
    /* short busy gap, then sample again to show the counter advances */
    for (volatile uint32_t i = 0; i < 200000u; i++) {
        __asm__ volatile ("nop");
    }
    log_puts("MI4IOS6_STAGE16 pre read CNTPCT sample B\n");
    read_cntpct(&b_lo, &b_hi);

    log_kv32("cntpct_a_lo", a_lo);
    log_kv32("cntpct_a_hi", a_hi);
    log_kv32("cntpct_b_lo", b_lo);
    log_kv32("cntpct_b_hi", b_hi);
    log_kv32("cntpct_delta_lo", b_lo - a_lo);
    if (b_lo != a_lo || b_hi != a_hi) {
        log_puts("MI4IOS6_STAGE16 arch-timer counter advancing\n");
    } else {
        log_puts("MI4IOS6_STAGE16 arch-timer counter static\n");
    }
    log_puts("MI4IOS6_STAGE16 probe arch-timer end\n");
}

static void probe_gic(void)
{
    uint32_t typer, itlines, nirqs;

    log_puts("MI4IOS6_STAGE16 probe gic begin\n");

    log_puts("MI4IOS6_STAGE16 pre read GICD_CTLR\n");
    log_kv32("gicd_ctlr", mmio_r32(GICD_BASE + GICD_CTLR));

    log_puts("MI4IOS6_STAGE16 pre read GICD_TYPER\n");
    typer = mmio_r32(GICD_BASE + GICD_TYPER);
    log_kv32("gicd_typer", typer);

    itlines = (typer & 0x1fu);
    nirqs = (itlines + 1u) * 32u;
    log_kv32("gicd_itlines_field", itlines);
    log_kv32("gicd_num_irqs", nirqs);
    log_kv32("gicd_cpu_number_field", (typer >> 5) & 0x7u);

    log_puts("MI4IOS6_STAGE16 pre read GICD_IIDR\n");
    log_kv32("gicd_iidr", mmio_r32(GICD_BASE + GICD_IIDR));

    log_puts("MI4IOS6_STAGE16 pre read GICC_IIDR\n");
    log_kv32("gicc_iidr", mmio_r32(GICC_BASE + GICC_IIDR));

    log_puts("MI4IOS6_STAGE16 probe gic end (read-only; IAR untouched)\n");
}

void run_readonly_hardware_probes(void)
{
    log_puts("MI4IOS6_STAGE16 hardware probes begin (read-only)\n");
    probe_cp15();
    probe_generic_timer();
    probe_gic();
    log_puts("MI4IOS6_STAGE16 hardware probes end\n");
}
