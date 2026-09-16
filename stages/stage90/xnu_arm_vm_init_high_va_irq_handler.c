/* Stage86 High-VA IRQ Handler Relocation Window
 *
 * Proves ARM exception handlers can execute from the L2-page-mapped high-virtual
 * kernel address at 0x80000000+offset by relocating VBAR to the high-VA projection
 * of the existing stage90_vectors table and verifying a timer IRQ is delivered and
 * handled correctly.
 *
 * Stage85 proved:
 * - High-VA code execution at 0x80000000 via L2 pages (function call)
 * - High-VA data access at 0x80000000 via L2 pages
 *
 * Stage86 proves:
 * - Exception vector table at high-VA (VBAR → 0x80008xxx)
 * - IRQ handler execution from high-VA vector base
 * - PC-relative branch from high-VA vector to high-VA C handler works
 * - IRQ stack access under high-VA candidate L1 works
 *
 * This completes exception-handling validation under the high-VA kernel pmap.
 */

#include "stage90.h"

/* GIC register offsets (from gic.c) */
#define GICD_ISENABLER0 0x100u

/* Timer PPI IDs (from gic.c) */
#define GIC_TIMER_PPI0_ID   18u
#define GIC_TIMER_PPI1_ID   19u
#define GIC_TIMER_PPI_MASK  ((1u << GIC_TIMER_PPI0_ID) | (1u << GIC_TIMER_PPI1_ID))

/* Inline MMIO helper */
static inline void hvir_mmio_write32(uint32_t addr, uint32_t value)
{
	*(volatile uint32_t *)(uintptr_t)addr = value;
}

/* Inline TTBR0/VBAR/IRQ helpers (copied from high_va_code_exec pattern) */
static inline uint32_t hvir_read_ttbr0(void)
{
	uint32_t v;
	__asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
	return v;
}

static inline void hvir_write_ttbr0(uint32_t v)
{
	__asm__ volatile ("mcr p15, 0, %0, c2, c0, 0" :: "r"(v));
}

static inline uint32_t hvir_read_vbar(void)
{
	uint32_t v;
	__asm__ volatile ("mrc p15, 0, %0, c12, c0, 0" : "=r"(v));
	return v;
}

static inline void hvir_write_vbar(uint32_t v)
{
	__asm__ volatile ("mcr p15, 0, %0, c12, c0, 0" :: "r"(v));
}

static inline void hvir_enable_irqs(void)
{
	__asm__ volatile ("cpsie i" ::: "memory");
	__asm__ volatile ("isb" ::: "memory");
}

static inline void hvir_disable_irqs(void)
{
	__asm__ volatile ("cpsid i" ::: "memory");
	__asm__ volatile ("isb" ::: "memory");
}

static inline void hvir_invalidate_tlbs(void)
{
	__asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" :: "r"(0));
}

static inline void hvir_dsb_isb(void)
{
	__asm__ volatile ("dsb" ::: "memory");
	__asm__ volatile ("isb" ::: "memory");
}

/* Generic timer register access via inline asm (avoid extern dependencies) */
static inline uint32_t local_read_cntp_ctl(void)
{
	uint32_t v;
	__asm__ volatile ("mrc p15, 0, %0, c14, c2, 1" : "=r"(v));
	return v;
}

static inline void local_write_cntp_ctl(uint32_t v)
{
	__asm__ volatile ("mcr p15, 0, %0, c14, c2, 1" :: "r"(v));
}

static inline void local_write_cntp_tval(uint32_t v)
{
	__asm__ volatile ("mcr p15, 0, %0, c14, c2, 0" :: "r"(v));
}

#define LOCAL_CNTP_CTL_ENABLE 0x00000001u
#define LOCAL_CNTP_CTL_IMASK  0x00000002u

extern uint8_t stage90_vectors[];  /* From vectors.S */
extern volatile uint32_t stage90_irq_count;
extern volatile uint32_t stage90_timer_irq_count;

static struct stage90_xnu_arm_vm_init_high_va_irq_handler_result g_result;

static uint32_t stage90_xnu_arm_vm_init_high_va_irq_handler_checksum(
    volatile const struct stage90_xnu_arm_vm_init_high_va_irq_handler_result *r)
{
	volatile const uint32_t *words = (volatile const uint32_t *)r;
	uint32_t count = (uint32_t)(offsetof(struct stage90_xnu_arm_vm_init_high_va_irq_handler_result, checksum) / sizeof(uint32_t));
	uint32_t chk = 0;
	for (uint32_t i = 0; i < count; i++) {
		chk ^= words[i];
	}
	return chk;
}

