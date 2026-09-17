/*
 * A bounded probe: can MSM8974 deliver an FIQ to non-secure PL1 at all?
 *
 * Why this is a question worth a hardware run. XNU's `locore.s:147` chooses its timer vector at
 * assembly time:
 *
 *     adr pc, Lexc_irq_vector
 * #if __ARM_TIME__
 *     adr pc, Lexc_decirq_vector
 * #else
 *     mov pc, r9                  // -> the shim's tbd_fiq_handler
 * #endif
 *
 * `__ARM_TIME__` is used in sixteen places in the ARM tree and defined in none of them, so the
 * `#else` branch is live and **XNU's timer arrives on FIQ on this build**. `pe_arm_init_timer`
 * supplies `fleh_fiq_generic` for it, and the shim deliberately leaves `tbd_fiq_handler` NULL
 * (spec section 2.3) because the payload has only ever driven IRQ.
 *
 * But the spec records, from the vendor's own header, that FIQ on this family *"requires secure
 * mode"* (`msm_watchdog.h:28`), and the cancro device tree does not enable kernel FIQ. If that is
 * right then no shim can supply a working FIQ path, and the resolution is the one spec section 6.2.1
 * already names: define `__ARM_TIME__` and move the timer to the tree's complete IRQ path.
 *
 * **That is a Phase 3 design decision resting on a claim that has never been measured on this
 * device.** This probe measures it, bounded, with the watchdog armed, and it is the smallest
 * experiment that can settle it: configure the GIC for a Group 0 interrupt on the measured CNTP PPI,
 * unmask CPSR.F, arm the timer, and see whether the FIQ vector is taken.
 *
 * Two outcomes, and the log says which:
 *
 *   * **the FIQ vector runs** - `vector_fiq` (`vectors.S:93`) logs `exception: fiq lr=... spsr=...`
 *     and then `platform_reboot`s, so the evidence is one line in `last_kmsg` and the device returns
 *     on its own. FIQ is available and the shim should supply a handler.
 *   * **it does not** - the spin completes, the probe masks FIQ again, disarms the timer, reports,
 *     and the run continues normally. Then the question is *why*, and the recorded GIC state is
 *     the answer: in a non-secure view `GICD_IGROUPR` is read-only and reads all ones, so a write
 *     that does not stick says the distributor is not configurable for Group 0 from here.
 *
 * The pre-state is logged BEFORE anything is unmasked, precisely because the first outcome reboots
 * before the report can be printed.
 *
 * Reachable only with `-DSTAGE90_XNU_MSM8974_FIQ_PROBE=1`; default off, and the build config that
 * the gate reads records it.
 */

#include "stage90.h"

#if STAGE90_XNU_MSM8974_FIQ_PROBE

/* --- the same hardware facts the shim uses, with their sources ----------------------------- */

#define FIQ_GIC_DIST_BASE   0xf9000000u     /* stage90_main.c: gic_reg[0] */
#define FIQ_GIC_CPU_BASE    0xf9002000u     /* stage90_main.c: gic_reg[2] */
#define FIQ_GICD_CTLR       0x000u
#define FIQ_GICD_IGROUPR0   0x080u          /* intid 0-31: the PPIs live here */
#define FIQ_GICC_CTLR       0x000u
#define FIQ_GICC_IAR        0x00cu
#define FIQ_GICC_EOIR       0x010u

/*
 * intr 19, measured - not the architectural 30. The payload enabled PPIs 18/19, armed CNTP, and
 * received IAR 0x13 with CNTP_CTL = 0x5 (experiment-12). The shim uses the same number for its EOI
 * pairing, and this probe uses it so that the interrupt under test is one already known to arrive
 * as IRQ.
 */
#define FIQ_TIMER_CNTP_INTID 19u

#define FIQ_CNTP_CTL_ENABLE  (1u << 0)
#define FIQ_CNTP_CTL_IMASK   (1u << 1)
#define FIQ_CNTP_CTL_ISTATUS (1u << 2)   /* reads 1 while the timer condition is met */

#define FIQ_CPSR_F           (1u << 6)

/* --- helpers (duplicated from the shim rather than shared: three lines each, and a shared header
 *     between two independently-gated probes would couple them for no gain) ------------------- */

static inline uint32_t fiq_mmio_read32(uint32_t addr)
{
	return *(volatile uint32_t *)(uintptr_t)addr;
}

