#include "stage17.h"

#define GICD_CTLR       0x000u
#define GICD_TYPER      0x004u
#define GICD_IIDR       0x008u
#define GICD_ISENABLER0 0x100u
#define GICD_ICENABLER0 0x180u
#define GICD_ISPENDR0   0x200u
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

struct gic_state_snapshot GIC_state_stage17;
volatile uint32_t stage17_irq_count;
volatile uint32_t stage17_last_iar;
volatile uint32_t stage17_last_irq_id;
volatile uint32_t stage17_sgi0_count;
volatile uint32_t stage17_spurious_irq_count;
volatile uint32_t stage17_timer_irq_count;
volatile uint32_t stage17_last_timer_irq_id;
volatile uint32_t stage17_last_timer_ctl;
volatile uint32_t stage17_other_irq_count;

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
    memset(&GIC_state_stage17, 0, sizeof(GIC_state_stage17));
    GIC_state_stage17.distBase = dist_base;
    GIC_state_stage17.cpuBase = cpu_base;

    xnu_log_puts("gic snapshot begin (read-only)\n");

    xnu_log_puts("gic pre read GICD_CTLR\n");
    GIC_state_stage17.distCtlr = mmio_read32(dist_base + GICD_CTLR);
    xnu_log_puts("gic pre read GICD_TYPER\n");
    GIC_state_stage17.distTyper = mmio_read32(dist_base + GICD_TYPER);
    xnu_log_puts("gic pre read GICD_IIDR\n");
    GIC_state_stage17.distIidr = mmio_read32(dist_base + GICD_IIDR);

    GIC_state_stage17.irqCount = ((GIC_state_stage17.distTyper & 0x1fu) + 1u) * 32u;
    GIC_state_stage17.cpuInterfaceCount = ((GIC_state_stage17.distTyper >> 5) & 0x7u) + 1u;

    xnu_log_puts("gic pre read GICD_ISENABLER0\n");
    GIC_state_stage17.isenabler0 = mmio_read32(dist_base + GICD_ISENABLER0);
    xnu_log_puts("gic pre read GICD_ISPENDR0\n");
    GIC_state_stage17.ispendr0 = mmio_read32(dist_base + GICD_ISPENDR0);
    xnu_log_puts("gic pre read GICD_IPRIORITY0\n");
    GIC_state_stage17.priority0 = mmio_read32(dist_base + GICD_IPRIORITY0);
    xnu_log_puts("gic pre read GICD_ITARGETS0\n");
    GIC_state_stage17.targets0 = mmio_read32(dist_base + GICD_ITARGETS0);

    xnu_log_puts("gic pre read GICC_CTLR\n");
    GIC_state_stage17.cpuCtlr = mmio_read32(cpu_base + GICC_CTLR);
    xnu_log_puts("gic pre read GICC_PMR\n");
    GIC_state_stage17.cpuPmr = mmio_read32(cpu_base + GICC_PMR);
    xnu_log_puts("gic pre read GICC_BPR\n");
    GIC_state_stage17.cpuBpr = mmio_read32(cpu_base + GICC_BPR);
    xnu_log_puts("gic pre read GICC_IIDR\n");
    GIC_state_stage17.cpuIidr = mmio_read32(cpu_base + GICC_IIDR);

    xnu_log_puts("gic snapshot end (read-only)\n");
}

void gic_log_snapshot(void)
{
    xnu_log_puts("gic state summary begin\n");
    xnu_log_kv32("gic_distBase", GIC_state_stage17.distBase);
    xnu_log_kv32("gic_cpuBase", GIC_state_stage17.cpuBase);
    xnu_log_kv32("gic_distCtlr", GIC_state_stage17.distCtlr);
    xnu_log_kv32("gic_distTyper", GIC_state_stage17.distTyper);
    xnu_log_kv32("gic_distIidr", GIC_state_stage17.distIidr);
    xnu_log_kv32("gic_cpuIidr", GIC_state_stage17.cpuIidr);
    xnu_log_kv32("gic_cpuCtlr", GIC_state_stage17.cpuCtlr);
    xnu_log_kv32("gic_cpuPmr", GIC_state_stage17.cpuPmr);
    xnu_log_kv32("gic_cpuBpr", GIC_state_stage17.cpuBpr);
    xnu_log_kv32("gic_irqCount", GIC_state_stage17.irqCount);
    xnu_log_kv32("gic_cpuInterfaceCount", GIC_state_stage17.cpuInterfaceCount);
    xnu_log_kv32("gic_isenabler0", GIC_state_stage17.isenabler0);
    xnu_log_kv32("gic_ispendr0", GIC_state_stage17.ispendr0);
    xnu_log_kv32("gic_priority0", GIC_state_stage17.priority0);
    xnu_log_kv32("gic_targets0", GIC_state_stage17.targets0);
    xnu_log_puts("gic state summary end\n");
}

int gic_validate_snapshot(void)
{
    int ok = 1;
    ok &= (GIC_state_stage17.distBase == 0xf9000000u);
    ok &= (GIC_state_stage17.cpuBase == 0xf9002000u);
    ok &= (GIC_state_stage17.irqCount >= 288u);
    ok &= (GIC_state_stage17.cpuInterfaceCount == 4u);
    ok &= (GIC_state_stage17.distIidr != 0u);
    ok &= (GIC_state_stage17.cpuIidr != 0u);

    if (ok) {
        xnu_log_puts("gic validate ok\n");
    } else {
        xnu_log_puts("gic validate failed\n");
    }
    return ok;
}

