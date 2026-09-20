/*
 * 482: which interrupt line the virtual timer asserts, measured from inside the kernel with delivery
 * still impossible.
 *
 * ------------------------------------------------------------------------------------------------
 * What the frontier is, and why this is a measurement rather than a configuration change
 * ------------------------------------------------------------------------------------------------
 *
 * 481 gave the kernel a working decrementer and deliberately masked its interrupt, because the
 * question this step answers could not be asked safely until the countdown itself was known to run.
 * 143 settled the other half of the routing question the hard way: **MSM8974 delivers no FIQ to
 * non-secure PL1**, measured with Group 0 accepted at both ends and the timer measurably reaching
 * `ISTATUS` - so Apple's own decrementer path, which this image's vector page already points slot 7
 * at, is a path nothing will ever arrive on. (Slot 7 holds `fleh_decirq`, and the *symbol* is this
 * image's: `entry_stubs.c:6066`, one line of `entry_epilogue`. Apple's `locore_fleh_decirq` is in
 * the image too, at 0x80015b8c, and in no vector slot.) The remaining route is the general IRQ:
 * GIC Group 1, slot 6, Apple's `fleh_irq`.
 *
 * **And slot 6 is not Apple's handler either.** It holds this image's `fleh_irq`
 * (`entry_stubs.c:6055`), which sets `g_irq_report_pending` and calls
 * `entry_epilogue("exception: irq")` - a report and a stop, which is exactly what 308's run ended
 * on. Apple's handler is in the image at 0x80015954 as `locore_fleh_irq`, reachable from nothing,
 * and its body is where 483's problem lives:
 *
 *     80015abc:  ldr r0, [r4, #192]      ; INTERRUPT_TARGET
 *     80015ac0:  ldr r1, [r4, #196]      ; INTERRUPT_REFCON
 *     80015ac4:  ldr r2, [r4, #184]      ; INTERRUPT_NUB
 *     80015ac8:  ldr r3, [r4, #188]      ; INTERRUPT_SOURCE
 *     80015acc:  ldr r5, [r4, #180]      ; INTERRUPT_HANDLER
 *     80015ad0:  blx r5                  ; -> whatever ml_install_interrupt_handler last stored
 *
 * `r4` is `cpu_data`, the five offsets are `assym.s`'s own `INTERRUPT_*`, and on this image nothing
 * has ever stored a handler - so the moment 483 points slot 6 at Apple's handler, `r5` is 0 and
 * `blx r5` branches to address 0. **Two facts, both checkable and both checked**
 * (`tools/check_gic_routing.py`): today's slot 6 is the reporting handler, and Apple's handler
 * loads its callee from an offset nothing has filled. Neither is a reason to guess which line the
 * timer is on - which is what this step measures - and together they are the reason the routing has
 * to be decided before anything is enabled.
 *
 * So 482 does not enable anything. It answers the one question 483 cannot be written without - *which
 * INTID does the line this kernel programs appear as* - and it answers it with the delivery path
 * switched off at two independent places, so that the answer costs nothing if it is wrong.
 *
 * ------------------------------------------------------------------------------------------------
 * The measurement, and the three things that make it a reading rather than a poke
 * ------------------------------------------------------------------------------------------------
 *
 * The device tree's timer node is `interrupts = <1 2 0 1 3 0>` and the GIC's xlate maps PPI n to
 * INTID n + 16 (`stages/stage90/gic.c`, whose two constants are the payload's own and are compared
 * against this file's by `tools/check_gic_routing.py`). That gives exactly two candidates, 18 and 19;
 * 143 measured 19 to be the one **`CNTP`** asserts, and 481 drives **`CNTV`**. So the question is
 * whether the virtual timer is the sibling the device tree lists first, and the probe is built so
 * that "neither" is an answer it can produce:
 *
 *   - for each candidate INTID, clear its pending bit, enable it at the distributor, and read
 *     `GICD_ISENABLER0` back (so the enable is a reading and not a write);
 *   - **with `CNTV_CTL` masked**, arm a countdown of `STAGE90_GIC_PROBE_TICKS`, spin the same
 *     distance, and read `GICD_ISPENDR0`. This is the negative control: a bit that rises here is not
 *     this timer's;
 *   - **with `CNTV_CTL` unmasked** (IMASK clear, ENABLE set), arm the same countdown, spin the same
 *     distance, and read `GICD_ISPENDR0` again **and** `CNTV_CTL`. The second read is what makes the
 *     first one mean anything: if the countdown did not reach zero (`ISTATUS`) the spin was too
 *     short and "no pending bit" is a fact about the loop rather than about the line;
 *   - re-mask, restart the countdown far into the future, disable the INTID, clear its pending, and
 *     read all of it back.
 *
 * A candidate is the answer when `ISTATUS` rose, the masked read did not show its bit, and the
 * unmasked read did. All three, per candidate, and every one of the nine numbers is in the log.
 *
 * ------------------------------------------------------------------------------------------------
 * Why nothing can be delivered while it runs, in three independent layers
 * ------------------------------------------------------------------------------------------------
 *
 *   - **The CPU interface is disabled.** `GICC_CTLR` is read, then written with *both* group enable
 *     bits clear, and `GICC_PMR` is read and written to 0; both are read back and recorded before
 *     the first countdown is armed. An interrupt that is pending at a distributor whose CPU
 *     interface is not enabled is not signalled to the processor at all - so this layer does not
 *     depend on which bit of `GICC_CTLR` this device's security view calls what (143 measured that
 *     the view is the non-secure one, where bit 0 is `EnableGrp1`; clearing both makes the question
 *     moot, which is the only reason a probe may be written against a register nobody has read the
 *     manual for).
 *   - **The exception is masked.** The probe saves `CPSR` and executes `cpsid if` for its whole
 *     window, restoring the two bits it found. It does not assume its caller disabled anything: the
 *     name of the function it is called from (`setPop`) says `splclock` is held, and a measurement
 *     whose safety depends on a *caller's* claim is a measurement that is one refactor away from
 *     being unsafe. The CPSR it found is recorded, so whether the caller's claim holds is a reading
 *     too.
 *   - **The candidates are disabled whenever the countdown is not the thing under test.** Outside the
 *     two spin windows both INTIDs are disabled at the distributor with their pending bits cleared,
 *     and the restore is ordered: **both are disabled and cleared first, and only then are `PMR` and
 *     `CTLR` put back.** The other order would leave a pending group-1 interrupt and a live CPU
 *     interface for one instruction, which is exactly enough to arrive.
 *
 * The residual risk is stated rather than implied: if all three layers failed, an interrupt would be
 * taken by slot 6 - which today is this image's reporting handler, so the run would end on
 * `exception: irq` with the GIC's own numbers in the report rather than continuing. That is a *stop*,
 * not a crash, and it is the stop this project has recovered from unattended every time: the ARM's
 * exception is masked until the epilogue, and the SoC watchdog - armed by the payload before the
 * jump, biting at 28 s - is what brings the device back. That net is why this step can be run at
 * all, and the probe's own three guards are why it should not need it.
 *
 * ------------------------------------------------------------------------------------------------
 * The one thing that had to be added to read anything at all
 * ------------------------------------------------------------------------------------------------
 *
 * **XNU's page tables do not map the GIC.** 308's run measured it and `entry_stubs.c`'s own header
 * records it: `fleh_irq` runs with XNU's tables live, which map `[physBase, physBase + memSize)` and
 * nothing at `0xf9002000` - which is why 309's epilogue has to read `GICC_IAR` with the MMU off. So
 * the first act of this probe is `entry_mmio_section(0xf9000000, 0xf9000000, ...)`: one 1 MB section
 * descriptor into XNU's own L1, with the live channel's own attribute (0xc on this device, the
 * strongly-ordered encoding read out of `PRRR`), through the same recipe the console's mapping uses.
 * The mapping and the descriptor are both recorded, and the probe returns without touching a single
 * GIC register if the slot was occupied - because a slot this instrument may not clobber is a slot
 * whose contents it cannot vouch for.
 *
 * ------------------------------------------------------------------------------------------------
 * 484: the table this section goes into is read *at the install*, and the probe says which it was
 * ------------------------------------------------------------------------------------------------
 *
 * 482's mapper wrote its descriptor into the table the console had latched at its first live write.
 * For the console that latch is sound - its sections are installed before `arm_vm_init` copies the
 * boot table into the system table four pages higher (`arm_vm_init.c:370-380`), so the copy carries
 * them across - but for this probe, which runs *after* that copy, it is a table the MMU has stopped
 * walking: the write lands, the read-back of the slot agrees, and the next load from
 * `0xf9000000` is a translation fault. 484's first run is exactly that fault - `sleh_abort` at
 * interrupt context, `pc = gicd_read+4`, `far = 0xf9000000`, with `xnu_live_l1` reading
 * `0x80700000` where 483 read `0x80704000` - and it was triggered by this step's own instrument,
 * whose first live write (`timer_call_setup`, earlier in the boot than 483's `ml_init_timebase`)
 * pulled the console's latch back onto the boot table. The mapper now re-reads `TTBR0`/`TTBR1` for
 * every install; the four numbers it used are published here, and `_l1_moved` is the reading that
 * says the latch and the live table are not the same one.
 */