static inline void fiq_mmio_write32(uint32_t addr, uint32_t value)
{
	*(volatile uint32_t *)(uintptr_t)addr = value;
}

static inline void fiq_barrier(void)
{
	__asm__ volatile ("dsb sy\n\tisb" ::: "memory");
}

static inline void fiq_write_cntp_tval(uint32_t v)
{
	__asm__ volatile ("mcr p15, 0, %0, c14, c2, 0" :: "r"(v));
}

static inline void fiq_write_cntp_ctl(uint32_t v)
{
	__asm__ volatile ("mcr p15, 0, %0, c14, c2, 1" :: "r"(v));
}

static inline uint32_t fiq_read_cntp_ctl(void)
{
	uint32_t v;
	__asm__ volatile ("mrc p15, 0, %0, c14, c2, 1" : "=r"(v));
	return v;
}

static inline uint32_t fiq_read_cpsr(void)
{
	uint32_t v;
	__asm__ volatile ("mrs %0, cpsr" : "=r"(v));
	return v;
}

/* Set and clear the FIQ mask. Named rather than repeated, because the second one is the
 * safety-relevant half: every exit path from the spin has to mask FIQ again. */
static inline void fiq_unmask(void) { __asm__ volatile ("cpsie f" ::: "memory"); }
static inline void fiq_mask(void)   { __asm__ volatile ("cpsid f" ::: "memory"); }

static uint32_t fiq_checksum(const struct stage90_xnu_fiq_probe_result *r)
{
	const uint32_t *p = (const uint32_t *)r;
	uint32_t count = (uint32_t)(offsetof(struct stage90_xnu_fiq_probe_result, checksum)
	                            / sizeof(uint32_t));
	uint32_t sum = 0;
	for (uint32_t i = 0; i < count; i++) {
		sum = (sum << 1) | (sum >> 31);
		sum ^= p[i];
	}
	return sum;
}

static struct stage90_xnu_fiq_probe_result g_fiq = { 0 };

/* --- the probe ------------------------------------------------------------------------------ */