void stage17_irq_c_handler(void)
{
    const uint32_t cpu_base = GIC_state_stage17.cpuBase;
    const uint32_t iar = mmio_read32(cpu_base + GICC_IAR);
    const uint32_t intid = iar & GICC_IAR_INTID_MASK;
    const uint32_t count = stage17_irq_count + 1u;

    stage17_irq_count = count;
    stage17_last_iar = iar;
    stage17_last_irq_id = intid;

    if (intid == GIC_SGI0_ID) {
        stage17_sgi0_count++;
    } else if (intid == GIC_TIMER_PPI0_ID || intid == GIC_TIMER_PPI1_ID) {
        uint32_t ctl = read_cntp_ctl();
        stage17_timer_irq_count++;
        stage17_last_timer_irq_id = intid;
        stage17_last_timer_ctl = ctl;
        generic_timer_shutdown();
    } else if (intid == GICC_SPURIOUS_ID) {
        stage17_spurious_irq_count++;
    } else {
        stage17_other_irq_count++;
    }

    if (count <= 8u) {
        log_puts("MI4IOS6_STAGE17 irq handler iar=");
        log_hex32(iar);
        log_puts(" id=");
        log_hex32(intid);
        log_puts(" count=");
        log_hex32(count);
        log_puts(" timer_count=");
        log_hex32(stage17_timer_irq_count);
        log_puts("\n");
    }

    if (intid != GICC_SPURIOUS_ID) {
        mmio_write32(cpu_base + GICC_EOIR, iar);
        barrier_dsb_isb();
    }
}

static void reset_irq_counters(void)
{
    stage17_irq_count = 0;
    stage17_last_iar = 0xffffffffu;
    stage17_last_irq_id = 0xffffffffu;
    stage17_sgi0_count = 0;
    stage17_spurious_irq_count = 0;
    stage17_timer_irq_count = 0;
    stage17_last_timer_irq_id = 0xffffffffu;
    stage17_last_timer_ctl = 0xffffffffu;
    stage17_other_irq_count = 0;
}

int gic_sgi_selftest(void)
{
    const uint32_t dist_base = GIC_state_stage17.distBase;
    const uint32_t cpu_base = GIC_state_stage17.cpuBase;
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
    while (stage17_sgi0_count == 0u && timebase_elapsed_us(start, timebase_ticks()) < 20000u) {
        __asm__ volatile ("nop" ::: "memory");
    }
    disable_irq_delivery();
    barrier_dsb_isb();

    xnu_log_kv32("gic_sgi_cpsr_after", read_cpsr());
    xnu_log_kv32("gic_sgi_irq_count", stage17_irq_count);
    xnu_log_kv32("gic_sgi_sgi0_count", stage17_sgi0_count);
    xnu_log_kv32("gic_sgi_spurious_count", stage17_spurious_irq_count);
    xnu_log_kv32("gic_sgi_last_iar", stage17_last_iar);
    xnu_log_kv32("gic_sgi_last_irq_id", stage17_last_irq_id);

    if (stage17_sgi0_count > 0u) {
        xnu_log_puts("gic SGI selftest ok\n");
        return 1;
    }

    xnu_log_puts("gic SGI selftest failed: no SGI0 IRQ observed\n");
    return 0;
}

int gic_timer_selftest(void)
{
    const uint32_t dist_base = GIC_state_stage17.distBase;
    const uint32_t cpu_base = GIC_state_stage17.cpuBase;
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
    while (stage17_timer_irq_count == 0u && timebase_elapsed_us(start, timebase_ticks()) < 50000u) {
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
    xnu_log_kv32("gic_timer_irq_count", stage17_irq_count);
    xnu_log_kv32("gic_timer_timer_count", stage17_timer_irq_count);
    xnu_log_kv32("gic_timer_other_count", stage17_other_irq_count);
    xnu_log_kv32("gic_timer_spurious_count", stage17_spurious_irq_count);
    xnu_log_kv32("gic_timer_last_iar", stage17_last_iar);
    xnu_log_kv32("gic_timer_last_irq_id", stage17_last_irq_id);
    xnu_log_kv32("gic_timer_last_timer_id", stage17_last_timer_irq_id);
    xnu_log_kv32("gic_timer_last_timer_ctl", stage17_last_timer_ctl);
    xnu_log_kv32("gic_timer_cntp_ctl_after", read_cntp_ctl());
    xnu_log_kv32("gic_timer_isenabler0_restored", mmio_read32(dist_base + GICD_ISENABLER0));

    if (stage17_timer_irq_count > 0u &&
        (stage17_last_timer_irq_id == GIC_TIMER_PPI0_ID || stage17_last_timer_irq_id == GIC_TIMER_PPI1_ID)) {
        xnu_log_puts("gic timer selftest ok\n");
        return 1;
    }

    xnu_log_puts("gic timer selftest failed: no timer IRQ observed\n");
    return 0;
}
