/* Stage90 XNU Handoff
 *
 * Stage90 is still inside the project's safety boundary: the embedded Mach-O
 * fixture remains inert/non-executable. The handoff path now validates the
 * real post-jump machinery by installing the Stage-owned candidate L1 and
 * jumping to a Stage-owned no-return function through its 0x80000000+phys
 * high-VA alias. A timer watchdog samples that target's interrupted PC and
 * reboots through PS_HOLD so the samples are readable in /proc/last_kmsg.
 */

#include "stage90.h"

static struct stage90_xnu_handoff_result g_result;

static inline uint32_t handoff_read_ttbr0(void)
{
	uint32_t v;
	__asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
	return v;
}

static inline void handoff_write_ttbr0(uint32_t v)
{
	__asm__ volatile ("mcr p15, 0, %0, c2, c0, 0" :: "r"(v) : "memory");
}

static inline void handoff_invalidate_tlbs(void)
{
	__asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" :: "r"(0) : "memory");
}

static inline void handoff_dsb_isb(void)
{
	__asm__ volatile ("dsb" ::: "memory");
	__asm__ volatile ("isb" ::: "memory");
}

/*
 * Bound on the identity-mapped preflight loop. The sampling watchdog is expected
 * to fire after SAMPLE_INTERVAL_US * SAMPLE_MAX (~8ms); the loop is given 4x that
 * and then returns, so a watchdog that never fires is reported as a failure
 * instead of hanging the device.
 */
#define STAGE90_HANDOFF_PREFLIGHT_LOOP_US \
	(STAGE90_HANDOFF_SAMPLE_INTERVAL_US * STAGE90_HANDOFF_SAMPLE_MAX * 4u)

#if STAGE90_HANDOFF_MODE == STAGE90_HANDOFF_MODE_FULL
/*
 * Stage-owned no-return handoff target. We intentionally keep the embedded
 * Mach-O fixture inert/non-executable and jump to this local function through
 * its 0x80000000+phys high-VA alias instead. That validates the post-jump
 * PC-sampling watchdog path without crossing the no-macho-exec boundary.
 */
static volatile uint32_t stage90_handoff_sample_target_entered;
static volatile uint32_t stage90_handoff_sample_target_args;

static void stage90_handoff_sample_target(struct boot_args *args) __attribute__((noinline, noreturn));

static void stage90_handoff_sample_target(struct boot_args *args)
{
	stage90_handoff_sample_target_entered = 0x53393048u; /* 'S90H' */
	stage90_handoff_sample_target_args = (uint32_t)(uintptr_t)args;
	handoff_dsb_isb();

	for (;;) {
		__asm__ volatile ("nop" ::: "memory");
	}
}
#endif

#if STAGE90_HANDOFF_MODE != STAGE90_HANDOFF_MODE_FULL
static volatile uint32_t stage90_handoff_preflight_loop_entered;
static volatile uint32_t stage90_handoff_preflight_loop_args;
static volatile uint32_t stage90_handoff_preflight_loop_ticks;

static void stage90_handoff_preflight_watchdog_loop(struct boot_args *args) __attribute__((noinline));