int stage90_xnu_fiq_probe_run(void)
{
	struct stage90_xnu_fiq_probe_result *r = &g_fiq;
	uint32_t bit = 1u << FIQ_TIMER_CNTP_INTID;
	uint32_t group_before, group_after;
	uint32_t checks = 0u, failures = 0u;
	volatile uint32_t spin = 0;

	memset(r, 0, sizeof(*r));
	r->version = STAGE90_XNU_FIQ_PROBE_VERSION;
	r->size = sizeof(*r);
	r->intid = FIQ_TIMER_CNTP_INTID;
	r->intid_bit = bit;

	xnu_log_puts("stage90 xnu_fiq_probe: begin - can non-secure PL1 take an FIQ on MSM8974?\n");
	xnu_log_puts("stage90 xnu_fiq_probe: XNU's timer arrives on FIQ on this build (locore.s:147,\n");
	xnu_log_puts("stage90 xnu_fiq_probe: __ARM_TIME__ undefined), so this decides whether a shim\n");
	xnu_log_puts("stage90 xnu_fiq_probe: can supply that path at all.\n");

	/* --- pre-state, logged before anything is unmasked, because outcome 1 reboots ---------- */
	r->gicd_ctlr = fiq_mmio_read32(FIQ_GIC_DIST_BASE + FIQ_GICD_CTLR);
	r->gicc_ctlr = fiq_mmio_read32(FIQ_GIC_CPU_BASE + FIQ_GICC_CTLR);
	group_before = fiq_mmio_read32(FIQ_GIC_DIST_BASE + FIQ_GICD_IGROUPR0);
	r->igroupr0_before = group_before;
	r->cpsr_before = fiq_read_cpsr();
	r->f_bit_set_before = (r->cpsr_before & FIQ_CPSR_F) ? 1u : 0u;

	xnu_log_kv32("fiq_gicd_ctlr", r->gicd_ctlr);
	xnu_log_kv32("fiq_gicc_ctlr", r->gicc_ctlr);
	xnu_log_kv32("fiq_igroupr0_before", r->igroupr0_before);
	xnu_log_kv32("fiq_cpsr_before", r->cpsr_before);
	xnu_log_kv32("fiq_intid", r->intid);

	/*
	 * Group 0 is the FIQ-signalling group in GICv2 when the CPU interface is in a secure state.
	 * Clearing this bit is the configuration; whether it STICKS is the measurement, because a
	 * non-secure view reports `GICD_IGROUPR` as read-only, all ones.
	 */
	fiq_mmio_write32(FIQ_GIC_DIST_BASE + FIQ_GICD_IGROUPR0, group_before & ~bit);
	fiq_barrier();
	group_after = fiq_mmio_read32(FIQ_GIC_DIST_BASE + FIQ_GICD_IGROUPR0);
	r->igroupr0_after_write = group_after;
	r->group_write_stuck = ((group_after & bit) == 0u) ? 1u : 0u;
	r->igroupr0_all_group1 = (group_after == 0xffffffffu) ? 1u : 0u;
	xnu_log_kv32("fiq_igroupr0_after_write", group_after);
	xnu_log_kv32("fiq_group_write_stuck", r->group_write_stuck);
	xnu_log_kv32("fiq_igroupr0_all_group1", r->igroupr0_all_group1);

	/* Enable the group the interrupt is in, then the timer, then FIQ delivery. Order matters and
	 * is the payload's validated one (distributor -> CPU interface -> source -> delivery). */
	r->gicc_ctlr_after = r->gicc_ctlr | 1u;
	fiq_mmio_write32(FIQ_GIC_CPU_BASE + FIQ_GICC_CTLR, r->gicc_ctlr_after);

	/* ~4 ms at 19.2 MHz, the same order the payload's timer code uses. */
	r->cntp_tval_written = FIQ_TIMER_CNTP_INTID * 0u + 76800u;
	fiq_write_cntp_tval(r->cntp_tval_written);
	fiq_write_cntp_ctl(FIQ_CNTP_CTL_ENABLE);
	fiq_barrier();
	r->cntp_ctl_after_arm = fiq_read_cntp_ctl();
	r->timer_armed = 1u;
	xnu_log_kv32("fiq_cntp_tval_written", r->cntp_tval_written);
	xnu_log_kv32("fiq_cntp_ctl_after_arm", r->cntp_ctl_after_arm);

	/*
	 * The point of the whole file. If a FIQ is delivered, `vector_fiq` logs and reboots and none
	 * of what follows runs; the two `fiq_*_before` lines above are the evidence that goes with it.
	 */
	fiq_unmask();
	r->cpsr_after_unmask = fiq_read_cpsr();
	r->f_bit_cleared = ((r->cpsr_after_unmask & FIQ_CPSR_F) == 0u) ? 1u : 0u;
	xnu_log_kv32("fiq_cpsr_after_unmask", r->cpsr_after_unmask);
	xnu_log_kv32("fiq_f_bit_cleared", r->f_bit_cleared);

	/*
	 * The wait, and it is bounded by the TIMER rather than by a loop count.
	 *
	 * The first version of this probe spun a fixed 2,000,000 iterations and reported "no FIQ" -
	 * which was not yet a result: 2M iterations of an empty loop is about 2.2 ms at 2.26 GHz while
	 * the timer had been armed for 4 ms, so **the measurement could end before the interrupt was
	 * due**. That is the project's "a measurement can be the thing that is wrong" defect, and it
	 * was caught by doing the arithmetic before writing the result down.
	 *
	 * `CNTP_CTL` bit 2 is ISTATUS: it reads 1 while the timer condition is met. Spinning until it
	 * is set means the wait cannot be too short, whatever the core's clock is; the iteration cap is
	 * only a backstop, and `timer_fired` records which one ended the loop.
	 */
	for (spin = 0; spin < STAGE90_XNU_FIQ_PROBE_SPIN; spin++) {
		if ((fiq_read_cntp_ctl() & FIQ_CNTP_CTL_ISTATUS) != 0u) {
			r->timer_fired = 1u;
			break;
		}
	}
	r->spin_iterations = spin;

	/*
	 * Mask FIQ **and IRQ together** for the readback, then re-enable IRQ.
	 *
	 * The first version did `cpsid f` and then read CPSR, and reported F as still clear - because
	 * it was: the payload's own IRQ handler runs throughout, and its return path is
	 * `subs pc, lr, #0`, which restores the full CPSR including F from SPSR_irq. So every IRQ
	 * arriving between the mask and the read puts F back the way it was. Masking IRQ across the
	 * two instructions is what makes the reading mean what it says.
	 */
	__asm__ volatile ("cpsid if\n\t"
	                  "mrs %0, cpsr\n\t"
	                  "cpsie i"
	                  : "=r"(r->cpsr_after_mask) :: "memory");
	r->f_bit_masked = ((r->cpsr_after_mask & FIQ_CPSR_F) != 0u) ? 1u : 0u;
	r->cpsr_final = fiq_read_cpsr();
	r->f_bit_clear_final = ((r->cpsr_final & FIQ_CPSR_F) == 0u) ? 1u : 0u;

	fiq_write_cntp_ctl(FIQ_CNTP_CTL_IMASK);
	fiq_write_cntp_tval(0u);
	fiq_barrier();
	r->cntp_ctl_after_disarm = fiq_read_cntp_ctl();
	r->fiq_delivered = 0u;   /* reaching here IS the result: the vector did not run */
	r->timer_disarmed = ((r->cntp_ctl_after_disarm & FIQ_CNTP_CTL_ENABLE) == 0u) ? 1u : 0u;

	xnu_log_kv32("fiq_timer_fired", r->timer_fired);
	xnu_log_kv32("fiq_cpsr_after_mask", r->cpsr_after_mask);
	xnu_log_kv32("fiq_f_bit_masked", r->f_bit_masked);
	xnu_log_kv32("fiq_cpsr_final", r->cpsr_final);
	xnu_log_kv32("fiq_f_bit_clear_final", r->f_bit_clear_final);

	checks++;
	if (!r->f_bit_cleared) failures |= STAGE90_XNU_FIQ_PROBE_FAIL_FIQ_NEVER_UNMASKED;
	checks++;
	if (!r->timer_disarmed) failures |= STAGE90_XNU_FIQ_PROBE_FAIL_TIMER_LEFT_ARMED;
	/*
	 * `timer_fired` is the check that makes the negative a result: if the timer never reached its
	 * condition, the wait was too short and "no FIQ" says nothing about FIQ.
	 */
	checks++;
	if (!r->timer_fired) failures |= STAGE90_XNU_FIQ_PROBE_FAIL_WAIT_TOO_SHORT;

	r->checks = checks;
	r->failures = failures;
	r->status = failures ? STAGE90_STATUS_FAIL(failures) : STAGE90_STATUS_OK;
	r->checksum = fiq_checksum(r);

	stage90_xnu_fiq_probe_log(r);
	return (failures != 0u) ? -1 : 0;
}

