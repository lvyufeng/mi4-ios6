#include "stage90.h"

/* GIC register addresses for MSM8974 */
#define GIC_DIST_BASE   0xf9001000u
#define GIC_CPU_BASE    0xf9002000u

/* Distributor registers */
#define GICD_CTLR       (GIC_DIST_BASE + 0x000)
#define GICD_ISENABLER(n)  (GIC_DIST_BASE + 0x100 + 4 * (n))
#define GICD_IPRIORITYR(n) (GIC_DIST_BASE + 0x400 + (n))
#define GICD_ITARGETSR(n)  (GIC_DIST_BASE + 0x800 + (n))

/* CPU interface registers */
#define GICC_CTLR       (GIC_CPU_BASE + 0x000)
#define GICC_PMR        (GIC_CPU_BASE + 0x004)
#define GICC_IAR        (GIC_CPU_BASE + 0x00c)
#define GICC_EOIR       (GIC_CPU_BASE + 0x010)

static struct stage90_gic_irq_state {
	uint32_t gicd_ctlr_before;
	uint32_t gicc_ctlr_before;
	uint32_t gicc_pmr_before;
	uint32_t gicd_ctlr_after;
	uint32_t gicc_ctlr_after;
	uint32_t gicc_pmr_after;
	uint32_t init_status;
} g_gic_irq_state;

void stage90_gic_irq_init(void)
{
	volatile uint32_t *gicd_ctlr = (volatile uint32_t *)GICD_CTLR;
	volatile uint32_t *gicc_ctlr = (volatile uint32_t *)GICC_CTLR;
	volatile uint32_t *gicc_pmr = (volatile uint32_t *)GICC_PMR;

	/* Read current state */
	g_gic_irq_state.gicd_ctlr_before = *gicd_ctlr;
	g_gic_irq_state.gicc_ctlr_before = *gicc_ctlr;
	g_gic_irq_state.gicc_pmr_before = *gicc_pmr;

	/* Enable GIC distributor */
	*gicd_ctlr = 1;

	/* Enable GIC CPU interface */
	*gicc_ctlr = 1;

	/* Set priority mask to allow all interrupts */
	*gicc_pmr = 0xff;

	/* Read back to verify */
	g_gic_irq_state.gicd_ctlr_after = *gicd_ctlr;
	g_gic_irq_state.gicc_ctlr_after = *gicc_ctlr;
	g_gic_irq_state.gicc_pmr_after = *gicc_pmr;

	g_gic_irq_state.init_status = STAGE90_STATUS_OK;
}

void stage90_gic_irq_enable_interrupt(uint32_t irq_id)
{
	volatile uint32_t *gicd_isenabler = (volatile uint32_t *)GICD_ISENABLER(irq_id / 32);
	volatile uint8_t *gicd_ipriorityr = (volatile uint8_t *)GICD_IPRIORITYR(irq_id);
	volatile uint8_t *gicd_itargetsr = (volatile uint8_t *)GICD_ITARGETSR(irq_id);

	/* Set priority to medium (0x80) */
	*gicd_ipriorityr = 0x80;

	/* Target CPU0 */
	*gicd_itargetsr = 0x01;

	/* Enable interrupt */
	*gicd_isenabler = (1u << (irq_id % 32));
}

const struct stage90_gic_irq_state *stage90_gic_irq_state(void)
{
	return &g_gic_irq_state;
}
