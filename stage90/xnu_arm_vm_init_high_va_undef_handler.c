/* Stage88 High-VA Undefined Instruction Handler Window
 *
 * Proves ARM undefined instruction exception handlers can execute from the
 * L2-page-mapped high-virtual kernel address at 0x80000000+offset by intentionally
 * executing an undefined instruction with VBAR pointing to high-VA exception vectors.
 *
 * Stage86 proved: Asynchronous exceptions (IRQ) at high-VA
 * Stage87 proved: Synchronous exceptions (data abort) at high-VA
 *
 * Stage88 proves:
 * - Undefined instruction exception handler at high-VA
 * - Exception return from undefined instruction handler
 * - Execution continues after undefined instruction
 *
 * This completes validation of the primary exception types under high-VA pmap.
 */

#include "stage90.h"

/* Inline TTBR0/VBAR helpers (copied from high_va pattern) */
static inline uint32_t hvun_read_ttbr0(void)
{
	uint32_t v;
	__asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
	return v;
}

static inline void hvun_write_ttbr0(uint32_t v)
{
	__asm__ volatile ("mcr p15, 0, %0, c2, c0, 0" :: "r"(v));
}

static inline uint32_t hvun_read_vbar(void)
{
	uint32_t v;
	__asm__ volatile ("mrc p15, 0, %0, c12, c0, 0" : "=r"(v));
	return v;
}

static inline void hvun_write_vbar(uint32_t v)
{
	__asm__ volatile ("mcr p15, 0, %0, c12, c0, 0" :: "r"(v));
}

static inline void hvun_disable_irqs(void)
{
	__asm__ volatile ("cpsid i" ::: "memory");
	__asm__ volatile ("isb" ::: "memory");
}

static inline void hvun_invalidate_tlbs(void)
{
	__asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" :: "r"(0));
}

static inline void hvun_dsb_isb(void)
{
	__asm__ volatile ("dsb" ::: "memory");
	__asm__ volatile ("isb" ::: "memory");
}

extern uint8_t stage90_vectors[];  /* From vectors.S */

/* Global state for undefined instruction handler */
static volatile uint32_t g_undef_count = 0;
static volatile uint32_t g_undef_last_addr = 0;
static volatile uint32_t g_undef_handled = 0;

static struct stage90_xnu_arm_vm_init_high_va_undef_handler_result g_result;

static uint32_t stage90_xnu_arm_vm_init_high_va_undef_handler_checksum(
    volatile const struct stage90_xnu_arm_vm_init_high_va_undef_handler_result *r)
{
	volatile const uint32_t *words = (volatile const uint32_t *)r;
	uint32_t count = (uint32_t)(offsetof(struct stage90_xnu_arm_vm_init_high_va_undef_handler_result, checksum) / sizeof(uint32_t));
	uint32_t chk = 0;
	for (uint32_t i = 0; i < count; i++) {
		chk ^= words[i];
	}
	return chk;
}

static void stage90_xnu_arm_vm_init_high_va_undef_handler_log(
    volatile const struct stage90_xnu_arm_vm_init_high_va_undef_handler_result *r)
{
	xnu_log_puts("stage90_xnu_arm_vm_init_high_va_undef_handler result:\n");
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_version", r->version);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_size", r->size);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_status", r->status);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_required_mask", r->required_mask);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_satisfied_mask", r->satisfied_mask);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_failure_mask", r->failure_mask);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_full_pmap_status", r->full_pmap_status);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_vectors_phys", r->vectors_phys);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_vectors_high_va", r->vectors_high_va);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_original_vbar", r->original_vbar);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_high_va_vbar_set", r->high_va_vbar_set);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_undef_triggered", r->undef_triggered);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled", r->undef_handled);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_undef_count", r->undef_count);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_undef_last_addr", r->undef_last_addr);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_undef_addr_valid", r->undef_addr_valid);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_vbar_restored", r->vbar_restored);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_restored_vbar", r->restored_vbar);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_undef_handler_checksum", r->checksum);
}

/* Undefined instruction C handler - called from vector_undef with stack pointer in r0 */
void stage90_undef_c_handler(uint32_t *stack_ptr)
{
	uint32_t lr = stack_ptr[5];  /* LR is at offset [5] after r0-r3, r12 */
	uint32_t undef_instr_addr = lr - 4;  /* The undefined instruction address */

	/* Record exception in global state */
	g_undef_last_addr = undef_instr_addr;
	g_undef_handled = 1;

	/* Log for debugging (first few exceptions only) */
	if (g_undef_count < 4) {
		log_puts("MI4IOS6_STAGE90 undef: addr=");
		log_hex32(undef_instr_addr);
		log_puts(" lr=");
		log_hex32(lr);
		log_puts("\n");
	}
	g_undef_count++;

	/* No LR adjustment needed - LR already points to next instruction */
}