static void stage90_handoff_preflight_watchdog_loop(struct boot_args *args)
{
	uint64_t start;

	stage90_handoff_preflight_loop_entered = 0x50393048u; /* 'P90H' */
	stage90_handoff_preflight_loop_args = (uint32_t)(uintptr_t)args;
	handoff_dsb_isb();

	start = timebase_ticks();
	while (timebase_elapsed_us(start, timebase_ticks()) < STAGE90_HANDOFF_PREFLIGHT_LOOP_US) {
		stage90_handoff_preflight_loop_ticks++;
		__asm__ volatile ("nop" ::: "memory");
	}
}
#endif

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
	xnu_log_kv32("stage90_xnu_handoff_mode", r->handoff_mode);
	xnu_log_kv32("stage90_xnu_handoff_entry_ladder_level", r->entry_ladder_level);
	xnu_log_kv32("stage90_xnu_handoff_loader_status", r->loader_status);
	xnu_log_kv32("stage90_xnu_handoff_xnu_entry_va", r->xnu_entry_va);
	xnu_log_kv32("stage90_xnu_handoff_stage_target_phys", r->stage_target_phys);
	xnu_log_kv32("stage90_xnu_handoff_stage_target_high_va", r->stage_target_high_va);
	xnu_log_kv32("stage90_xnu_handoff_boot_args_ptr", r->boot_args_ptr);
	xnu_log_kv32("stage90_xnu_handoff_full_pmap_status", r->full_pmap_status);
	xnu_log_kv32("stage90_xnu_handoff_candidate_l1_base", r->candidate_l1_base);
	xnu_log_kv32("stage90_xnu_handoff_original_ttbr0", r->original_ttbr0);
	xnu_log_kv32("stage90_xnu_handoff_handoff_ttbr0", r->handoff_ttbr0);
	xnu_log_kv32("stage90_xnu_handoff_ready", r->ready);
	xnu_log_kv32("stage90_xnu_handoff_jumped", r->jumped);
	xnu_log_kv32("stage90_xnu_handoff_xnu_returned", r->xnu_returned);
	xnu_log_kv32("stage90_xnu_handoff_jumped_observed", r->jumped_observed);
	xnu_log_kv32("stage90_xnu_handoff_xnu_returned_observed", r->xnu_returned_observed);
	xnu_log_kv32("stage90_xnu_handoff_pc_sample_count", r->pc_sample_count);
	xnu_log_kv32("stage90_xnu_handoff_pc_sample_last_pc", r->pc_sample_last_pc);
	xnu_log_kv32("stage90_xnu_handoff_watchdog_fired", r->watchdog_fired);
	xnu_log_kv32("stage90_xnu_handoff_preflight_watchdog_armed", r->preflight_watchdog_armed);
	xnu_log_kv32("stage90_xnu_handoff_preflight_loop_entered", r->preflight_loop_entered);
	xnu_log_kv32("stage90_xnu_handoff_preflight_loop_ticks", r->preflight_loop_ticks);
	xnu_log_kv32("stage90_xnu_handoff_checksum", r->checksum);
}

int stage90_xnu_handoff_run(
    struct boot_args *args,
    struct stage90_xnu_entry_stub_result *entry_result)
{
	struct stage90_xnu_handoff_result *r = &g_result;
	const struct stage90_xnu_macho_loader_result *loader;
#if STAGE90_HANDOFF_MODE == STAGE90_HANDOFF_MODE_FULL
	const struct stage90_xnu_arm_vm_init_full_pmap_result *pmap_result;
	typedef void (*handoff_entry_t)(struct boot_args *);
	handoff_entry_t target_start;
	uint32_t *code;
#endif
	uint32_t vbar, sp, cpsr;
	uint32_t candidate_l1_installed = 0u;

	/* Initialize result */
	memset(r, 0, sizeof(*r));
	r->version = STAGE90_XNU_HANDOFF_VERSION;
	r->size = sizeof(*r);
	r->required_mask = STAGE90_XNU_HANDOFF_REQUIRED_MASK_FOR_MODE;
	r->handoff_mode = STAGE90_HANDOFF_MODE;
	r->entry_ladder_level = entry_result ? entry_result->ladder_level : STAGE90_ENTRY_LADDER_LEVEL;

	/* Check loader prerequisite. The fixture stays inert; we retain its entry for diagnostics only. */
	loader = stage90_xnu_macho_loader_result();
	if (!loader || loader->status != STAGE90_STATUS_OK) {
		xnu_log_puts("stage90_xnu_handoff: loader failed\n");
		r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_LOADER;
		goto finish;
	}

	r->loader_status = loader->status;
	r->xnu_entry_va = loader->xnu_entry_va;
	r->satisfied_mask |= STAGE90_XNU_HANDOFF_SAT_LOADER_OK;