#include <stdint.h>

#include "entry_gic.h"
#include "entry_timebase.h"

/*
 * The live channel and the mapper, both defined in `entry_stubs.c`. The probe is compiled in every
 * build; the records are not, which is the same split `entry_timebase.c` makes and for the same
 * reason - the measurement is the step and the records are the instrument.
 */
#ifndef STAGE90_ENTRY_GIC_TRACED
#define STAGE90_ENTRY_GIC_TRACED 0
#endif

#if STAGE90_ENTRY_GIC_TRACED
extern void entry_live_write(const char *key, uint32_t value);
extern uint32_t g_live_state;
/* 484: the table `entry_mmio_section` read at the install, and whether it was the console's latch. */
extern uint32_t g_live_mmio_l1;
extern uint32_t g_live_mmio_l1_moved;
extern uint32_t g_live_mmio_ttbr0;
extern uint32_t g_live_mmio_ttbr1;
#define GIC_LIVE(key, value) entry_live_write((key), (uint32_t)(value))
#else
#define GIC_LIVE(key, value) do { (void)(key); (void)(value); } while (0)
#endif

extern uint32_t entry_mmio_section(uint32_t va, uint32_t pa, uint32_t *slot_before_out,
                                   uint32_t *desc_out);

/* --------------------------------------------------------------------------------------------- */
/* The two register windows                                                                       */
/* --------------------------------------------------------------------------------------------- */

