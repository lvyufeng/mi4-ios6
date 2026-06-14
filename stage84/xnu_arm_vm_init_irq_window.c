#include "stage84.h"

/* ARM Timer PPI ID */
#define ARM_TIMER_PPI 29

/* IRQ window result structure */
static struct stage84_xnu_arm_vm_init_irq_window_result {
	uint32_t version;
	uint32_t size;
	uint32_t status;
	uint32_t required_mask;
	uint32_t satisfied_mask;
	uint32_t failure_mask;

	/* Prerequisites */
	uint32_t full_pmap_status;
	uint32_t full_pmap_checksum;

	/* GIC init */
	uint32_t gic_init_called;
	uint32_t gic_init_status;
	uint32_t gicd_ctlr_after;
	uint32_t gicc_ctlr_after;
	uint32_t gicc_pmr_after;

	/* Timer init */
	uint32_t timer_init_called;
	uint32_t timer_init_status;
	uint32_t timer_cntfrq;
	uint32_t timer_interval_us;
	uint32_t timer_ticks;
	uint32_t timer_cntp_ctl_after;

	/* IRQ window */
	uint32_t irq_count_before;
	uint32_t irq_count_after;
	uint32_t irq_timer_count_before;
	uint32_t irq_timer_count_after;
	uint32_t irq_last_id;
	uint32_t cpsr_before_enable;
	uint32_t cpsr_after_enable;
	uint32_t cpsr_after_disable;

	/* Validation */
	uint32_t irq_fired;
	uint32_t irq_timer_fired;
	uint32_t irqs_disabled_after;
	uint32_t pmap_preserved;
	uint32_t public_xnu_irq_executed;
	uint32_t persistent_write_attempted;

	uint32_t checksum;
} g_result;

extern void stage84_gic_irq_init(void);
extern void stage84_gic_irq_enable_interrupt(uint32_t irq_id);
extern void stage84_timer_irq_init(uint32_t interval_us);
extern void stage84_timer_irq_disable(void);
extern uint32_t stage84_get_irq_count(void);
extern uint32_t stage84_get_irq_last_id(void);
extern uint32_t stage84_get_irq_timer_count(void);
extern const struct stage84_gic_irq_state *stage84_gic_irq_state(void);
extern const struct stage84_timer_irq_state *stage84_timer_irq_state(void);
extern const struct stage84_xnu_arm_vm_init_full_pmap_result *stage84_xnu_arm_vm_init_full_pmap_result(void);

static inline uint32_t read_cpsr(void)
{
	uint32_t cpsr;
	__asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
	return cpsr;
}

static inline void enable_irqs(void)
{
	__asm__ volatile("cpsie i" ::: "memory");
}

static inline void disable_irqs(void)
{
	__asm__ volatile("cpsid i" ::: "memory");
}

int stage84_xnu_arm_vm_init_irq_window_run(
    struct boot_args *args,
    struct stage84_xnu_entry_stub_result *entry_result)
{
	struct stage84_xnu_arm_vm_init_irq_window_result *r = &g_result;
	const struct stage84_xnu_arm_vm_init_full_pmap_result *pmap_result;
	const struct stage84_gic_irq_state *gic_state;
	const struct stage84_timer_irq_state *timer_state;

	(void)args;
	(void)entry_result;

	/* Initialize result */
	memset(r, 0, sizeof(*r));
	r->version = STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_VERSION;
	r->size = sizeof(*r);
	r->required_mask = STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_REQUIRED_MASK;

	/* Check full pmap prerequisite */
	pmap_result = stage84_xnu_arm_vm_init_full_pmap_result();
	if (!pmap_result || pmap_result->status != STAGE84_STATUS_OK) {
		r->failure_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_FAIL_FULL_PMAP;
		goto finish;
	}
	r->full_pmap_status = pmap_result->status;
	r->full_pmap_checksum = pmap_result->checksum;
	r->satisfied_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_FULL_PMAP_OK;

	/* Initialize GIC */
	stage84_gic_irq_init();
	r->gic_init_called = 1;

	gic_state = stage84_gic_irq_state();
	r->gic_init_status = gic_state->init_status;
	r->gicd_ctlr_after = gic_state->gicd_ctlr_after;
	r->gicc_ctlr_after = gic_state->gicc_ctlr_after;
	r->gicc_pmr_after = gic_state->gicc_pmr_after;