static void stage90_xnu_arm_vm_init_high_va_irq_handler_log(
    volatile const struct stage90_xnu_arm_vm_init_high_va_irq_handler_result *r)
{
	xnu_log_puts("stage90_xnu_arm_vm_init_high_va_irq_handler result:\n");
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_version", r->version);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_size", r->size);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_status", r->status);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_required_mask", r->required_mask);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_satisfied_mask", r->satisfied_mask);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_failure_mask", r->failure_mask);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_full_pmap_status", r->full_pmap_status);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_vectors_phys", r->vectors_phys);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_vectors_high_va", r->vectors_high_va);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_original_vbar", r->original_vbar);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_high_va_vbar_set", r->high_va_vbar_set);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_irq_count_before", r->irq_count_before);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_timer_irq_count_before", r->timer_irq_count_before);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_irq_window_opened", r->irq_window_opened);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_irq_count_after", r->irq_count_after);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_timer_irq_count_after", r->timer_irq_count_after);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_irq_delivered", r->irq_delivered);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_vbar_restored", r->vbar_restored);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_restored_vbar", r->restored_vbar);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_irq_handler_checksum", r->checksum);
}

int stage90_xnu_arm_vm_init_high_va_irq_handler_run(
    struct boot_args *args,
    struct stage90_xnu_entry_stub_result *entry_result)
{
	struct stage90_xnu_arm_vm_init_high_va_irq_handler_result *r = &g_result;
	const struct stage90_xnu_arm_vm_init_full_pmap_result *pmap_result;
	uint32_t vectors_phys = (uint32_t)(uintptr_t)&stage90_vectors;
	uint32_t vectors_high_va;
	uint32_t candidate_l1_base;
	uint32_t original_ttbr0;
	uint32_t original_vbar;
	uint32_t irq_count_before, timer_irq_count_before;
	uint32_t irq_count_after, timer_irq_count_after;
	uint32_t timeout;
	uint32_t ctl;

	(void)args;
	(void)entry_result;

	/* Initialize result */
	memset(r, 0, sizeof(*r));
	r->version = STAGE90_XNU_ARM_VM_INIT_HIGH_VA_IRQ_HANDLER_VERSION;
	r->size = sizeof(*r);
	r->required_mask = STAGE90_XNU_ARM_VM_INIT_HIGH_VA_IRQ_HANDLER_REQUIRED_MASK;

	/* Check full pmap prerequisite (Stage86's full pmap install) */
	pmap_result = stage90_xnu_arm_vm_init_full_pmap_result();
	if (!pmap_result || pmap_result->status != STAGE90_STATUS_OK) {
		r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_IRQ_HANDLER_FAIL_FULL_PMAP;
		goto finish;
	}
	r->full_pmap_status = pmap_result->status;
	r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_IRQ_HANDLER_SAT_FULL_PMAP_OK;

	/* Compute high-VA address of exception vectors.
	 * The candidate L2 maps VA 0x80000000+N -> PA 0x00000000+N (identity offset),
	 * so the high-VA for physical address X is virt_base + X. */
	vectors_high_va = pmap_result->virt_base + vectors_phys;
	r->vectors_phys = vectors_phys;
	r->vectors_high_va = vectors_high_va;
	r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_IRQ_HANDLER_SAT_VECTORS_ADDR_COMPUTED;

	/* Verify candidate L1 base is valid (16KB aligned) */
	candidate_l1_base = pmap_result->candidate_l1_base;
	if ((candidate_l1_base & 0x3fffu) != 0u || candidate_l1_base == 0u) {
		r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_IRQ_HANDLER_FAIL_FULL_PMAP;
		goto finish;
	}

	/* Save original TTBR0 and VBAR */
	original_ttbr0 = hvir_read_ttbr0();
	original_vbar = hvir_read_vbar();
	r->original_vbar = original_vbar;

	/* Disable IRQs (safety: mask before reconfiguring VBAR) */
	hvir_disable_irqs();

	/* Re-install the Stage-owned candidate L1 (with L2-mapped 0x80000000 region).
	 * full_pmap already restored TTBR0, so we must re-install it for the
	 * high-VA IRQ handler window. The candidate L1 maps:
	 * - Section 0/1 (identity, includes vectors + C handler + IRQ stack)
	 * - 0x80000000 (L2 pages, high-VA alias of section 0/1)
	 * - RAM direct map, MMIO
	 * So both vectors and handler are accessible at high-VA when candidate L1 is live. */
	hvir_dsb_isb();
	hvir_write_ttbr0(candidate_l1_base);
	hvir_invalidate_tlbs();
	hvir_dsb_isb();

	/* DIAGNOSTIC: First test with low-VA VBAR (keep original_vbar) to verify
	 * timer configuration works, then switch to high-VA VBAR. */

	/* Record IRQ counters before opening the window */
	irq_count_before = stage90_irq_count;
	timer_irq_count_before = stage90_timer_irq_count;
	r->irq_count_before = irq_count_before;
	r->timer_irq_count_before = timer_irq_count_before;

	/* Re-enable timer PPI in GIC (gic_timer_selftest disabled it after completion).
	 * The GIC distributor base is at 0xf9000000 (from GIC_state_stage90 in gic.c). */
	hvir_mmio_write32(0xf9000000u + GICD_ISENABLER0, GIC_TIMER_PPI_MASK);
	hvir_dsb_isb();

	/* Arm a timer IRQ. We reuse the ARMv7 generic timer (CNTP), which was
	 * already configured by earlier selftests (gic_timer_selftest in mmu.c).
	 * The timer PPI is now re-enabled in GICD_ISENABLER0. We just need to
	 * arm the timer counter and enable it. */