/*
 * **483 moved the four accesses into `entry_irq.c`**, which is the object that also reads `GICC_IAR`
 * and writes `GICC_EOIR`: one definition of "how this image talks to the GIC", used by the probe here
 * and by the handler there, and compared against the payload's own two sources by
 * `tools/check_gic_routing.py` for the numbers rather than for the accessors. The `volatile` and the
 * `dsb sy` after every write are still the property that matters and still stated once, at the
 * definition: a peripheral register write that is still in the write buffer when the next instruction
 * reads the same block is a read of the old value.
 */

/* --------------------------------------------------------------------------------------------- */
/* The exception mask, and the two bits it is made of                                             */
/* --------------------------------------------------------------------------------------------- */

#define GIC_CPSR_I  0x00000080u
#define GIC_CPSR_F  0x00000040u
#define GIC_CPSR_IF (GIC_CPSR_I | GIC_CPSR_F)

/*
 * `mrs`/`msr cpsr_c` rather than `ml_set_interrupts_enabled`, for two reasons: the kernel's own call
 * would set `cpu_data->interrupts_enabled` as a side effect of a probe that must not change the
 * state it is measuring, and the flag it would set is a *claim* the probe would then be relying on.
 * The restore writes only the two bits it masked, and takes the mode bits from a fresh read, so a
 * mode this function did not expect cannot be restored into existence.
 */