	/* Verify the inert Mach-O fixture entry point is in the expected kernel VA window. */
	if (r->xnu_entry_va < 0x80000000u || r->xnu_entry_va >= 0x80100000u) {
		xnu_log_puts("stage90_xnu_handoff: invalid fixture entry VA\n");
		r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_INVALID_ENTRY;
		goto finish;
	}
	r->satisfied_mask |= STAGE90_XNU_HANDOFF_SAT_ENTRY_VALID;

	/* Prepare boot_args pointer */
	r->boot_args_ptr = (uint32_t)(uintptr_t)args;
	r->satisfied_mask |= STAGE90_XNU_HANDOFF_SAT_BOOT_ARGS_READY;

#if STAGE90_HANDOFF_MODE == STAGE90_HANDOFF_MODE_HARD_SKIP
	xnu_log_puts("stage90_xnu_handoff: HARD_SKIP active - no L1 switch, no loop, no IRQ, no jump\n");
	r->original_ttbr0 = handoff_read_ttbr0();
	r->handoff_ttbr0 = r->original_ttbr0;
	r->ready = 1u;
	r->jumped = 0u;
	r->xnu_returned = 0u;
	r->jumped_observed = 0u;
	r->xnu_returned_observed = 0u;
	r->pc_sample_count = 0u;
	r->pc_sample_last_pc = 0u;
	r->watchdog_fired = 0u;
	r->satisfied_mask = r->required_mask;
	r->status = STAGE90_STATUS_OK;
	r->checksum = stage90_xnu_handoff_checksum(r);
	stage90_xnu_handoff_log(r);
	return 0;
#endif

#if STAGE90_HANDOFF_MODE == STAGE90_HANDOFF_MODE_FULL
	/* The high-VA handoff requires the candidate L1 built by full_pmap. */
	pmap_result = stage90_xnu_arm_vm_init_full_pmap_result();
	if (!pmap_result || pmap_result->status != STAGE90_STATUS_OK) {
		xnu_log_puts("stage90_xnu_handoff: full_pmap prerequisite failed\n");
		r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_FULL_PMAP;
		goto finish;
	}
	r->full_pmap_status = pmap_result->status;
	r->candidate_l1_base = pmap_result->candidate_l1_base;
	r->satisfied_mask |= STAGE90_XNU_HANDOFF_SAT_FULL_PMAP_OK;

	if (r->candidate_l1_base == 0u || (r->candidate_l1_base & 0x3fffu) != 0u) {
		xnu_log_puts("stage90_xnu_handoff: invalid candidate L1 base\n");
		r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_CANDIDATE_L1;
		goto finish;
	}
	r->satisfied_mask |= STAGE90_XNU_HANDOFF_SAT_CANDIDATE_L1_READY;

	/*
	 * Compute the high-VA alias of the Stage-owned target. The candidate L2 maps
	 * VA virt_base+N -> PA N for N in [0, 1MB). Keep the target inside that proven
	 * L2 window and do not execute the inert Mach-O fixture.
	 */
	r->stage_target_phys = (uint32_t)(uintptr_t)stage90_handoff_sample_target;
	r->stage_target_high_va = pmap_result->virt_base + r->stage_target_phys;
	if (r->stage_target_phys >= 0x00100000u ||
	    r->stage_target_high_va < pmap_result->virt_base ||
	    r->stage_target_high_va >= (pmap_result->virt_base + 0x00100000u)) {
		xnu_log_puts("stage90_xnu_handoff: Stage-owned target outside candidate L2 window\n");
		r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_STAGE_TARGET;
		goto finish;
	}
	/*
	 * Non-executable-entry guard, address half: the fixture's own entry VA must
	 * never be the jump target. The address window alone cannot decide this - the
	 * Stage-owned alias and the fixture share the fixture's VA window - so the
	 * content check below adds the other half.
	 */
	if (r->stage_target_high_va == r->xnu_entry_va || (r->stage_target_high_va & 0x3u) != 0u) {
		xnu_log_puts("stage90_xnu_handoff: jump target is the inert fixture entry or unaligned\n");
		xnu_log_kv32("stage_target_high_va", r->stage_target_high_va);
		xnu_log_kv32("diagnostic_inert_fixture_entry_va", r->xnu_entry_va);
		r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_STAGE_TARGET;
		goto finish;
	}
	r->satisfied_mask |= STAGE90_XNU_HANDOFF_SAT_STAGE_TARGET_READY;

