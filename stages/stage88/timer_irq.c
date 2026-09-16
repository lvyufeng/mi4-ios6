#include "stage88.h"

/* ARM Generic Timer - Virtual Timer */
#define ARM_TIMER_IRQ 27  /* Virtual timer PPI */

static struct stage88_timer_irq_state {
	uint32_t cntfrq;
	uint32_t interval_us;
	uint32_t ticks_requested;
	uint32_t cntv_ctl_before;
	uint32_t cntv_ctl_after;
	uint32_t init_status;
} g_timer_irq_state;

static inline uint32_t read_cntfrq(void)
{
	uint32_t val;
	__asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(val));
	return val;
}

static inline void write_cntv_tval(uint32_t val)
{
	__asm__ volatile("mcr p15, 0, %0, c14, c3, 0" :: "r"(val));
}

static inline uint32_t read_cntv_ctl(void)
{
	uint32_t val;
	__asm__ volatile("mrc p15, 0, %0, c14, c3, 1" : "=r"(val));
	return val;
}

static inline void write_cntv_ctl(uint32_t val)
{
	__asm__ volatile("mcr p15, 0, %0, c14, c3, 1" :: "r"(val));
}

void stage88_timer_irq_init(uint32_t interval_us)
{
	uint32_t cntfrq;
	uint32_t ticks;
	uint32_t ctl;

	/* Read timer frequency */
	cntfrq = read_cntfrq();

	/* Calculate ticks for requested interval */
	uint64_t ticks64 = ((uint64_t)cntfrq * (uint64_t)interval_us) / 1000000ULL;
	ticks = (uint32_t)ticks64;

	/* Read current timer control */
	ctl = read_cntv_ctl();
	g_timer_irq_state.cntv_ctl_before = ctl;

	/* Set timer value (CNTV_TVAL) */
	write_cntv_tval(ticks);

	/* Enable timer: ENABLE=1, IMASK=0 */
	ctl = 1;
	write_cntv_ctl(ctl);

	/* Read back to verify */
	ctl = read_cntv_ctl();
	g_timer_irq_state.cntv_ctl_after = ctl;

	g_timer_irq_state.cntfrq = cntfrq;
	g_timer_irq_state.interval_us = interval_us;
	g_timer_irq_state.ticks_requested = ticks;
	g_timer_irq_state.init_status = STAGE88_STATUS_OK;
}

void stage88_timer_irq_clear(void)
{
	/* Re-arm timer for next interval */
	write_cntv_tval(g_timer_irq_state.ticks_requested);
}

void stage88_timer_irq_disable(void)
{
	uint32_t ctl = 0;  /* ENABLE=0, IMASK=0 */
	write_cntv_ctl(ctl);
}

const struct stage88_timer_irq_state *stage88_timer_irq_state(void)
{
	return &g_timer_irq_state;
}