int stage90_xnu_arm_vm_init_high_va_undef_handler_run(
    struct boot_args *args,
    struct stage90_xnu_entry_stub_result *entry_result)
{
	struct stage90_xnu_arm_vm_init_high_va_undef_handler_result *r = &g_result;
	const struct stage90_xnu_arm_vm_init_full_pmap_result *pmap_result;
	uint32_t vectors_phys = (uint32_t)(uintptr_t)&stage90_vectors;
	uint32_t vectors_high_va;
	uint32_t candidate_l1_base;
	uint32_t original_ttbr0;
	uint32_t original_vbar;
	uint32_t undef_instr_addr_before;

	(void)args;
	(void)entry_result;

	/* Initialize result */
	memset(r, 0, sizeof(*r));
	r->version = STAGE90_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_VERSION;
	r->size = sizeof(*r);
	r->required_mask = STAGE90_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_REQUIRED_MASK;

	/* Check full pmap prerequisite */
	pmap_result = stage90_xnu_arm_vm_init_full_pmap_result();
	if (!pmap_result || pmap_result->status != STAGE90_STATUS_OK) {
		r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_FAIL_FULL_PMAP;
		goto finish;
	}
	r->full_pmap_status = pmap_result->status;
	r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_SAT_FULL_PMAP_OK;

	/* Compute high-VA address of exception vectors */
	vectors_high_va = pmap_result->virt_base + vectors_phys;
	r->vectors_phys = vectors_phys;
	r->vectors_high_va = vectors_high_va;

	/* Verify candidate L1 base is valid (16KB aligned) */
	candidate_l1_base = pmap_result->candidate_l1_base;
	if ((candidate_l1_base & 0x3fffu) != 0u || candidate_l1_base == 0u) {
		r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_FAIL_FULL_PMAP;
		goto finish;
	}

	/* Save original TTBR0 and VBAR */
	original_ttbr0 = hvun_read_ttbr0();
	original_vbar = hvun_read_vbar();
	r->original_vbar = original_vbar;

	/* Disable IRQs (safety: mask before reconfiguring VBAR) */
	hvun_disable_irqs();

	/* Re-install the Stage-owned candidate L1 */
	hvun_dsb_isb();
	hvun_write_ttbr0(candidate_l1_base);
	hvun_invalidate_tlbs();
	hvun_dsb_isb();

	/* Set VBAR to the high-VA projection of the exception vectors */
	hvun_write_vbar(vectors_high_va);
	hvun_dsb_isb();
	r->high_va_vbar_set = 1;
	r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_SAT_VBAR_SET;

	/* Reset undef globals */
	g_undef_handled = 0;
	g_undef_count = 0;
	g_undef_last_addr = 0;

	/* Trigger undefined instruction exception.
	 * This will vector through high-VA VBAR + 0x04 (undef offset).
	 * The handler will record the exception and return to the next instruction.
	 * We use a single asm block and capture the address of the undefined instruction
	 * itself via a local label so the validation is precise. */
	__asm__ volatile (
		"adr %[before], 0f\n"          /* address of the undef instruction below */
		"0:\n"
		".word 0xe7f000f0\n"           /* ARM undefined instruction pattern */
		: [before] "=r"(undef_instr_addr_before)
		:
		: "memory"
	);

	/* If we reach here, exception was handled and we continued to next instruction */
	r->undef_triggered = 1;
	r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_SAT_UNDEF_TRIGGERED;

	/* Verify exception was handled */
	if (g_undef_handled) {
		r->undef_handled = 1;
		r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_SAT_UNDEF_HANDLED;
	} else {
		r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_FAIL_UNDEF_NOT_HANDLED;
	}

	/* Record exception details */
	r->undef_count = g_undef_count;
	r->undef_last_addr = g_undef_last_addr;

	/* Verify the recorded address is in the expected range (within ~32 bytes of trigger point) */
	if (g_undef_last_addr >= undef_instr_addr_before &&
	    g_undef_last_addr < undef_instr_addr_before + 32) {
		r->undef_addr_valid = 1;
		r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_SAT_UNDEF_ADDR_VALID;
	}

	/* Restore original VBAR and TTBR0 */
	hvun_dsb_isb();
	hvun_write_vbar(original_vbar);
	hvun_write_ttbr0(original_ttbr0);
	hvun_invalidate_tlbs();
	hvun_dsb_isb();

	r->vbar_restored = 1;
	r->restored_vbar = hvun_read_vbar();
	r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_UNDEF_HANDLER_SAT_VBAR_RESTORED;

finish:
	/* Compute final status */
	if (r->satisfied_mask == r->required_mask && r->failure_mask == 0) {
		r->status = STAGE90_STATUS_OK;
	} else {
		r->status = STAGE90_STATUS_FAIL(r->failure_mask);
	}

	/* Compute checksum */
	r->checksum = stage90_xnu_arm_vm_init_high_va_undef_handler_checksum(r);

	/* Log result */
	stage90_xnu_arm_vm_init_high_va_undef_handler_log(r);

	return r->status == STAGE90_STATUS_OK ? 0 : -1;
}

const struct stage90_xnu_arm_vm_init_high_va_undef_handler_result *
stage90_xnu_arm_vm_init_high_va_undef_handler_result(void)
{
	return &g_result;
}