	/* Log environment before installing the candidate L1. */
	xnu_log_puts("stage90_xnu_handoff: environment check before candidate L1\n");
	r->original_ttbr0 = handoff_read_ttbr0();
	__asm__ volatile ("mrc p15, 0, %0, c12, c0, 0" : "=r"(vbar));
	__asm__ volatile ("mov %0, sp" : "=r"(sp));
	__asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));
	xnu_log_kv32("ttbr0_before_candidate", r->original_ttbr0);
	xnu_log_kv32("vbar_before_candidate", vbar);
	xnu_log_kv32("sp_before_candidate", sp);
	xnu_log_kv32("cpsr_before_candidate", cpsr);
	xnu_log_kv32("diagnostic_inert_fixture_entry_va", r->xnu_entry_va);
	xnu_log_kv32("stage_target_phys", r->stage_target_phys);
	xnu_log_kv32("stage_target_high_va", r->stage_target_high_va);
#else
	/*
	 * PREFLIGHT_WATCHDOG_ONLY deliberately does not touch the candidate L1: it
	 * never reads the full_pmap result, never computes a high-VA alias and never
	 * requires the L1 SAT bits. Everything runs under the original known-good
	 * mapping so the watchdog path is exercised on its own.
	 */
	r->original_ttbr0 = handoff_read_ttbr0();
	r->handoff_ttbr0 = r->original_ttbr0;
#endif

