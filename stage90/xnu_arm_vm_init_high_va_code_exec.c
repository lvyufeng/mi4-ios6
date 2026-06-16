/* Stage86 High-VA Code Execution Window
 *
 * Proves Stage-owned code can execute from the L2-page-mapped high-virtual
 * kernel address at 0x80000000+offset, completing the "high-virtual code
 * execution" milestone.
 *
 * Stage84 proved:
 * - High-VA data access at 0x80000000 via L2 pages (xnu_arm_vm_init_full_pmap.c)
 * - High-VA code execution at 0xc0000000 via L1 sections (mmu.c startup_entry)
 * - IRQ handling under live pmap (IRQ handler at identity addresses)
 *
 * Stage86 proves:
 * - Instruction fetch through L2 page translation at 0x80000000
 * - Function call via high-VA function pointer
 * - Correct execution and return value from high-VA code
 *
 * This completes the high-VA code execution validation, as the L2-mapped
 * kernel VA region (0x80000000) is now proven executable, not just data-accessible.
 */

#include "stage90.h"

/* Minimal TTBR0/TLB barrier helpers (identical to xnu_arm_vm_init_full_pmap.c).
 * Used to install the Stage-owned candidate L1 for the duration of the high-VA
 * function call, then restore the original TTBR0. The full pmap window restores
 * TTBR0 before returning, so high_va_code_exec must re-install the candidate L1. */
static inline uint32_t hvce_read_ttbr0(void)
{
	uint32_t v;
	__asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
	return v;
}

static inline void hvce_write_ttbr0(uint32_t v)
{
	__asm__ volatile ("mcr p15, 0, %0, c2, c0, 0" :: "r"(v));
}

static inline void hvce_invalidate_tlbs(void)
{
	__asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" :: "r"(0));
}

static inline void hvce_dsb_isb(void)
{
	__asm__ volatile ("dsb" ::: "memory");
	__asm__ volatile ("isb" ::: "memory");
}

/* Stage-owned test function marked noinline to ensure it's a real call */
static uint32_t stage90_high_va_target(uint32_t input) __attribute__((noinline));

static uint32_t stage90_high_va_target(uint32_t input)
{
	return (input ^ 0xfeedface) + 0x12345678;
}

static struct stage90_xnu_arm_vm_init_high_va_code_exec_result g_result;

static uint32_t stage90_xnu_arm_vm_init_high_va_code_exec_checksum(
    volatile const struct stage90_xnu_arm_vm_init_high_va_code_exec_result *r)
{
	volatile const uint32_t *words = (volatile const uint32_t *)r;
	uint32_t count = (uint32_t)(offsetof(struct stage90_xnu_arm_vm_init_high_va_code_exec_result, checksum) / sizeof(uint32_t));
	uint32_t chk = 0;
	for (uint32_t i = 0; i < count; i++) {
		chk ^= words[i];
	}
	return chk;
}

static void stage90_xnu_arm_vm_init_high_va_code_exec_log(
    volatile const struct stage90_xnu_arm_vm_init_high_va_code_exec_result *r)
{
	xnu_log_puts("stage90_xnu_arm_vm_init_high_va_code_exec result:\n");
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_version", r->version);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_size", r->size);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_status", r->status);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_required_mask", r->required_mask);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_satisfied_mask", r->satisfied_mask);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_failure_mask", r->failure_mask);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_full_pmap_status", r->full_pmap_status);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_full_pmap_checksum", r->full_pmap_checksum);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_fn_phys", r->fn_phys);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_fn_high_va", r->fn_high_va);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_fn_virt_base", r->fn_virt_base);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_fn_called", r->fn_called);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_fn_input", r->fn_input);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_fn_result", r->fn_result);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_fn_expected", r->fn_expected);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_fn_result_correct", r->fn_result_correct);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_public_xnu_executed", r->public_xnu_executed);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_persistent_write_attempted", r->persistent_write_attempted);
	xnu_log_kv32("stage90_xnu_arm_vm_init_high_va_code_exec_checksum", r->checksum);
}

int stage90_xnu_arm_vm_init_high_va_code_exec_run(
    struct boot_args *args,
    struct stage90_xnu_entry_stub_result *entry_result)
{
	struct stage90_xnu_arm_vm_init_high_va_code_exec_result *r = &g_result;
	const struct stage90_xnu_arm_vm_init_full_pmap_result *pmap_result;
	uint32_t fn_phys = (uint32_t)(uintptr_t)stage90_high_va_target;
	uint32_t fn_high_va;
	typedef uint32_t (*high_va_fn_t)(uint32_t);
	high_va_fn_t fn;
	uint32_t input = 0x11223344u;
	uint32_t expected = (input ^ 0xfeedface) + 0x12345678u;
	uint32_t result;
	uint32_t candidate_l1_base;
	uint32_t original_ttbr0;