void stage90_xnu_fiq_probe_log(const struct stage90_xnu_fiq_probe_result *r)
{
	xnu_log_puts("stage90 xnu_fiq_probe result:\n");
	xnu_log_kv32("fiq_probe_status", r->status);
	xnu_log_kv32("fiq_probe_fiq_delivered", r->fiq_delivered);
	xnu_log_kv32("fiq_probe_group_write_stuck", r->group_write_stuck);
	xnu_log_kv32("fiq_probe_igroupr0_all_group1", r->igroupr0_all_group1);
	xnu_log_kv32("fiq_probe_spin_iterations", r->spin_iterations);
	xnu_log_kv32("fiq_probe_cpsr_final", r->cpsr_final);
	xnu_log_kv32("fiq_probe_cntp_ctl_after_disarm", r->cntp_ctl_after_disarm);
	xnu_log_kv32("fiq_probe_checks", r->checks);
	xnu_log_kv32("fiq_probe_failures", r->failures);
	xnu_log_kv32("fiq_probe_checksum", r->checksum);

	if (r->fiq_delivered) {
		xnu_log_puts("stage90 xnu_fiq_probe: FIQ DELIVERED\n");
	} else if (r->igroupr0_all_group1) {
		xnu_log_puts("stage90 xnu_fiq_probe: no FIQ, and GICD_IGROUPR reads all ones - the\n");
		xnu_log_puts("stage90 xnu_fiq_probe: distributor is in a non-secure view, so Group 0\n");
		xnu_log_puts("stage90 xnu_fiq_probe: cannot be configured from here. This matches the\n");
		xnu_log_puts("stage90 xnu_fiq_probe: vendor header's \"secure mode required for FIQ\".\n");
	} else {
		xnu_log_puts("stage90 xnu_fiq_probe: no FIQ, and the Group 0 routing DID stick - so the\n");
		xnu_log_puts("stage90 xnu_fiq_probe: GIC is configurable but the interrupt did not arrive on\n");
		xnu_log_puts("stage90 xnu_fiq_probe: FIQ. See the state above before concluding.\n");
	}
}

const struct stage90_xnu_fiq_probe_result *stage90_xnu_fiq_probe_result(void)
{
	return &g_fiq;
}

#endif /* STAGE90_XNU_MSM8974_FIQ_PROBE */