static uint32_t gic_irq_mask(void)
{
    uint32_t cpsr;

    __asm__ volatile ("mrs %0, cpsr" : "=r" (cpsr));
    __asm__ volatile ("cpsid if" ::: "memory");
    return cpsr;
}

static void gic_irq_restore(uint32_t saved)
{
    uint32_t now;

    __asm__ volatile ("mrs %0, cpsr" : "=r" (now));
    now = (now & ~GIC_CPSR_IF) | (saved & GIC_CPSR_IF);
    __asm__ volatile ("msr cpsr_c, %0" :: "r" (now) : "memory");
}

/*
 * A bounded spin of empty inline assembly, the same *shape* 481 measured this machine with. Not the
 * same length per iteration, and the difference is measured rather than assumed: 481's counter is a
 * register (`uint32_t spin`, non-volatile) and this one is `volatile`, so an iteration here is a
 * load-add-store plus the branch instead of a register decrement - ≈100 ticks per 1000 iterations
 * against 481's ≈41 (this probe's `xnu_live_gic_ticks_elapsed = 0x00027282` over four windows of
 * 400000 iterations, and 481's 813 ticks over 20000). The volatile is not removable: the body is
 * empty and *that* is the point (the work being measured is time, not instructions), so the counter
 * is what keeps the loop honest when the body cannot. The margin that matters is the one the probe
 * measures, not this one: `ISTATUS` set in both windows' post-spin `CNTV_CTL`, and the elapsed
 * `CNTVCT` in the log beside the result.
 *
 * **The sentence that used to end this comment was wrong**: it said "the same shape the new check
 * requires the *two* of them to keep" - a claim that a check exists, which it did not, and which
 * would have been false about the lengths even if it had. What the check requires instead is that
 * `entry_gic_probe` brackets its candidates with `stage90_cntvct_read`, so that the margin is a
 * reading in the log rather than an argument in a comment.
 */
static void gic_spin(uint32_t iterations)
{
    volatile uint32_t i;

    for (i = 0u; i < iterations; i++)
        __asm__ volatile ("");
}

/* --------------------------------------------------------------------------------------------- */
/* The probe                                                                                      */
/* --------------------------------------------------------------------------------------------- */

static uint32_t g_gic_probed;

/* The countdown restarted after every window: long enough that it cannot reach zero before the
 * caller overwrites it one instruction later, short enough to stay a plausible 32-bit value. */
#define GIC_TVAL_PARKED 0x7FFFFFFFu

/*
 * One candidate: the masked / unmasked sequence above, with every number it reads returned to the
 * caller through the live channel. Returns the bits that became pending **while this candidate's
 * countdown was the only variable** - and clears them, so the next candidate's masked control starts
 * from the state this probe found.
 *
 * **Two corrections the first run's own numbers forced, both of them visible in its log.** (1) The
 * masked `CNTV_CTL` read was taken *before* the spin and the unmasked one *after* it, so the pair a
 * reader would compare - `ctl_masked` against `ctl_unmasked` - was two different moments of the
 * countdown rather than two values of `IMASK`: the run reports `0x3` and `0x5`, which looks like the
 * mask clearing `ISTATUS` and is only the read happening earlier. Both are now read after their own
 * spin, so the pair differs in `IMASK` and nothing else. (2) The candidate cleared its *own* INTID
 * (`bit`) and not the line its countdown had raised (INTID 20, measured), so the bit stayed pending
 * into the second candidate - whose masked control therefore inherited it and whose `pend_masked`
 * was already dirty - and stayed pending into the distributor after the probe returned. It now clears
 * `added`: the bits that appeared while the only thing that changed was this countdown's `IMASK`.
 */