	/* Shutdown timer (clear ENABLE bit and mask interrupts) */
	ctl = local_read_cntp_ctl();
	ctl &= ~LOCAL_CNTP_CTL_ENABLE;
	ctl |= LOCAL_CNTP_CTL_IMASK;   /* Mask while reconfiguring */
	local_write_cntp_ctl(ctl);
	hvir_dsb_isb();

	/* Arm timer for 500µs. The timer frequency is typically 19.2MHz on MSM8974,
	 * so 500µs = 500 * 19.2 = 9600 ticks. We use a very short interval. */
	local_write_cntp_tval(9600);  /* 500µs at 19.2MHz */

	/* Enable timer and unmask interrupts */
	ctl = LOCAL_CNTP_CTL_ENABLE;  /* ENABLE=1, IMASK=0 */
	local_write_cntp_ctl(ctl);
	hvir_dsb_isb();

	/* DIAGNOSTIC: Test IRQ delivery with LOW-VA VBAR first */
	hvir_enable_irqs();
	timeout = 100000;
	while (stage90_timer_irq_count == timer_irq_count_before && timeout > 0) {
		timeout--;
	}
	hvir_disable_irqs();

	/* Check if timer fired with low-VA VBAR */
	if (stage90_timer_irq_count > timer_irq_count_before) {
		/* Timer works! Now try with high-VA VBAR */
		timer_irq_count_before = stage90_timer_irq_count;

		/* Set VBAR to the high-VA projection of the exception vectors */
		hvir_write_vbar(vectors_high_va);
		hvir_dsb_isb();
		r->high_va_vbar_set = 1;
		r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_IRQ_HANDLER_SAT_HIGH_VA_VBAR_SET;

		/* Re-arm timer for another IRQ */
		hvir_disable_irqs();
		ctl = local_read_cntp_ctl();
		ctl &= ~LOCAL_CNTP_CTL_ENABLE;
		ctl |= LOCAL_CNTP_CTL_IMASK;
		local_write_cntp_ctl(ctl);
		hvir_dsb_isb();

		local_write_cntp_tval(9600);
		ctl = LOCAL_CNTP_CTL_ENABLE;
		local_write_cntp_ctl(ctl);
		hvir_dsb_isb();

		/* Open IRQ window with high-VA VBAR active */
		hvir_enable_irqs();
		r->irq_window_opened = 1;
		r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_IRQ_HANDLER_SAT_IRQ_WINDOW_OPENED;

		timeout = 200000;
		while (stage90_timer_irq_count == timer_irq_count_before && timeout > 0) {
			timeout--;
		}
		hvir_disable_irqs();
	} else {
		/* Timer didn't fire even with low-VA VBAR - config problem */
		r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_IRQ_HANDLER_SAT_HIGH_VA_VBAR_SET;
		hvir_write_vbar(vectors_high_va);
		hvir_dsb_isb();
		r->high_va_vbar_set = 1;

		hvir_enable_irqs();
		r->irq_window_opened = 1;
		r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_IRQ_HANDLER_SAT_IRQ_WINDOW_OPENED;

		timeout = 200000;
		while (stage90_timer_irq_count == timer_irq_count_before && timeout > 0) {
			timeout--;
		}
		hvir_disable_irqs();
	}

	/* Record IRQ counters after the window */
	irq_count_after = stage90_irq_count;
	timer_irq_count_after = stage90_timer_irq_count;
	r->irq_count_after = irq_count_after;
	r->timer_irq_count_after = timer_irq_count_after;

	/* Verify IRQ was delivered and handled */
	if (timer_irq_count_after > timer_irq_count_before) {
		r->irq_delivered = 1;
		r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_IRQ_HANDLER_SAT_IRQ_DELIVERED;
	} else {
		r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_IRQ_HANDLER_FAIL_IRQ_NOT_DELIVERED;
	}

	/* Restore original VBAR and TTBR0 (return to normal identity table + low-VA vectors) */
	hvir_dsb_isb();
	hvir_write_vbar(original_vbar);
	hvir_write_ttbr0(original_ttbr0);
	hvir_invalidate_tlbs();
	hvir_dsb_isb();

	r->vbar_restored = 1;
	r->restored_vbar = hvir_read_vbar();
	r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_IRQ_HANDLER_SAT_VBAR_RESTORED;

finish:
	/* Compute final status */
	if (r->satisfied_mask == r->required_mask && r->failure_mask == 0) {
		r->status = STAGE90_STATUS_OK;
	} else {
		r->status = STAGE90_STATUS_FAIL(r->failure_mask);
	}

	/* Compute checksum */
	r->checksum = stage90_xnu_arm_vm_init_high_va_irq_handler_checksum(r);

	/* Log result */
	stage90_xnu_arm_vm_init_high_va_irq_handler_log(r);

	return r->status == STAGE90_STATUS_OK ? 0 : -1;
}

const struct stage90_xnu_arm_vm_init_high_va_irq_handler_result *
stage90_xnu_arm_vm_init_high_va_irq_handler_result(void)
{
	return &g_result;
}