#if STAGE90_HANDOFF_MODE == STAGE90_HANDOFF_MODE_FULL
	/* Install the candidate L1 and keep it live across the no-return handoff. */
	xnu_log_puts("stage90_xnu_handoff: installing candidate L1 for high-VA handoff\n");
	handoff_dsb_isb();
	handoff_write_ttbr0(r->candidate_l1_base);
	handoff_invalidate_tlbs();
	handoff_dsb_isb();
	candidate_l1_installed = 1u;
	r->handoff_ttbr0 = handoff_read_ttbr0();
	if ((r->handoff_ttbr0 & 0xffffc000u) != (r->candidate_l1_base & 0xffffc000u)) {
		xnu_log_puts("stage90_xnu_handoff: candidate L1 TTBR0 mismatch\n");
		r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_CANDIDATE_L1;
		goto finish;
	}

	__asm__ volatile ("mrc p15, 0, %0, c12, c0, 0" : "=r"(vbar));
	__asm__ volatile ("mov %0, sp" : "=r"(sp));
	__asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));
	xnu_log_kv32("ttbr0_after_candidate", r->handoff_ttbr0);
	xnu_log_kv32("vbar_after_candidate", vbar);
	xnu_log_kv32("sp_after_candidate", sp);
	xnu_log_kv32("cpsr_after_candidate", cpsr);

	/*
	 * Non-executable-entry guard, content half. Read the first word at the target
	 * under the candidate L1 and refuse to jump if it is the fixture's __TEXT
	 * marker: that means the "target" resolves to fixture data, not to code, and
	 * jumping there executes ASCII and hangs the device with no output - exactly
	 * what the first Stage90 handoff attempt did. Also flag words that are
	 * entirely blank, which a valid mapped code page never starts with.
	 */
	xnu_log_puts("stage90_xnu_handoff: validating Stage-owned high-VA target code\n");
	code = (uint32_t *)(uintptr_t)r->stage_target_high_va;
	xnu_log_kv32("fixture_text_magic", STAGE90_HANDOFF_GUARD_FIXTURE_TEXT_MAGIC);
	if (code[0] == STAGE90_HANDOFF_GUARD_FIXTURE_TEXT_MAGIC) {
		xnu_log_puts("stage90_xnu_handoff: target holds the inert fixture marker; refusing to jump\n");
		r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_STAGE_TARGET;
		goto finish;
	}
	for (uint32_t i = 0u; i < 4u; i++) {
		uint32_t instr = code[i];
		xnu_log_kv32("stage_target_instr", instr);
		if (instr == 0x00000000u || instr == 0xffffffffu) {
			xnu_log_puts("stage90_xnu_handoff: WARNING - suspicious target instruction pattern\n");
		}
	}

	/* Ready for handoff */
	r->ready = 1;
	r->satisfied_mask |= STAGE90_XNU_HANDOFF_SAT_READY;

	/* Log the moment before jump */
	xnu_log_puts("stage90_xnu_handoff: *** READY TO JUMP TO STAGE-OWNED HIGH-VA TARGET ***\n");
	xnu_log_kv32("diagnostic_inert_fixture_entry_va", r->xnu_entry_va);
	xnu_log_kv32("stage_target_high_va", r->stage_target_high_va);
	xnu_log_kv32("boot_args_ptr", r->boot_args_ptr);
	xnu_log_puts("stage90_xnu_handoff: all prerequisites verified\n");

	xnu_log_puts("stage90_xnu_handoff: arming PC-sampling watchdog\n");

	/*
	 * Arm a periodic timer that will interrupt the target every SAMPLE_INTERVAL_US
	 * and record the interrupted PC into the sample ring. The IRQ handler dumps
	 * the samples and warm-reboots after SAMPLE_MAX samples.
	 */
	if (!stage90_arm_pc_sampling_watchdog(STAGE90_HANDOFF_SAMPLE_INTERVAL_US,
	                                      STAGE90_HANDOFF_SAMPLE_MAX)) {
		xnu_log_puts("stage90_xnu_handoff: watchdog arm failed; refusing no-return jump\n");
		r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_WATCHDOG;
		goto finish;
	}

	xnu_log_puts("stage90_xnu_handoff: *** JUMPING TO STAGE-OWNED HIGH-VA TARGET ***\n");

	/*
	 * THE JUMP - IRQs are enabled (arm opened them). The target is declared
	 * __attribute__((noreturn)) and spins forever, so the only ways out of this
	 * call are the sampling watchdog's reboot (the expected path) or a fault. If
	 * the call does return, the jump did not reach the target's body at all, and
	 * that is a failure rather than a normal completion.
	 */
	target_start = (handoff_entry_t)(uintptr_t)r->stage_target_high_va;
	r->jumped = 1;
	target_start(args);

	/* Unreachable through the target's own body: the call returned without reaching it. */
	stage90_stop_pc_sampling_watchdog();
	xnu_log_puts("stage90_xnu_handoff: *** HIGH-VA TARGET RETURNED UNEXPECTEDLY ***\n");
	xnu_log_kv32("sample_target_entered", stage90_handoff_sample_target_entered);
	r->xnu_returned = 1;
	r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_XNU_RETURNED;