static uint32_t gic_probe_candidate(uint32_t intid, uint32_t pre_pending)
{
    const uint32_t bit = 1u << intid;
    uint32_t isenabler, pend_masked, pend_unmasked, ctl_masked, ctl_unmasked, pend_after, added;

    gicd_write(STAGE90_GICD_ICPENDR0, bit);
    gicd_write(STAGE90_GICD_ISENABLER0, bit);
    isenabler = gicd_read(STAGE90_GICD_ISENABLER0);

    /* The negative control: the same countdown, the same spin, the line masked. */
    stage90_cntv_tval_write(STAGE90_GIC_PROBE_TICKS);
    stage90_cntv_ctl_write(STAGE90_CNTV_ARM_MASK);
    gic_spin(STAGE90_GIC_PROBE_SPIN);
    ctl_masked = stage90_cntv_ctl_read();
    pend_masked = gicd_read(STAGE90_GICD_ISPENDR0);

    /* The measurement: IMASK clear, so the countdown's zero asserts the line. */
    stage90_cntv_tval_write(STAGE90_GIC_PROBE_TICKS);
    stage90_cntv_ctl_write(STAGE90_CNTV_CTL_ENABLE);
    gic_spin(STAGE90_GIC_PROBE_SPIN);
    ctl_unmasked = stage90_cntv_ctl_read();
    pend_unmasked = gicd_read(STAGE90_GICD_ISPENDR0);

    /* What this candidate raised: the bits that were not pending when it started. */
    added = pend_unmasked & ~pre_pending;

    /* Park it and take the line down before anything else is looked at. */
    stage90_cntv_ctl_write(STAGE90_CNTV_ARM_MASK);
    stage90_cntv_tval_write(GIC_TVAL_PARKED);
    gicd_write(STAGE90_GICD_ICENABLER0, bit | added);
    gicd_write(STAGE90_GICD_ICPENDR0, bit | added);
    pend_after = gicd_read(STAGE90_GICD_ISPENDR0);

    if (intid == STAGE90_GIC_TIMER_PPI0) {
        GIC_LIVE("xnu_live_gic_id18_isenabler", isenabler);
        GIC_LIVE("xnu_live_gic_id18_pend_masked", pend_masked);
        GIC_LIVE("xnu_live_gic_id18_ctl_masked", ctl_masked);
        GIC_LIVE("xnu_live_gic_id18_pend_unmasked", pend_unmasked);
        GIC_LIVE("xnu_live_gic_id18_ctl_unmasked", ctl_unmasked);
        GIC_LIVE("xnu_live_gic_id18_pend_added", added);
        GIC_LIVE("xnu_live_gic_id18_pend_after", pend_after);
    } else {
        GIC_LIVE("xnu_live_gic_id19_isenabler", isenabler);
        GIC_LIVE("xnu_live_gic_id19_pend_masked", pend_masked);
        GIC_LIVE("xnu_live_gic_id19_ctl_masked", ctl_masked);
        GIC_LIVE("xnu_live_gic_id19_pend_unmasked", pend_unmasked);
        GIC_LIVE("xnu_live_gic_id19_ctl_unmasked", ctl_unmasked);
        GIC_LIVE("xnu_live_gic_id19_pend_added", added);
        GIC_LIVE("xnu_live_gic_id19_pend_after", pend_after);
    }

    return added;
}

/*
 * The INTID of a mask with exactly one bit set, and `0xffffffff` when the mask is empty or names more
 * than one line. A sentinel rather than a guess: "the timer is on line N" is a reading only when
 * exactly one line appeared, and each of the two candidates contributes one - so agreement between
 * them is visible in the record rather than averaged away by it.
 */
static uint32_t gic_single_intid(uint32_t mask)
{
    uint32_t i;

    if (mask == 0u || (mask & (mask - 1u)) != 0u)
        return 0xffffffffu;
    for (i = 0u; i < 32u; i++)
        if ((mask & (1u << i)) != 0u)
            return i;
    return 0xffffffffu;
}

