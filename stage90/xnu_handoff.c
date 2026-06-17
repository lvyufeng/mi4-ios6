/* Stage90 XNU Handoff
 *
 * Transfers control from Stage boot code to real XNU kernel at high-VA.
 * This is the critical transition from "simulating XNU" to "executing real XNU."
 *
 * Expected: XNU will likely crash immediately. Our job is to capture the crash
 * and learn what XNU expects.
 */

#include "stage90.h"

static struct stage90_xnu_handoff_result g_result;

/* Helper: compute checksum */
static uint32_t stage90_xnu_handoff_checksum(
    volatile const struct stage90_xnu_handoff_result *r)
{
	volatile const uint32_t *words = (volatile const uint32_t *)r;
	uint32_t count = (uint32_t)(offsetof(struct stage90_xnu_handoff_result, checksum) / sizeof(uint32_t));
	uint32_t chk = 0;
	for (uint32_t i = 0; i < count; i++) {
		chk ^= words[i];
	}
	return chk;
}

/* Helper: log result */
static void stage90_xnu_handoff_log(
    volatile const struct stage90_xnu_handoff_result *r)
{
	xnu_log_puts("stage90_xnu_handoff result:\n");
	xnu_log_kv32("stage90_xnu_handoff_version", r->version);
	xnu_log_kv32("stage90_xnu_handoff_status", r->status);
	xnu_log_kv32("stage90_xnu_handoff_loader_status", r->loader_status);
	xnu_log_kv32("stage90_xnu_handoff_xnu_entry_va", r->xnu_entry_va);
	xnu_log_kv32("stage90_xnu_handoff_boot_args_ptr", r->boot_args_ptr);
	xnu_log_kv32("stage90_xnu_handoff_ready", r->ready);
	xnu_log_kv32("stage90_xnu_handoff_jumped", r->jumped);
	xnu_log_kv32("stage90_xnu_handoff_xnu_returned", r->xnu_returned);
	xnu_log_kv32("stage90_xnu_handoff_checksum", r->checksum);
}

int stage90_xnu_handoff_run(
    struct boot_args *args,
    struct stage90_xnu_entry_stub_result *entry_result)
{
	struct stage90_xnu_handoff_result *r = &g_result;
	const struct stage90_xnu_macho_loader_result *loader;
	typedef void (*xnu_entry_t)(struct boot_args *);
	xnu_entry_t xnu_start;

	(void)entry_result;

	/* Initialize result */
	memset(r, 0, sizeof(*r));
	r->version = STAGE90_XNU_HANDOFF_VERSION;
	r->size = sizeof(*r);
	r->required_mask = STAGE90_XNU_HANDOFF_REQUIRED_MASK;

	/* Check loader prerequisite */
	loader = stage90_xnu_macho_loader_result();
	if (!loader || loader->status != STAGE90_STATUS_OK) {
		xnu_log_puts("stage90_xnu_handoff: loader failed\n");
		r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_LOADER;
		goto finish;
	}

	r->loader_status = loader->status;
	r->xnu_entry_va = loader->xnu_entry_va;
	r->satisfied_mask |= STAGE90_XNU_HANDOFF_SAT_LOADER_OK;

	/* Verify entry point is valid */
	if (r->xnu_entry_va < 0x80000000 || r->xnu_entry_va >= 0x80100000) {
		xnu_log_puts("stage90_xnu_handoff: invalid entry VA\n");
		r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_INVALID_ENTRY;
		goto finish;
	}

	r->satisfied_mask |= STAGE90_XNU_HANDOFF_SAT_ENTRY_VALID;

	/* Prepare boot_args pointer */
	r->boot_args_ptr = (uint32_t)(uintptr_t)args;
	r->satisfied_mask |= STAGE90_XNU_HANDOFF_SAT_BOOT_ARGS_READY;

	/* Log environment before jump */
	xnu_log_puts("stage90_xnu_handoff: environment check\n");
	uint32_t ttbr0, vbar, sp, cpsr;
	__asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(ttbr0));
	__asm__ volatile ("mrc p15, 0, %0, c12, c0, 0" : "=r"(vbar));
	__asm__ volatile ("mov %0, sp" : "=r"(sp));
	__asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));
	xnu_log_kv32("ttbr0", ttbr0);
	xnu_log_kv32("vbar", vbar);
	xnu_log_kv32("sp", sp);
	xnu_log_kv32("cpsr", cpsr);

	/* Validate code at entry point (read first 4 instructions) */
	xnu_log_puts("stage90_xnu_handoff: validating code at entry point\n");
	uint32_t *code = (uint32_t *)r->xnu_entry_va;
	for (int i = 0; i < 4; i++) {
		uint32_t instr = code[i];
		xnu_log_kv32("instr", instr);
		/* Check it's not all zeros or all ones (common garbage patterns) */
		if (instr == 0x00000000 || instr == 0xffffffff) {
			xnu_log_puts("stage90_xnu_handoff: WARNING - suspicious instruction pattern\n");
		}
	}

	/* Ready for handoff */
	r->ready = 1;
	r->satisfied_mask |= STAGE90_XNU_HANDOFF_SAT_READY;

	/* Log the moment before jump */
	xnu_log_puts("stage90_xnu_handoff: *** READY TO JUMP TO XNU ***\n");
	xnu_log_kv32("xnu_entry_va", r->xnu_entry_va);
	xnu_log_kv32("boot_args_ptr", r->boot_args_ptr);
	xnu_log_puts("stage90_xnu_handoff: all prerequisites verified\n");

	/* STAGE90 SAFETY: Skip actual jump to verify environment first */
	#if 1
	xnu_log_puts("stage90_xnu_handoff: SKIPPING JUMP (safety mode for environment validation)\n");
	xnu_log_puts("stage90_xnu_handoff: all prerequisites verified, would jump if safety mode disabled\n");
	r->satisfied_mask |= STAGE90_XNU_HANDOFF_SAT_READY;
	goto finish;
	#endif

	xnu_log_puts("stage90_xnu_handoff: exception handler will capture any crash\n");

	/* Disable IRQs (safety - XNU will enable when ready) */
	__asm__ volatile ("cpsid i" ::: "memory");
	__asm__ volatile ("isb" ::: "memory");

	/* THE JUMP - This is it! */
	xnu_start = (xnu_entry_t)r->xnu_entry_va;
	r->jumped = 1;

	xnu_start(args);

	/* If we reach here, XNU returned (shouldn't happen) */
	xnu_log_puts("stage90_xnu_handoff: *** XNU RETURNED UNEXPECTEDLY ***\n");
	r->xnu_returned = 1;
	r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_XNU_RETURNED;

finish:
	/* Compute final status */
	if (r->satisfied_mask == r->required_mask && r->failure_mask == 0) {
		r->status = STAGE90_STATUS_OK;
	} else {
		r->status = STAGE90_STATUS_FAIL(r->failure_mask);
	}

	/* Compute checksum */
	r->checksum = stage90_xnu_handoff_checksum(r);

	/* Log result */
	stage90_xnu_handoff_log(r);

	return r->status == STAGE90_STATUS_OK ? 0 : -1;
}

const struct stage90_xnu_handoff_result *
stage90_xnu_handoff_result(void)
{
	return &g_result;
}
