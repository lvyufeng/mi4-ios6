/* Stage87 High-VA Data Abort Handler Window
 *
 * Proves ARM synchronous exception handlers (data aborts) can execute from the
 * L2-page-mapped high-virtual kernel address at 0x80000000+offset by setting
 * VBAR to the high-VA projection of exception vectors and intentionally triggering
 * a data abort (unmapped memory access).
 *
 * Stage86 proved:
 * - Asynchronous exceptions (IRQ) at high-VA
 * - PC-relative branch from high-VA vector to high-VA C handler
 *
 * Stage87 proves:
 * - Synchronous exceptions (data abort) at high-VA
 * - Fault register access (DFSR/DFAR) in high-VA exception context
 * - Exception return with PC adjustment (skip faulting instruction)
 *
 * This completes synchronous exception-handling validation under the high-VA kernel pmap.
 */

#include "stage87.h"

/* Inline TTBR0/VBAR/IRQ helpers (copied from high_va_irq_handler pattern) */
static inline uint32_t hvda_read_ttbr0(void)
{
	uint32_t v;
	__asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
	return v;
}

static inline void hvda_write_ttbr0(uint32_t v)
{
	__asm__ volatile ("mcr p15, 0, %0, c2, c0, 0" :: "r"(v));
}

static inline uint32_t hvda_read_vbar(void)
{
	uint32_t v;
	__asm__ volatile ("mrc p15, 0, %0, c12, c0, 0" : "=r"(v));
	return v;
}

static inline void hvda_write_vbar(uint32_t v)
{
	__asm__ volatile ("mcr p15, 0, %0, c12, c0, 0" :: "r"(v));
}

static inline void hvda_disable_irqs(void)
{
	__asm__ volatile ("cpsid i" ::: "memory");
	__asm__ volatile ("isb" ::: "memory");
}

static inline void hvda_invalidate_tlbs(void)
{
	__asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" :: "r"(0));
}

static inline void hvda_dsb_isb(void)
{
	__asm__ volatile ("dsb" ::: "memory");
	__asm__ volatile ("isb" ::: "memory");
}

/* ARMv7 fault status register access */
static inline uint32_t hvda_read_dfsr(void)
{
	uint32_t v;
	__asm__ volatile ("mrc p15, 0, %0, c5, c0, 0" : "=r"(v));
	return v;
}

static inline uint32_t hvda_read_dfar(void)
{
	uint32_t v;
	__asm__ volatile ("mrc p15, 0, %0, c6, c0, 0" : "=r"(v));
	return v;
}

extern uint8_t stage87_vectors[];  /* From vectors.S */

/* Global state for data abort handler */
static volatile uint32_t g_abort_count = 0;
static volatile uint32_t g_abort_dfsr = 0;
static volatile uint32_t g_abort_dfar = 0;
static volatile uint32_t g_abort_handled = 0;

static struct stage87_xnu_arm_vm_init_high_va_data_abort_handler_result g_result;

static uint32_t stage87_xnu_arm_vm_init_high_va_data_abort_handler_checksum(
    volatile const struct stage87_xnu_arm_vm_init_high_va_data_abort_handler_result *r)
{
	volatile const uint32_t *words = (volatile const uint32_t *)r;
	uint32_t count = (uint32_t)(offsetof(struct stage87_xnu_arm_vm_init_high_va_data_abort_handler_result, checksum) / sizeof(uint32_t));
	uint32_t chk = 0;
	for (uint32_t i = 0; i < count; i++) {
		chk ^= words[i];
	}
	return chk;
}

static void stage87_xnu_arm_vm_init_high_va_data_abort_handler_log(
    volatile const struct stage87_xnu_arm_vm_init_high_va_data_abort_handler_result *r)
{
	xnu_log_puts("stage87_xnu_arm_vm_init_high_va_data_abort_handler result:\n");
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_version", r->version);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_size", r->size);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_status", r->status);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_required_mask", r->required_mask);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_satisfied_mask", r->satisfied_mask);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_failure_mask", r->failure_mask);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_full_pmap_status", r->full_pmap_status);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_vectors_phys", r->vectors_phys);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_vectors_high_va", r->vectors_high_va);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_original_vbar", r->original_vbar);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_high_va_vbar_set", r->high_va_vbar_set);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_abort_triggered", r->abort_triggered);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_abort_handled", r->abort_handled);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_dfar", r->dfar);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_dfar_expected", r->dfar_expected);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_dfar_correct", r->dfar_correct);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_dfsr", r->dfsr);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_dfsr_valid", r->dfsr_valid);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_vbar_restored", r->vbar_restored);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_restored_vbar", r->restored_vbar);
	xnu_log_kv32("stage87_xnu_arm_vm_init_high_va_data_abort_handler_checksum", r->checksum);
}