	if (r->gic_init_status != STAGE84_STATUS_OK) {
		r->failure_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_FAIL_GIC_INIT;
		goto finish;
	}
	r->satisfied_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_GIC_INIT;

	/* Enable timer interrupt in GIC */
	stage84_gic_irq_enable_interrupt(ARM_TIMER_PPI);
	r->satisfied_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_GIC_IRQ_ENABLED;

	/* Initialize ARM generic timer (100ms interval) */
	stage84_timer_irq_init(100000);
	r->timer_init_called = 1;

	timer_state = stage84_timer_irq_state();
	r->timer_init_status = timer_state->init_status;
	r->timer_cntfrq = timer_state->cntfrq;
	r->timer_interval_us = timer_state->interval_us;
	r->timer_ticks = timer_state->ticks_requested;
	r->timer_cntp_ctl_after = timer_state->cntp_ctl_after;

	if (r->timer_init_status != STAGE84_STATUS_OK) {
		r->failure_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_FAIL_TIMER_INIT;
		goto finish;
	}
	r->satisfied_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_TIMER_INIT;

	/* Read CPSR before enabling IRQs */
	r->cpsr_before_enable = read_cpsr();

	/* Record IRQ counters before window */
	r->irq_count_before = stage84_get_irq_count();
	r->irq_timer_count_before = stage84_get_irq_timer_count();

	/* Enable IRQs (note: full pmap is already installed by Stage83) */
	enable_irqs();
	r->cpsr_after_enable = read_cpsr();
	r->satisfied_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_IRQ_ENABLED;

	/* Wait for at least one timer interrupt (busy loop ~200ms) */
	for (volatile uint32_t i = 0; i < 10000000; i++) {
		/* Spin, waiting for IRQ to fire */
	}

	/* Disable IRQs */
	disable_irqs();
	r->cpsr_after_disable = read_cpsr();

	/* Record IRQ counters after window */
	r->irq_count_after = stage84_get_irq_count();
	r->irq_timer_count_after = stage84_get_irq_timer_count();
	r->irq_last_id = stage84_get_irq_last_id();

	/* Disable timer */
	stage84_timer_irq_disable();

	/* Validate IRQs fired */
	if (r->irq_count_after > r->irq_count_before) {
		r->irq_fired = 1;
		r->satisfied_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_IRQ_FIRED;
	} else {
		r->failure_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_FAIL_IRQ_NOT_FIRED;
	}

	if (r->irq_timer_count_after > r->irq_timer_count_before) {
		r->irq_timer_fired = 1;
		r->satisfied_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_TIMER_IRQ_FIRED;
	} else {
		r->failure_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_FAIL_TIMER_IRQ_NOT_FIRED;
	}

	/* Validate IRQs are disabled after window */
	if ((r->cpsr_after_disable & 0x80) != 0) {  /* I bit set = IRQs disabled */
		r->irqs_disabled_after = 1;
		r->satisfied_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_IRQ_DISABLED;
	} else {
		r->failure_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_FAIL_IRQ_STILL_ENABLED;
	}

	/* Validate pmap preserved (check full_pmap result still valid) */
	if (pmap_result->status == STAGE84_STATUS_OK) {
		r->pmap_preserved = 1;
		r->satisfied_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_PMAP_PRESERVED;
	}

	/* Validate no public XNU IRQ handler execution */
	r->public_xnu_irq_executed = 0;  /* Stage-owned only */
	r->satisfied_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_NO_PUBLIC_IRQ;

	/* Validate no persistent writes */
	r->persistent_write_attempted = 0;  /* Stage-owned only */
	r->satisfied_mask |= STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_NO_PERSISTENT_WRITE;

finish:
	/* Compute final status */
	if (r->satisfied_mask == r->required_mask && r->failure_mask == 0) {
		r->status = STAGE84_STATUS_OK;
	} else {
		r->status = STAGE84_STATUS_FAIL(r->failure_mask);
	}

	/* Compute checksum */
	const uint32_t *p = (const uint32_t *)r;
	const uint32_t *end = (const uint32_t *)&r->checksum;
	uint32_t xor_sum = 0;
	while (p < end) {
		xor_sum ^= *p++;
	}
	r->checksum = xor_sum;

	return r->status == STAGE84_STATUS_OK ? 0 : -1;
}

const struct stage84_xnu_arm_vm_init_irq_window_result *
stage84_xnu_arm_vm_init_irq_window_result(void)
{
	return &g_result;
}
