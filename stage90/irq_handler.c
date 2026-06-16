#include "stage90.h"

/* GIC CPU interface registers */
#define GICC_IAR  0xf900200cu
#define GICC_EOIR 0xf9002010u

/* ARM Generic Timer PPI */
#define ARM_TIMER_PPI 29

static volatile uint32_t g_stage90_irq_count = 0;
static volatile uint32_t g_stage90_irq_last_id = 0x3ff;  /* 0x3ff = spurious */
static volatile uint32_t g_stage90_irq_timer_count = 0;

extern void stage90_timer_irq_clear(void);

void stage90_irq_handler_c(void)
{
	volatile uint32_t *gicc_iar = (volatile uint32_t *)GICC_IAR;
	volatile uint32_t *gicc_eoir = (volatile uint32_t *)GICC_EOIR;
	uint32_t irq_id;

	/* Read interrupt ID from IAR */
	irq_id = *gicc_iar;
	g_stage90_irq_last_id = irq_id;

	/* Increment global IRQ counter */
	g_stage90_irq_count++;

	/* Handle timer interrupt */
	if (irq_id == ARM_TIMER_PPI) {
		g_stage90_irq_timer_count++;
		stage90_timer_irq_clear();
	}

	/* Signal end of interrupt */
	*gicc_eoir = irq_id;
}

uint32_t stage90_get_irq_count(void)
{
	return g_stage90_irq_count;
}

uint32_t stage90_get_irq_last_id(void)
{
	return g_stage90_irq_last_id;
}

uint32_t stage90_get_irq_timer_count(void)
{
	return g_stage90_irq_timer_count;
}