/* Data abort C handler - called from vector_data_abort with stack pointer in r0 */
void stage87_data_abort_c_handler(uint32_t *stack_ptr)
{
	uint32_t dfsr = hvda_read_dfsr();
	uint32_t dfar = hvda_read_dfar();
	uint32_t lr = stack_ptr[5];  /* LR is at offset [5] after r0-r3, r12 */

	/* Record abort in global state */
	g_abort_dfsr = dfsr;
	g_abort_dfar = dfar;
	g_abort_handled = 1;

	/* Log for debugging (first few aborts only) */
	if (g_abort_count < 4) {
		log_puts("MI4IOS6_STAGE87 data abort: dfar=");
		log_hex32(dfar);
		log_puts(" dfsr=");
		log_hex32(dfsr);
		log_puts(" lr=");
		log_hex32(lr);
		log_puts("\n");
	}
	g_abort_count++;

	/* Skip the faulting instruction by advancing LR by 4 bytes.
	 * The faulting load instruction will not re-execute on return. */
	stack_ptr[5] = lr + 4;
}

int stage87_xnu_arm_vm_init_high_va_data_abort_handler_run(
    struct boot_args *args,
    struct stage87_xnu_entry_stub_result *entry_result)
{
	struct stage87_xnu_arm_vm_init_high_va_data_abort_handler_result *r = &g_result;
	const struct stage87_xnu_arm_vm_init_full_pmap_result *pmap_result;
	uint32_t vectors_phys = (uint32_t)(uintptr_t)&stage87_vectors;
	uint32_t vectors_high_va;
	uint32_t candidate_l1_base;
	uint32_t original_ttbr0;
	uint32_t original_vbar;
	volatile uint32_t *bad_ptr = (volatile uint32_t *)0xdeadc000;  /* 4-byte aligned unmapped address */
	uint32_t dummy;

	(void)args;
	(void)entry_result;

	/* Initialize result */
	memset(r, 0, sizeof(*r));
	r->version = STAGE87_XNU_ARM_VM_INIT_HIGH_VA_DATA_ABORT_HANDLER_VERSION;
	r->size = sizeof(*r);
	r->required_mask = STAGE87_XNU_ARM_VM_INIT_HIGH_VA_DATA_ABORT_HANDLER_REQUIRED_MASK;

	/* Check full pmap prerequisite (Stage86's full pmap install) */
	pmap_result = stage87_xnu_arm_vm_init_full_pmap_result();
	if (!pmap_result || pmap_result->status != STAGE87_STATUS_OK) {
		r->failure_mask |= STAGE87_XNU_ARM_VM_INIT_HIGH_VA_DATA_ABORT_HANDLER_FAIL_FULL_PMAP;
		goto finish;
	}
	r->full_pmap_status = pmap_result->status;
	r->satisfied_mask |= STAGE87_XNU_ARM_VM_INIT_HIGH_VA_DATA_ABORT_HANDLER_SAT_FULL_PMAP_OK;

	/* Compute high-VA address of exception vectors */
	vectors_high_va = pmap_result->virt_base + vectors_phys;
	r->vectors_phys = vectors_phys;
	r->vectors_high_va = vectors_high_va;

	/* Verify candidate L1 base is valid (16KB aligned) */
	candidate_l1_base = pmap_result->candidate_l1_base;
	if ((candidate_l1_base & 0x3fffu) != 0u || candidate_l1_base == 0u) {
		r->failure_mask |= STAGE87_XNU_ARM_VM_INIT_HIGH_VA_DATA_ABORT_HANDLER_FAIL_FULL_PMAP;
		goto finish;
	}

	/* Save original TTBR0 and VBAR */
	original_ttbr0 = hvda_read_ttbr0();
	original_vbar = hvda_read_vbar();
	r->original_vbar = original_vbar;

