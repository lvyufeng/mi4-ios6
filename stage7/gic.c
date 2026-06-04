#include "stage7.h"

#define GICD_CTLR       0x000u
#define GICD_TYPER      0x004u
#define GICD_IIDR       0x008u
#define GICD_ISENABLER0 0x100u
#define GICD_ISPENDR0   0x200u
#define GICD_IPRIORITY0 0x400u
#define GICD_ITARGETS0  0x800u

#define GICC_CTLR       0x000u
#define GICC_PMR        0x004u
#define GICC_BPR        0x008u
#define GICC_IIDR       0x0fcu

struct gic_state_snapshot GIC_state_stage7;

static inline uint32_t mmio_read32(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

void gic_readonly_snapshot(uint32_t dist_base, uint32_t cpu_base)
{
    memset(&GIC_state_stage7, 0, sizeof(GIC_state_stage7));
    GIC_state_stage7.distBase = dist_base;
    GIC_state_stage7.cpuBase = cpu_base;

    xnu_log_puts("gic snapshot begin (read-only)\n");

    xnu_log_puts("gic pre read GICD_CTLR\n");
    GIC_state_stage7.distCtlr = mmio_read32(dist_base + GICD_CTLR);
    xnu_log_puts("gic pre read GICD_TYPER\n");
    GIC_state_stage7.distTyper = mmio_read32(dist_base + GICD_TYPER);
    xnu_log_puts("gic pre read GICD_IIDR\n");
    GIC_state_stage7.distIidr = mmio_read32(dist_base + GICD_IIDR);

    GIC_state_stage7.irqCount = ((GIC_state_stage7.distTyper & 0x1fu) + 1u) * 32u;
    GIC_state_stage7.cpuInterfaceCount = ((GIC_state_stage7.distTyper >> 5) & 0x7u) + 1u;

    xnu_log_puts("gic pre read GICD_ISENABLER0\n");
    GIC_state_stage7.isenabler0 = mmio_read32(dist_base + GICD_ISENABLER0);
    xnu_log_puts("gic pre read GICD_ISPENDR0\n");
    GIC_state_stage7.ispendr0 = mmio_read32(dist_base + GICD_ISPENDR0);
    xnu_log_puts("gic pre read GICD_IPRIORITY0\n");
    GIC_state_stage7.priority0 = mmio_read32(dist_base + GICD_IPRIORITY0);
    xnu_log_puts("gic pre read GICD_ITARGETS0\n");
    GIC_state_stage7.targets0 = mmio_read32(dist_base + GICD_ITARGETS0);

    xnu_log_puts("gic pre read GICC_CTLR\n");
    GIC_state_stage7.cpuCtlr = mmio_read32(cpu_base + GICC_CTLR);
    xnu_log_puts("gic pre read GICC_PMR\n");
    GIC_state_stage7.cpuPmr = mmio_read32(cpu_base + GICC_PMR);
    xnu_log_puts("gic pre read GICC_BPR\n");
    GIC_state_stage7.cpuBpr = mmio_read32(cpu_base + GICC_BPR);
    xnu_log_puts("gic pre read GICC_IIDR\n");
    GIC_state_stage7.cpuIidr = mmio_read32(cpu_base + GICC_IIDR);

    xnu_log_puts("gic snapshot end (read-only)\n");
}

void gic_log_snapshot(void)
{
    xnu_log_puts("gic state summary begin\n");
    xnu_log_kv32("gic_distBase", GIC_state_stage7.distBase);
    xnu_log_kv32("gic_cpuBase", GIC_state_stage7.cpuBase);
    xnu_log_kv32("gic_distCtlr", GIC_state_stage7.distCtlr);
    xnu_log_kv32("gic_distTyper", GIC_state_stage7.distTyper);
    xnu_log_kv32("gic_distIidr", GIC_state_stage7.distIidr);
    xnu_log_kv32("gic_cpuIidr", GIC_state_stage7.cpuIidr);
    xnu_log_kv32("gic_cpuCtlr", GIC_state_stage7.cpuCtlr);
    xnu_log_kv32("gic_cpuPmr", GIC_state_stage7.cpuPmr);
    xnu_log_kv32("gic_cpuBpr", GIC_state_stage7.cpuBpr);
    xnu_log_kv32("gic_irqCount", GIC_state_stage7.irqCount);
    xnu_log_kv32("gic_cpuInterfaceCount", GIC_state_stage7.cpuInterfaceCount);
    xnu_log_kv32("gic_isenabler0", GIC_state_stage7.isenabler0);
    xnu_log_kv32("gic_ispendr0", GIC_state_stage7.ispendr0);
    xnu_log_kv32("gic_priority0", GIC_state_stage7.priority0);
    xnu_log_kv32("gic_targets0", GIC_state_stage7.targets0);
    xnu_log_puts("gic state summary end\n");
}

int gic_validate_snapshot(void)
{
    int ok = 1;
    ok &= (GIC_state_stage7.distBase == 0xf9000000u);
    ok &= (GIC_state_stage7.cpuBase == 0xf9002000u);
    ok &= (GIC_state_stage7.irqCount >= 288u);
    ok &= (GIC_state_stage7.cpuInterfaceCount == 4u);
    ok &= (GIC_state_stage7.distIidr != 0u);
    ok &= (GIC_state_stage7.cpuIidr != 0u);

    if (ok) {
        xnu_log_puts("gic validate ok\n");
    } else {
        xnu_log_puts("gic validate failed\n");
    }
    return ok;
}