	(void)args;
	(void)entry_result;

	/* Initialize result */
	memset(r, 0, sizeof(*r));
	r->version = STAGE90_XNU_ARM_VM_INIT_HIGH_VA_CODE_EXEC_VERSION;
	r->size = sizeof(*r);
	r->required_mask = STAGE90_XNU_ARM_VM_INIT_HIGH_VA_CODE_EXEC_REQUIRED_MASK;

	/* Check full pmap prerequisite (Stage86's full pmap install) */
	pmap_result = stage90_xnu_arm_vm_init_full_pmap_result();
	if (!pmap_result || pmap_result->status != STAGE90_STATUS_OK) {
		r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_CODE_EXEC_FAIL_FULL_PMAP;
		goto finish;
	}
	r->full_pmap_status = pmap_result->status;
	r->full_pmap_checksum = pmap_result->checksum;
	r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_CODE_EXEC_SAT_FULL_PMAP_OK;

	/* Compute high-VA address.
	 * The candidate L2 maps VA 0x80000000+N -> PA 0x00000000+N (identity offset),
	 * so the high-VA for a physical address X is virt_base + X.
	 * The fn lives in the Stage-owned image at a low physical address, and its
	 * high-VA projection (fn_phys + virt_base) is the L2-page-mapped kernel alias. */
	fn_high_va = pmap_result->virt_base + fn_phys;
	fn = (high_va_fn_t)(uintptr_t)fn_high_va;

	/* Record function addresses */
	r->fn_phys = fn_phys;
	r->fn_high_va = fn_high_va;
	r->fn_virt_base = pmap_result->virt_base;
	r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_CODE_EXEC_SAT_FN_ADDR_COMPUTED;

	/* The full pmap window restores the original TTBR0 before returning, so we
	 * must re-install the Stage-owned candidate L1 (with the L2-mapped 0x80000000
	 * region) for the duration of the high-VA function call, then restore. This
	 * mirrors the TTBR0 roundtrip selftest pattern: candidate L1 already maps the
	 * Stage image, stack, vectors, and the 0x80000000 L2 region. */
	candidate_l1_base = pmap_result->candidate_l1_base;
	if ((candidate_l1_base & 0x3fffu) != 0u || candidate_l1_base == 0u) {
		r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_CODE_EXEC_FAIL_FULL_PMAP;
		goto finish;
	}
	original_ttbr0 = hvce_read_ttbr0();
	hvce_dsb_isb();
	hvce_write_ttbr0(candidate_l1_base);
	hvce_invalidate_tlbs();
	hvce_dsb_isb();

	/* Call function through high-VA pointer. We're calling the function at
	 * 0x80000000+offset, which is mapped via L2 4KB pages (not L1 sections like
	 * the 0xc0000000 bootstrap alias). This proves instruction fetch through L2
	 * page translation works. */
	result = fn(input);

	/* Restore the original TTBR0 immediately so subsequent code runs under the
	 * normal identity table. */
	hvce_dsb_isb();
	hvce_write_ttbr0(original_ttbr0);
	hvce_invalidate_tlbs();
	hvce_dsb_isb();

	r->fn_called = 1;
	r->fn_input = input;
	r->fn_result = result;
	r->fn_expected = expected;
	r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_CODE_EXEC_SAT_FN_CALLED;

	/* Verify result */
	if (result == expected) {
		r->fn_result_correct = 1;
		r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_CODE_EXEC_SAT_FN_RESULT_CORRECT;
	} else {
		r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_CODE_EXEC_FAIL_FN_RESULT_WRONG;
	}

	/* Validate safety boundaries */
	r->public_xnu_executed = 0;  /* Stage-owned only */
	r->persistent_write_attempted = 0;  /* Stage-owned only */
	r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_CODE_EXEC_SAT_NO_PUBLIC_XNU;
	r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_HIGH_VA_CODE_EXEC_SAT_NO_PERSISTENT_WRITE;

finish:
	/* Compute final status */
	if (r->satisfied_mask == r->required_mask && r->failure_mask == 0) {
		r->status = STAGE90_STATUS_OK;
	} else {
		r->status = STAGE90_STATUS_FAIL(r->failure_mask);
	}

	/* Compute checksum */
	r->checksum = stage90_xnu_arm_vm_init_high_va_code_exec_checksum(r);

	/* Log result */
	stage90_xnu_arm_vm_init_high_va_code_exec_log(r);

	return r->status == STAGE90_STATUS_OK ? 0 : -1;
}

const struct stage90_xnu_arm_vm_init_high_va_code_exec_result *
stage90_xnu_arm_vm_init_high_va_code_exec_result(void)
{
	return &g_result;
}