void entry_gic_probe(void)
{
    uint32_t cpsr, slot_before = 0u, desc = 0u, mapped;
    uint32_t dist_ctlr, dist_typer, isenabler0, ispendr0, igroupr0, ipriority_ppi, itargets_ppi;
    uint32_t cpu_ctlr, cpu_pmr, guard_ctlr, guard_pmr;
    uint32_t hits = 0u, added18, added19, added, timer_intid;
    uint32_t restore_ctlr, restore_pmr, restore_isenabler0, restore_ispendr0;
    uint64_t t_a, t_b;

    if (g_gic_probed != 0u)
        return;
    g_gic_probed = 1u;

#if STAGE90_ENTRY_GIC_TRACED
    GIC_LIVE("xnu_live_gic_live_state", g_live_state);
#else
    GIC_LIVE("xnu_live_gic_live_state", 0u);
#endif
    GIC_LIVE("xnu_live_gic_probe_ticks", STAGE90_GIC_PROBE_TICKS);
    GIC_LIVE("xnu_live_gic_probe_spin", STAGE90_GIC_PROBE_SPIN);

    /*
     * **The mapping, before anything is read.** A result of 0 means the slot at `0xf9000000 >> 20`
     * already held a descriptor, and this probe then reads no GIC register at all: the mapping it
     * would be reading through is not one it installed, and a read of an address whose translation
     * it cannot vouch for is a fault, not a measurement.
     */
    mapped = entry_mmio_section(STAGE90_GIC_DIST_BASE, STAGE90_GIC_DIST_BASE, &slot_before, &desc);
    GIC_LIVE("xnu_live_gic_map", mapped);
    GIC_LIVE("xnu_live_gic_slot_before", slot_before);
    GIC_LIVE("xnu_live_gic_desc", desc);
    /*
     * 484: **which table the section went into**, and whether that was the table the console latched.
     * The mapper re-reads `TTBR0`/`TTBR1` for every install, so these four numbers are a reading of
     * the live state and not a copy of `xnu_live_l1`: a `_l1_moved` of 1 means the console's latch
     * (`xnu_live_l1`) and the live table disagree, and before 484 the install would have gone into
     * the latch - a table the MMU no longer walks - and the first GIC read would have faulted.
     */
    GIC_LIVE("xnu_live_gic_l1", g_live_mmio_l1);
    GIC_LIVE("xnu_live_gic_l1_moved", g_live_mmio_l1_moved);
    GIC_LIVE("xnu_live_gic_ttbr0", g_live_mmio_ttbr0);
    GIC_LIVE("xnu_live_gic_ttbr1", g_live_mmio_ttbr1);
    if (mapped == 0u)
        return;

    cpsr = gic_irq_mask();
    GIC_LIVE("xnu_live_gic_cpsr", cpsr);

    dist_ctlr = gicd_read(STAGE90_GICD_CTLR);
    dist_typer = gicd_read(STAGE90_GICD_TYPER);
    isenabler0 = gicd_read(STAGE90_GICD_ISENABLER0);
    ispendr0 = gicd_read(STAGE90_GICD_ISPENDR0);
    igroupr0 = gicd_read(STAGE90_GICD_IGROUPR0);
    ipriority_ppi = gicd_read(STAGE90_GICD_IPRIORITY0 + 16u);
    itargets_ppi = gicd_read(STAGE90_GICD_ITARGETSR0 + 16u);
    cpu_ctlr = gicc_read(STAGE90_GICC_CTLR);
    cpu_pmr = gicc_read(STAGE90_GICC_PMR);

    GIC_LIVE("xnu_live_gic_dist_ctlr", dist_ctlr);
    GIC_LIVE("xnu_live_gic_dist_typer", dist_typer);
    GIC_LIVE("xnu_live_gic_isenabler0", isenabler0);
    GIC_LIVE("xnu_live_gic_ispendr0", ispendr0);
    GIC_LIVE("xnu_live_gic_igroupr0", igroupr0);
    GIC_LIVE("xnu_live_gic_ipriority_ppi", ipriority_ppi);
    GIC_LIVE("xnu_live_gic_itargets_ppi", itargets_ppi);
    GIC_LIVE("xnu_live_gic_cpu_ctlr", cpu_ctlr);
    GIC_LIVE("xnu_live_gic_cpu_pmr", cpu_pmr);

    /* Layer one: the CPU interface, off, and read back so the guard is a reading. */
    gicc_write(STAGE90_GICC_CTLR, cpu_ctlr & ~STAGE90_GICC_CTLR_ANY);
    gicc_write(STAGE90_GICC_PMR, 0u);
    guard_ctlr = gicc_read(STAGE90_GICC_CTLR);
    guard_pmr = gicc_read(STAGE90_GICC_PMR);
    GIC_LIVE("xnu_live_gic_guard_cpu_ctlr", guard_ctlr);
    GIC_LIVE("xnu_live_gic_guard_cpu_pmr", guard_pmr);

    t_a = stage90_cntvct_read();
    GIC_LIVE("xnu_live_gic_cntvct_a", (uint32_t)t_a);

    added18 = gic_probe_candidate(STAGE90_GIC_TIMER_PPI0, ispendr0);
    added19 = gic_probe_candidate(STAGE90_GIC_TIMER_PPI1, ispendr0);
    added = added18 | added19;
    if ((added18 & (1u << STAGE90_GIC_TIMER_PPI0)) != 0u)
        hits |= 1u;
    if ((added19 & (1u << STAGE90_GIC_TIMER_PPI1)) != 0u)
        hits |= 2u;
    timer_intid = gic_single_intid(added);

    t_b = stage90_cntvct_read();
    GIC_LIVE("xnu_live_gic_cntvct_b", (uint32_t)t_b);
    GIC_LIVE("xnu_live_gic_ticks_elapsed", (uint32_t)(t_b - t_a));
    GIC_LIVE("xnu_live_gic_pend_added18", added18);
    GIC_LIVE("xnu_live_gic_pend_added19", added19);
    GIC_LIVE("xnu_live_gic_pend_added", added);
    GIC_LIVE("xnu_live_gic_timer_intid", timer_intid);
    GIC_LIVE("xnu_live_gic_hits", hits);

    /*
     * The restore, in the order that matters: both candidate lines down and clear **before** the CPU
     * interface is armed again. The other order leaves a pending group-1 interrupt and a live
     * interface for one instruction.
     *
     * Each candidate has already cleared the bits it raised, so this is the second half of one
     * property: what the distributor holds pending after the probe is what it held before it, and the
     * run proves that by `restore_ispendr0` against the `ispendr0` read above. The first run of this
     * probe failed it - INTID 20 was left pending, because the candidate cleared its own INTID
     * instead of the line its countdown had raised.
     */
    gicd_write(STAGE90_GICD_ICENABLER0, (1u << STAGE90_GIC_TIMER_PPI0) | (1u << STAGE90_GIC_TIMER_PPI1));
    gicd_write(STAGE90_GICD_ICPENDR0, (1u << STAGE90_GIC_TIMER_PPI0) | (1u << STAGE90_GIC_TIMER_PPI1));
    restore_isenabler0 = gicd_read(STAGE90_GICD_ISENABLER0);
    restore_ispendr0 = gicd_read(STAGE90_GICD_ISPENDR0);

    gicc_write(STAGE90_GICC_PMR, cpu_pmr);
    gicc_write(STAGE90_GICC_CTLR, cpu_ctlr);
    restore_pmr = gicc_read(STAGE90_GICC_PMR);
    restore_ctlr = gicc_read(STAGE90_GICC_CTLR);

    GIC_LIVE("xnu_live_gic_restore_isenabler0", restore_isenabler0);
    GIC_LIVE("xnu_live_gic_restore_ispendr0", restore_ispendr0);
    GIC_LIVE("xnu_live_gic_restore_cpu_pmr", restore_pmr);
    GIC_LIVE("xnu_live_gic_restore_cpu_ctlr", restore_ctlr);

    gic_irq_restore(cpsr);
}