#else
	/*
	 * PREFLIGHT_WATCHDOG_ONLY: do not install the candidate L1 and do not branch
	 * through the high-VA alias. Everything - MMIO, logging, ram_console - stays
	 * under the original known-good mapping, and the only thing under test is
	 * whether the timer interrupt can preempt a tight loop and drive the
	 * sample-dump -> platform_reboot()/PS_HOLD warm-reboot path to completion.
	 */
	xnu_log_puts("stage90_xnu_handoff: PREFLIGHT_WATCHDOG_ONLY active - skipping candidate L1 install\n");

	__asm__ volatile ("mrc p15, 0, %0, c12, c0, 0" : "=r"(vbar));
	__asm__ volatile ("mov %0, sp" : "=r"(sp));
	__asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));
	xnu_log_kv32("ttbr0_preflight_watchdog", r->handoff_ttbr0);
	xnu_log_kv32("vbar_preflight_watchdog", vbar);
	xnu_log_kv32("sp_preflight_watchdog", sp);
	xnu_log_kv32("cpsr_preflight_watchdog", cpsr);

	/* Ready for the preflight; nothing is jumped to. */
	r->ready = 1;
	r->satisfied_mask |= STAGE90_XNU_HANDOFF_SAT_READY;

	xnu_log_puts("stage90_xnu_handoff: arming PC-sampling watchdog for the identity-mapped preflight\n");
	if (!stage90_arm_pc_sampling_watchdog(STAGE90_HANDOFF_SAMPLE_INTERVAL_US,
	                                      STAGE90_HANDOFF_SAMPLE_MAX)) {
		xnu_log_puts("stage90_xnu_handoff: preflight watchdog arm failed\n");
		r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_WATCHDOG;
		goto finish;
	}
	r->preflight_watchdog_armed = 1u;

	xnu_log_puts("stage90_xnu_handoff: *** ENTERING IDENTITY-MAPPED PREFLIGHT LOOP (NO L1, NO HIGH-VA, NO JUMP) ***\n");
	stage90_handoff_preflight_watchdog_loop(args);

	/*
	 * Reaching here means the sampling watchdog did not interrupt and reboot the
	 * device inside the bounded loop. Report that as a failure and let the caller
	 * take its normal visible reboot path, instead of spinning forever.
	 */
	r->preflight_loop_entered = stage90_handoff_preflight_loop_entered;
	r->preflight_loop_ticks = stage90_handoff_preflight_loop_ticks;
	xnu_log_puts("stage90_xnu_handoff: preflight loop returned without a watchdog reboot\n");
	xnu_log_kv32("preflight_loop_entered", stage90_handoff_preflight_loop_entered);
	xnu_log_kv32("preflight_loop_args", stage90_handoff_preflight_loop_args);
	xnu_log_kv32("preflight_loop_ticks", stage90_handoff_preflight_loop_ticks);
	xnu_log_kv32("preflight_irq_count", stage90_irq_count);
	xnu_log_kv32("preflight_timer_irq_count", stage90_timer_irq_count);
	xnu_log_kv32("preflight_watchdog_fired", stage90_irq_sample_watchdog_fired);
	r->failure_mask |= STAGE90_XNU_HANDOFF_FAIL_WATCHDOG;
#endif

finish:
	/* Stop and dump any samples if control returned before watchdog reboot. */
	stage90_stop_pc_sampling_watchdog();
	stage90_dump_pc_samples();
	r->jumped_observed = r->jumped;
	r->xnu_returned_observed = r->xnu_returned;
	r->pc_sample_count = stage90_irq_sample_count;
	r->pc_sample_last_pc = stage90_irq_last_sampled_pc;
	r->watchdog_fired = stage90_irq_sample_watchdog_fired;

	/*
	 * The stop above also disarmed the recovery net that the handoff re-used.
	 * Re-arm it so the remaining payload work (returning up through the loader
	 * preflight to platform_reboot()) is covered again. The handoff's own, much
	 * shorter budget was for the no-return jump; a plain return does not need a
	 * fast trigger.
	 */
	(void)stage90_arm_deadman_reset();

	if (candidate_l1_installed != 0u) {
		handoff_dsb_isb();
		handoff_write_ttbr0(r->original_ttbr0);
		handoff_invalidate_tlbs();
		handoff_dsb_isb();
		xnu_log_kv32("stage90_xnu_handoff_restored_ttbr0", handoff_read_ttbr0());
	}

	/* Compute final status */
	if (r->satisfied_mask == r->required_mask && r->failure_mask == 0u) {
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