	/* Disable IRQs (safety: mask before reconfiguring VBAR) */
	hvda_disable_irqs();

	/* Re-install the Stage-owned candidate L1 */
	hvda_dsb_isb();
	hvda_write_ttbr0(candidate_l1_base);
	hvda_invalidate_tlbs();
	hvda_dsb_isb();

	/* Set VBAR to the high-VA projection of the exception vectors */
	hvda_write_vbar(vectors_high_va);
	hvda_dsb_isb();
	r->high_va_vbar_set = 1;
	r->satisfied_mask |= STAGE87_XNU_ARM_VM_INIT_HIGH_VA_DATA_ABORT_HANDLER_SAT_VBAR_SET;

	/* Reset abort globals */
	g_abort_handled = 0;
	g_abort_dfsr = 0;
	g_abort_dfar = 0;
	g_abort_count = 0;

	/* Trigger data abort by accessing unmapped address.
	 * This will vector through high-VA VBAR + 0x10 (data abort offset).
	 * The handler will record DFSR/DFAR and skip the faulting instruction. */
	dummy = *bad_ptr;
	(void)dummy;  /* Suppress unused warning */

	/* If we reach here, abort was handled and we skipped the faulting instruction */
	r->abort_triggered = 1;
	r->satisfied_mask |= STAGE87_XNU_ARM_VM_INIT_HIGH_VA_DATA_ABORT_HANDLER_SAT_ABORT_TRIGGERED;

	/* Verify abort was handled */
	if (g_abort_handled) {
		r->abort_handled = 1;
		r->satisfied_mask |= STAGE87_XNU_ARM_VM_INIT_HIGH_VA_DATA_ABORT_HANDLER_SAT_ABORT_HANDLED;
	} else {
		r->failure_mask |= STAGE87_XNU_ARM_VM_INIT_HIGH_VA_DATA_ABORT_HANDLER_FAIL_ABORT_NOT_HANDLED;
	}

	/* Verify DFAR is correct (should be 0xdeadc000) */
	r->dfar = g_abort_dfar;
	r->dfar_expected = 0xdeadc000;
	if (g_abort_dfar == 0xdeadc000) {
		r->dfar_correct = 1;
		r->satisfied_mask |= STAGE87_XNU_ARM_VM_INIT_HIGH_VA_DATA_ABORT_HANDLER_SAT_DFAR_CORRECT;
	}

	/* Verify DFSR indicates a fault (alignment fault=0x1 or translation fault=0x5/0x7) */
	r->dfsr = g_abort_dfsr;
	uint32_t dfsr_status = g_abort_dfsr & 0xfu;
	if (dfsr_status == 0x1u || dfsr_status == 0x5u || dfsr_status == 0x7u) {
		r->dfsr_valid = 1;
		r->satisfied_mask |= STAGE87_XNU_ARM_VM_INIT_HIGH_VA_DATA_ABORT_HANDLER_SAT_DFSR_VALID;
	}

	/* Restore original VBAR and TTBR0 */
	hvda_dsb_isb();
	hvda_write_vbar(original_vbar);
	hvda_write_ttbr0(original_ttbr0);
	hvda_invalidate_tlbs();
	hvda_dsb_isb();

	r->vbar_restored = 1;
	r->restored_vbar = hvda_read_vbar();
	r->satisfied_mask |= STAGE87_XNU_ARM_VM_INIT_HIGH_VA_DATA_ABORT_HANDLER_SAT_VBAR_RESTORED;

finish:
	/* Compute final status */
	if (r->satisfied_mask == r->required_mask && r->failure_mask == 0) {
		r->status = STAGE87_STATUS_OK;
	} else {
		r->status = STAGE87_STATUS_FAIL(r->failure_mask);
	}

	/* Compute checksum */
	r->checksum = stage87_xnu_arm_vm_init_high_va_data_abort_handler_checksum(r);

	/* Log result */
	stage87_xnu_arm_vm_init_high_va_data_abort_handler_log(r);

	return r->status == STAGE87_STATUS_OK ? 0 : -1;
}

const struct stage87_xnu_arm_vm_init_high_va_data_abort_handler_result *
stage87_xnu_arm_vm_init_high_va_data_abort_handler_result(void)
{
	return &g_result;
}
