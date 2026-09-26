/*
 * 483: the interrupt gets a handler, the handler gets a line, and the line gets enabled - last.
 *
 * ------------------------------------------------------------------------------------------------
 * What this step is, in one sentence
 * ------------------------------------------------------------------------------------------------
 *
 * 481 gave the kernel a decrementer and masked every write; 482 measured that the countdown asserts
 * **INTID 20** and that the two lines the payload's device tree names (18 and 19) are not it, and
 * also measured the shape of the dispatch that would run a handler: `fleh_irq_handler` at
 * `0x80015abc` loads five `struct cpu_data` words into r0..r3 and r5 and `blx`es the fifth, and this
 * image has never stored anything there - so pointing the vector page at Apple's dispatcher without
 * a handler first is a branch to address 0. This step supplies that word, points the slot, configures
 * the line, and only then lets a countdown become an interrupt.
 *
 * The order is the step. Every step below is recorded before the next one runs, and the enable is
 * last, because the failure this ordering exists to prevent is not a fault - it is a run in which
 * something is half-armed and the log cannot say which half.
 *
 * ------------------------------------------------------------------------------------------------
 * Where it runs, and why that is not inside the probe
 * ------------------------------------------------------------------------------------------------
 *
 * `entry_irq_arm()` is called from the **end** of the first `ml_set_decrementer` this image makes -
 * after 481's countdown sample, after 482's probe, after the kernel's own first deadline has been
 * written to `CNTV_TVAL`, and after `g_dec_writes` has been incremented. Three of those four
 * conditions are about re-entrancy and they are worth naming:
 *
 *   - `ml_install_interrupt_handler` ends with `initialize_screen(NULL, kPEAcquireScreen)`
 *     (`osfmk/arm/machine_routines.c:420`), which is not a store: it walks the video-console path.
 *     Called from *inside* 482's probe - which is itself inside the sample block - a re-entrant
 *     `setPop` would run this image's `set_decrementer` again while it was still in its first-call
 *     branch, and the count that guards that branch (`g_dec_writes == 0`) has not been incremented
 *     yet at that point. From after `g_dec_writes++` the branch is closed.
 *   - `g_irq_armed` is a second guard, because "called once" is a claim about the boot and not about
 *     this function: a second call returns without touching a register.
 *   - The line is enabled only after `ml_install_interrupt_handler` has returned and the five words
 *     have been read back, so the window in which the line is live but the handler is not does not
 *     exist - and `entry_irq_arm` returns 0 if any of that read-back disagrees, leaving the machine
 *     exactly as 482 left it.
 *
 * ------------------------------------------------------------------------------------------------
 * The EOI, which is this project's decision and not Apple's
 * ------------------------------------------------------------------------------------------------
 *
 * Apple's armv7 does its timer EOI **from the FIQ bank**, out of `cpu_tbd_hardware_addr` /
 * `_val`: `fiq_context_init`'s `#else` branch loads them into r10 and r11
 * (`osfmk/arm/machine_routines_asm.s:955-957`) and each SoC's own handler writes r11 to r10 plus a
 * private offset - `fleh_fiq_s7002`'s `str r11, [r10, #PMGR_INTERVAL_TMR_CTL_OFFSET]` (`:1071`),
 * `fleh_fiq_t8002`'s AIC write (`:1101`). Those are *private timer blocks*, not the GIC: there is no
 * `GICC_EOIR` anywhere in Apple's armv7 tree, and `rtclock_timebase_addr`/`_val` reach nothing else
 * (481 registered the honest `0u, 0u`, which is "no EOI register"). MSM8974's timers *are* on the
 * GIC, so the acknowledgement has to be `GICC_EOIR`, and the shape of it is measured rather than
 * invented: the payload's own IRQ path in this boot reads `GICC_IAR` into `gic_timer_last_iar =
 * 0x13`, matches it against its two PPIs, services, and writes `GICC_EOIR` with the value it read
 * (`stages/stage90/gic.c:246`, `:293`). That is what this handler does.
 *
 * The order - EOI before `rtclock_intr` - is chosen and not inherited: `rtclock_intr` re-arms the
 * countdown for the next deadline (through `setPop` -> `ml_set_decrementer` -> this image's writer),
 * so EOI'ing first means a second expiry during that work is a line that is already back in the
 * distributor's hands rather than one the CPU interface still believes is active.
 */
#include <stdint.h>

#include "entry_gic.h"

/* Set by `build_entry.sh` from `STAGE90_ENTRY_TRACE`; the live channel exists only in a traced
 * build, and `xnu_live_irq_traced` carries the value into the report so a zero count is never
 * ambiguous between "never armed" and "not instrumented". */
#ifndef STAGE90_IRQ_TRACED
#define STAGE90_IRQ_TRACED 0
#endif

#if STAGE90_IRQ_TRACED
extern void entry_live_write(const char *key, uint32_t value);
#define IRQ_LIVE(key, value) entry_live_write((key), (uint32_t)(value))
#else
#define IRQ_LIVE(key, value) do { (void)(key); (void)(value); } while (0)
#endif

extern char BootCpuData[];
extern void rtclock_intr(unsigned int is_user_context);
extern void entry_epilogue(const char *why) __attribute__((noreturn));

/* `IOInterruptHandler` (`iokit/IOKit/IOInterrupts.h:55`) as this image can state it: the four
 * registers `fleh_irq_handler` loads out of `cpu_data`, in the order it loads them. The real
 * declaration is `extern void ml_install_interrupt_handler(void *nub, int source, void *target,
 * IOInterruptHandler handler, void *refCon)` (`osfmk/arm/machine_routines.h:277`), and this is that
 * with the one type this image does not have spelled out. */
typedef void (*entry_irq_handler_t)(void *target, void *refCon, void *nub, int source);
extern void ml_install_interrupt_handler(void *nub, int source, void *target,
                                         entry_irq_handler_t handler, void *refCon);

/* --------------------------------------------------------------------------------------------- */
/* The register window, defined once and used by both of this step's objects                      */
/* --------------------------------------------------------------------------------------------- */

/*
 * Read with `volatile` and write followed by `dsb sy`, because a peripheral register write that is
 * still in the write buffer when the next instruction reads the same block is a read of the old
 * value. The handler below reads `GICC_IAR` and the distributor's pending register, and
 * `gicd_write(ISENABLER0, ...)` followed by anything at all is exactly the sequence where an
 * unbarriered write would turn a reading into a claim. These four are the *only* place in the image
 * the GIC is addressed, which is the property `tools/check_irq_routing.py` asserts by reading
 * `entry_gic.c` for a second copy of the arithmetic.
 */
uint32_t gicd_read(uint32_t off)
{
    return *(volatile uint32_t *)(uintptr_t)(STAGE90_GIC_DIST_BASE + off);
}

void gicd_write(uint32_t off, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)(STAGE90_GIC_DIST_BASE + off) = value;
    __asm__ volatile ("dsb sy" ::: "memory");
}

uint32_t gicc_read(uint32_t off)
{
    return *(volatile uint32_t *)(uintptr_t)(STAGE90_GIC_CPU_BASE + off);
}

void gicc_write(uint32_t off, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)(STAGE90_GIC_CPU_BASE + off) = value;
    __asm__ volatile ("dsb sy" ::: "memory");
}

/* --------------------------------------------------------------------------------------------- */
/* The build's one switch, and why it is a source constant                                        */
/* --------------------------------------------------------------------------------------------- */

/*
 * **0 is the state 482 left the machine in; 1 is this step's object.** The value is a `#define` here
 * rather than a `-D` from the environment because it selects *what this image does*, and an
 * environment variable would let the linked image and the source that the check reads disagree -
 * `tools/check_irq_routing.py` reads this line, and it is the only way the check can require the
 * right thing of the vector page in either state (slot 6 holds this image's reporting `fleh_irq`
 * when the flag is 0 and Apple's `locore_fleh_irq` when it is 1, and there is no third state).
 *
 * Run A of this step was built with 0: everything installed and configuring, no line enabled and no
 * countdown unmasked - the plumbing's own measurement, which is the run whose console and block
 * census have to be unchanged. Run B was built with 1, and 1 is what is committed, so a plain build
 * of this tree is the configuration that was measured taking interrupts.
 */
#define STAGE90_IRQ_ENABLE_LINE 1

/* --------------------------------------------------------------------------------------------- */
/* The second-level handler                                                                       */
/* --------------------------------------------------------------------------------------------- */

/*
 * There is exactly one line this handler services itself, one intid it acts on, and **since 496 one
 * table of lines it will hand to a driver**. Everything else is recorded and **named** rather than
 * serviced or ignored, because the two silent failure modes are both worse than a stop: an unhandled
 * line that is EOI'd and returns is an interrupt that will arrive again immediately (a run that looks
 * like a hang with no message), and one that is not EOI'd and returns is an interrupt that will
 * arrive again immediately for a different reason. So the third case names its intid and stops - and
 * that case is what a driver gets if it pends a line it never registered, which is why the registry
 * below is a gate and not a convenience.
 */
static uint32_t g_irq_seq;
static uint32_t g_irq_args[4];
static uint32_t g_irq_timer_count;
static uint32_t g_irq_other_count;
static uint32_t g_irq_spurious_count;
static uint32_t g_irq_late_count;

/* 500's two counters, and they are records by 497's rule rather than by habit: the refusal count is
 * written where a caller asked for an intid this machine does not have, and the enable count is
 * incremented in the enable and read by the two keys published after it - in the same call, which is
 * why it is the *pair* of keys that keeps it: `_line_seq` and `_line_rc` are two readers of values that
 * depend on the increment, and the check requires both to be published. */
static uint32_t g_irq_line_enabled;
static uint32_t g_irq_line_refused;

/* --------------------------------------------------------------------------------------------- */
/* The client registry (496)                                                                      */
/* --------------------------------------------------------------------------------------------- */

/*
 * **One registration table, and the order its fields are written in is the concurrency contract.**
 * The dispatcher runs with interrupts masked, but `entry_irq_register_client` runs in *process*
 * context with them open, so a registration can be preempted by the very IRQ path that reads the
 * table. The rule that makes that harmless is: a slot is taken only when its handler is zero, the
 * intid and the `refCon` are stored first, and **the handler is stored last** - so a scan racing the
 * write either does not match the intid (the slot is not yet that line's) or finds a zero handler and
 * treats the line as unregistered, which is exactly what it was a moment earlier. The reverse order
 * would publish a handler for an intid the scan has not yet been told about, which is not a
 * variation on this - it is a different, broken state.
 */
typedef void (*entry_irq_client_t)(void *refCon, uint32_t intid);

static uint32_t g_irq_cli_intid[STAGE90_IRQ_CLIENTS];
static uint32_t g_irq_cli_refcon[STAGE90_IRQ_CLIENTS];
static entry_irq_client_t g_irq_cli_handler[STAGE90_IRQ_CLIENTS];
static uint32_t g_irq_cli_calls[STAGE90_IRQ_CLIENTS];
static uint32_t g_irq_cli_registered;
static uint32_t g_irq_cli_unregistered;
static uint32_t g_irq_cli_refused;
static uint32_t g_irq_cli_calls_total;
static uint32_t g_irq_cli_storm;

/*
 * Register a client for one line, or refuse and say why. **A refusal is a value and not a silence**:
 * the two lines this image will not hand over are its own - `STAGE90_GIC_TIMER_INTID`, which the
 * dispatcher services itself before it reaches this table, and `STAGE90_GICC_SPURIOUS_ID`, which is
 * not a line at all - and a caller that asked for one of them has made a mistake the log should
 * carry rather than a call that quietly did nothing. `_cli_refused` is that count.
 */
uint32_t entry_irq_register_client(uint32_t intid, uint32_t handler, uint32_t refCon)
{
    uint32_t i, slot = STAGE90_IRQ_CLIENTS;

    if (handler == 0u ||
        intid == STAGE90_GIC_TIMER_INTID ||
        intid == STAGE90_GICC_SPURIOUS_ID) {
        g_irq_cli_refused++;
        IRQ_LIVE("xnu_live_irq_cli_refused", g_irq_cli_refused);
        IRQ_LIVE("xnu_live_irq_cli_refused_intid", intid);
        return 0u;
    }

    for (i = 0u; i < STAGE90_IRQ_CLIENTS; i++) {
        if (g_irq_cli_handler[i] == 0) {
            if (slot == STAGE90_IRQ_CLIENTS)
                slot = i;
        } else if (g_irq_cli_intid[i] == intid) {
            /* One line, one client. A second registration for a line that already has one is a
             * driver that has lost track of its own state, and the first client's handler is still
             * the one the dispatcher will call - which is the safe direction, and recorded. */
            g_irq_cli_refused++;
            IRQ_LIVE("xnu_live_irq_cli_refused", g_irq_cli_refused);
            IRQ_LIVE("xnu_live_irq_cli_refused_intid", intid);
            return 0u;
        }
    }

    if (slot == STAGE90_IRQ_CLIENTS) {
        g_irq_cli_refused++;
        IRQ_LIVE("xnu_live_irq_cli_refused", g_irq_cli_refused);
        IRQ_LIVE("xnu_live_irq_cli_refused_intid", intid);
        return 0u;
    }

    g_irq_cli_intid[slot] = intid;
    g_irq_cli_refcon[slot] = refCon;
    g_irq_cli_handler[slot] = (entry_irq_client_t)(uintptr_t) handler;

    g_irq_cli_registered++;
    IRQ_LIVE("xnu_live_irq_cli_seq", g_irq_cli_registered);
    IRQ_LIVE("xnu_live_irq_cli_slot", slot);
    IRQ_LIVE("xnu_live_irq_cli_intid", intid);
    IRQ_LIVE("xnu_live_irq_cli_handler", handler);
    IRQ_LIVE("xnu_live_irq_cli_refcon", refCon);
    IRQ_LIVE("xnu_live_irq_cli_capacity", STAGE90_IRQ_CLIENTS);
    return 1u;
}

/*
 * And the way back, which is **the client's own decision and not the dispatcher's**. A registration
 * that cannot be withdrawn is a line the driver can never stop owning, and the failure that makes
 * that matter is not hypothetical: an intid that reaches the dispatcher with no client is the case
 * that *stops the run*, so a driver that gives its line back has to know the line is quiet first.
 * That guard is the client's - it is the one that can read its own device - and this function only
 * refuses to lie: a line with no registration is counted (`_cli_unregistered_gone`) and answered 0,
 * so a driver that unregisters twice, or after a failed registration, can tell that from the case it
 * is trying to distinguish.
 */
uint32_t entry_irq_unregister_client(uint32_t intid)
{
    uint32_t i;

    for (i = 0u; i < STAGE90_IRQ_CLIENTS; i++) {
        if (g_irq_cli_handler[i] != 0 && g_irq_cli_intid[i] == intid) {
            /* The handler first: the slot stops being live for the dispatcher before anything else
             * about it changes, so a delivery racing this store is a line with no client - the case
             * the run's own record names - and not a call through half-cleared state. */
            g_irq_cli_handler[i] = 0;
            g_irq_cli_intid[i] = 0u;
            g_irq_cli_refcon[i] = 0u;
            g_irq_cli_unregistered++;
            IRQ_LIVE("xnu_live_irq_cli_unregistered", g_irq_cli_unregistered);
            IRQ_LIVE("xnu_live_irq_cli_unreg_intid", intid);
            return 1u;
        }
    }

    IRQ_LIVE("xnu_live_irq_cli_unreg_gone", intid);
    return 0u;
}

/*
 * ------------------------------------------------------------------------------------------------
 * 500: the per-line distributor state, written once for the driver that owns the line
 * ------------------------------------------------------------------------------------------------
 *
 * 498 left the line owned and unarmed: `entry_irq_register_client` filed the driver's handler for intid
 * 40 and `entry_irq_handler` will call it, but a registration is not a delivery. What was missing was
 * **the deadline in the frame** - nothing in this boot had put one there, and a line with no device
 * behind it asserts nothing. The run's answer is exact: with the frame's `CNTP_TVAL`/`CTRL` written,
 * the same registration delivered three times (`_isr_calls` 3, `_isr_intid` 0x28, `_isr_rearmed` 2,
 * `_isr_masked` 1), and every distributor write below wrote a value that was already there.
 *
 * **That last clause is a correction to what this comment first said, and the before-values are what
 * corrected it.** The argument was that the distributor's per-line state had to be written because at
 * reset an SPI has `ITARGETSR` 0 and its enable bit clear. True of a GIC at reset - and *this* GIC was
 * not at reset: the run read `_line_isen_before = 0x100` (intid 40 already enabled), `_line_target_before
 * = 0x01010101` (already CPU 0) and `_line_group_before = 0` (already Group 0). The machine that booted
 * before this image had configured the line, which is not a coincidence: the device's own kernel takes
 * this frame's deadline on this SPI, so the line is one Android enables at its own boot. So the four
 * writes are the *architecture's* requirement - a line whose target byte is 0 is dropped whatever the
 * handler is, and a machine whose predecessor left it unconfigured needs all four - and the before/after
 * pairs are what tell the two cases apart. A step that had asserted the distributor was the reason
 * `_isr_calls` was 0 in 498 would have recorded a cause the machine does not have.
 *
 * What this function writes, then, and why each write is worth its key:
 *
 *   1. **the group.** A line is delivered through the CPU interface's bank whose enable bit is set, and
 *      `GICC_CTLR` on this machine reads 1 (`EnableGrp0` only, 483's and 498's runs) - so the line has to
 *      be in Group 0, and the register is written to *make* it so rather than assumed, because a line in
 *      the other group would be enabled, pending, and never delivered.
 *   2. **the target.** `GICD_ITARGETSR` is one byte per intid. This is the byte that decides *which CPU*
 *      hears the line, and the caller names the mask because the caller is the one that knows.
 *   3. **the pending state, cleared.** A line that latched before it was configured would be delivered
 *      the moment the enable bit went in, which would make the first callback indistinguishable from a
 *      delivery the driver asked for. The pending bit is read before and after and both are published.
 *   4. **the enable bit.** Last, so that the three writes above are all in place before the distributor
 *      can raise anything, and read back, because a write to a device with no clock behind it can
 *      vanish - the same gate `entry_irq_arm` uses.
 *
 * **What is *not* written is as much of the reading as what is.** The priority register is left exactly
 * as the distributor has it, and published beside `GICC_PMR`: "the line was never delivered" and "the
 * line's priority is outside the mask" are different findings, and a driver that picked a priority would
 * make the second one unreadable. The trigger configuration is not written either - it is *read*, and
 * published as a field, because the tree declares the frame's line level-high while a distributor entry
 * that was never programmed is level by reset: the two agreeing is a property the log can show and a
 * comment cannot.
 *
 * **Every number is derived from the intid.** `ISENABLER`, `ICENABLER`, `ISPENDR`, `ICPENDR` and
 * `IGROUPR` are one bit per intid at word `intid / 32`; `ITARGETSR` and `IPRIORITYR` are one byte per
 * intid; `ICFGR` is two bits. A literal 40 or a literal 8 here would be a second definition of what the
 * tree's cells mean, and only one of the two readings of `<0 8 0x4>` is this machine's.
 *
 * A refusal is a value: an intid outside the architecture's range (0 and 1020 and above are not lines
 * this machine has) is counted and answered 0 rather than turned into a write at an offset that belongs
 * to another register.
 */
uint32_t entry_irq_enable_line(uint32_t intid, uint32_t target)
{
    const uint32_t word   = intid / 32u;                 /* which 32-line word of the bit registers */
    const uint32_t bit    = 1u << (intid % 32u);         /* the bit inside it                       */
    const uint32_t isaddr = STAGE90_GICD_ISENABLER0 + word * 4u;
    const uint32_t cpaddr = STAGE90_GICD_ICPENDR0 + word * 4u;
    const uint32_t ipaddr = STAGE90_GICD_ISPENDR0 + word * 4u;
    const uint32_t gaddr  = STAGE90_GICD_IGROUPR0 + word * 4u;
    const uint32_t bshift = (intid % 4u) * 8u;           /* the byte inside a four-byte register    */
    const uint32_t tword  = STAGE90_GICD_ITARGETSR0 + (intid & ~3u);
    const uint32_t pword  = STAGE90_GICD_IPRIORITY0 + (intid & ~3u);
    const uint32_t iword  = STAGE90_GICD_ICFGR0 + (intid / 16u) * 4u;
    const uint32_t ishift = (intid % 16u) * 2u;
    const uint32_t bmask  = 0xffu << bshift;
    uint32_t isen_before, isen_after, pend_before, pend_after;
    uint32_t target_before, target_after, group_before, group_after;
    uint32_t prio, icfgr, ctlr, pmr, rc;

    if (intid == 0u || intid >= STAGE90_GIC_MAX_INTID) {
        g_irq_line_refused++;
        IRQ_LIVE("xnu_live_irq_line_refused", g_irq_line_refused);
        IRQ_LIVE("xnu_live_irq_line_refused_intid", intid);
        return 0u;
    }

    isen_before   = gicd_read(isaddr);
    pend_before   = gicd_read(ipaddr);
    target_before = gicd_read(tword);
    group_before  = gicd_read(gaddr);
    prio          = gicd_read(pword);
    icfgr         = gicd_read(iword);
    ctlr          = gicd_read(STAGE90_GICD_CTLR);
    pmr           = gicc_read(STAGE90_GICC_PMR);

    IRQ_LIVE("xnu_live_irq_line_word", word);
    IRQ_LIVE("xnu_live_irq_line_bit", bit);
    IRQ_LIVE("xnu_live_irq_line_isaddr", isaddr);
    IRQ_LIVE("xnu_live_irq_line_isen_before", isen_before);
    IRQ_LIVE("xnu_live_irq_line_pend_before", pend_before);
    IRQ_LIVE("xnu_live_irq_line_target_before", target_before);
    IRQ_LIVE("xnu_live_irq_line_group_before", group_before);
    IRQ_LIVE("xnu_live_irq_line_prio", prio);
    IRQ_LIVE("xnu_live_irq_line_icfgr", icfgr);
    IRQ_LIVE("xnu_live_irq_line_icfgr_field", (icfgr >> ishift) & 0x3u);
    IRQ_LIVE("xnu_live_irq_line_dist_ctlr", ctlr);
    IRQ_LIVE("xnu_live_irq_line_dist_pmr", pmr);
    IRQ_LIVE("xnu_live_irq_line_target_want", target);

    /* 3: clear whatever is latched before the line can be delivered by the enable below. */
    gicd_write(cpaddr, bit);
    pend_after = gicd_read(ipaddr);

    /* 2: the byte this intid lives in, and only that byte - the other three intids' targets are not
     * this caller's to change. */
    gicd_write(tword, (target_before & ~bmask) | ((target & 0xffu) << bshift));
    target_after = gicd_read(tword);

    /* 1: the group the CPU interface actually enables. */
    gicd_write(gaddr, group_before & ~bit);
    group_after = gicd_read(gaddr);

    /* 4: the enable, last, and read back - the whole return value. */
    gicd_write(isaddr, bit);
    isen_after = gicd_read(isaddr);
    rc = ((isen_after & bit) != 0u) ? 1u : 0u;

    g_irq_line_enabled++;
    IRQ_LIVE("xnu_live_irq_line_seq", g_irq_line_enabled);
    IRQ_LIVE("xnu_live_irq_line_intid", intid);
    IRQ_LIVE("xnu_live_irq_line_isen_after", isen_after);
    IRQ_LIVE("xnu_live_irq_line_pend_after", pend_after);
    IRQ_LIVE("xnu_live_irq_line_target_after", target_after);
    IRQ_LIVE("xnu_live_irq_line_group_after", group_after);
    IRQ_LIVE("xnu_live_irq_line_rc", rc);
    return rc;
}

void entry_irq_handler(void *target, void *refCon, void *nub, int source)
{
    uint32_t iar, intid, before, late;

    /* Every argument, because this is the ABI claim the whole step rests on: `fleh_irq_handler`
     * loads r0..r3 out of `cpu_data` and this function's parameters are those four. The values are
     * recorded on the first call, and `target` is the only one with a value this image chose. */
    ++g_irq_seq;
    if (g_irq_seq == 1u) {
        g_irq_args[0] = (uint32_t)(uintptr_t)target;
        g_irq_args[1] = (uint32_t)(uintptr_t)refCon;
        g_irq_args[2] = (uint32_t)(uintptr_t)nub;
        g_irq_args[3] = (uint32_t)source;
    }

    iar = gicc_read(STAGE90_GICC_IAR);
    intid = iar & STAGE90_GICC_IAR_INTID_MASK;

    if (g_irq_seq == 1u) {
        IRQ_LIVE("xnu_live_irq_handler_entered", g_irq_seq);
        IRQ_LIVE("xnu_live_irq_target", g_irq_args[0]);
        IRQ_LIVE("xnu_live_irq_refcon", g_irq_args[1]);
        IRQ_LIVE("xnu_live_irq_nub", g_irq_args[2]);
        IRQ_LIVE("xnu_live_irq_source", g_irq_args[3]);
        /* **497: the `g_irq_first_iar` this used to be published from does not exist, and that is the
         * fix rather than the defect.** The static was written here and read one line later, so the
         * compiler forwarded the value and removed the variable; the key published the local `iar`
         * and every value in the log was right - which is why nothing in a run could show it. The
         * honest reading is that there was never a *record* here: the first `IAR` is a value this
         * function holds at this moment, and it is published as such. A file-scope record earns its
         * storage by being read where its value is not already known - `g_irq_timer_count` and the
         * registry's counters are - and `claim_static_storage` in `tools/check_irq_routing.py`
         * refuses an image in which one of the declared ones is missing. */
        IRQ_LIVE("xnu_live_irq_first_iar", iar);
    }

    if (intid == STAGE90_GIC_TIMER_INTID) {
        /* Acknowledge first, then let the kernel re-arm. `late` is the countdown's own register:
         * if it reads back as a saturated `DECREMENTER_MAX` the deadline this interrupt was for has
         * been replaced by a far one, which is the shape an already-serviced pop leaves behind. */
        gicc_write(STAGE90_GICC_EOIR, iar);
        before = gicd_read(STAGE90_GICD_ISPENDR0);
        g_irq_timer_count++;
        rtclock_intr(0);
        late = gicd_read(STAGE90_GICD_ISPENDR0);
        if ((late & (1u << STAGE90_GIC_TIMER_INTID)) != 0u)
            g_irq_late_count++;

        if ((g_irq_timer_count & (g_irq_timer_count - 1u)) == 0u) {
            IRQ_LIVE("xnu_live_irq_timer_count", g_irq_timer_count);
            IRQ_LIVE("xnu_live_irq_pend_before", before);
            IRQ_LIVE("xnu_live_irq_pend_after", late);
            /* The EOI's own count, and it is a *count* rather than a sample: `_pend_after` above says
             * what the distributor looked like on this one call, and this says how many of the calls
             * so far left the line still asserted after the acknowledgement. It is published on the
             * same powers of two and not only at the end, because the run has no end - the payload's
             * log is captured when the watchdog brings the phone back, and a counter that is only
             * written at the end is a counter that is never read. */
            IRQ_LIVE("xnu_live_irq_late_count", g_irq_late_count);
        }
        return;
    }

    /*
     * **496: the lines a driver owns.** Read out of the table one entry at a time and called only
     * when the intid matches, so the stop below is still reachable for every line this image did not
     * hand out - which is the property that makes a driver's mistake a stop rather than a storm.
     *
     * The acknowledgement is written **here and before the call**, for the timer line's reason:
     * EOIR'ing first means a line that is re-asserted during the client's work is already back in the
     * distributor's hands, and it means the client cannot forget. The client's job is its *device* -
     * clearing its own status, which is the one thing this image cannot do for it.
     *
     * The call is counted per slot and the count is a bound: a line that keeps re-asserting itself
     * without ever being satisfied would otherwise call the client until the watchdog, and the two
     * silent failure modes this handler already reasons about are both worse than a stop. So the
     * cap stops *and names the intid*, which is the same shape as the unexpected-line case and the
     * reason `tools/check_irq_routing.py` requires exactly two stops and not one.
     */
    {
        uint32_t i;

        for (i = 0u; i < STAGE90_IRQ_CLIENTS; i++) {
            entry_irq_client_t client = g_irq_cli_handler[i];

            if (client == 0 || g_irq_cli_intid[i] != intid)
                continue;

            gicc_write(STAGE90_GICC_EOIR, iar);
            g_irq_cli_calls[i]++;
            g_irq_cli_calls_total++;
            /* `g_irq_cli_last` was the same shape as `g_irq_first_iar` and got the same answer (497):
             * a record whose only reader is the key beside its store is not a record. The intid of
             * this call is published where it is known. */
            IRQ_LIVE("xnu_live_irq_cli_last", intid);
            IRQ_LIVE("xnu_live_irq_cli_calls_total", g_irq_cli_calls_total);
            if ((g_irq_cli_calls_total & (g_irq_cli_calls_total - 1u)) == 0u)
                IRQ_LIVE("xnu_live_irq_cli_calls0", g_irq_cli_calls[0]);

            if (g_irq_cli_calls[i] > STAGE90_IRQ_CLIENT_CAP) {
                g_irq_cli_storm++;
                IRQ_LIVE("xnu_live_irq_cli_storm", g_irq_cli_storm);
                IRQ_LIVE("xnu_live_irq_cli_storm_intid", intid);
                IRQ_LIVE("xnu_live_irq_cli_storm_calls", g_irq_cli_calls[i]);
                entry_epilogue("exception: irq client storm");
            }

            client((void *)(uintptr_t) g_irq_cli_refcon[i], intid);
            return;
        }
    }

    if (intid == STAGE90_GICC_SPURIOUS_ID) {
        /* No EOI: the architecture says a spurious read acknowledges nothing, so writing `EOIR`
         * with `0x3ff` is an acknowledgement of an interrupt that does not exist. */
        g_irq_spurious_count++;
        IRQ_LIVE("xnu_live_irq_spurious_count", g_irq_spurious_count);
        return;
    }

    /* Any other line: this image handed none out (the table above is the only way one becomes
     * serviceable), so this is a fact about the machine and not a case to service. EOI it so that it
     * cannot be asserted straight back at us, record it, and stop with the intid in the channel -
     * which is what 308's run needed and did not have. */
    gicc_write(STAGE90_GICC_EOIR, iar);
    g_irq_other_count++;
    IRQ_LIVE("xnu_live_irq_other_count", g_irq_other_count);
    IRQ_LIVE("xnu_live_irq_other_iar", iar);
    IRQ_LIVE("xnu_live_irq_other_cli", (uint32_t)(uintptr_t) g_irq_cli_handler);
    IRQ_LIVE("xnu_live_irq_timer_count_final", g_irq_timer_count);
    entry_epilogue("exception: irq line");
}

/* --------------------------------------------------------------------------------------------- */
/* The arming sequence                                                                            */
/* --------------------------------------------------------------------------------------------- */

/* The value `cpu_data->interrupt_target` is given: this image's own address, so that the first
 * argument the handler is called with is a number the log can recognise. Apple passes the
 * interrupt controller object (`IOCPUInterruptController::enableCPUInterrupt`,
 * `iokit/Kernel/IOCPU.cpp:828`), whose `handleInterrupt` looks the source up in `vectors[]` and
 * forwards to the registered client - one level of indirection this image does not need and cannot
 * supply, because the object graph that would own it is IOKit's platform plane and not the entry
 * window. What 483 borrows from that call site is the *shape*: a nub, a source, a target and a
 * handler, installed at the same `ml_install_interrupt_handler` boundary. */
static uint32_t g_irq_target;

static uint32_t g_irq_armed;
static uint32_t g_irq_cpsr;
static uint32_t g_irq_istack;
static uint32_t g_irq_enabled_line;

uint32_t entry_irq_arm(void)
{
    uint32_t before[5], after[5], icfgr, priority_word, targets_word, pending, enabled, cpsr;
    const uint32_t bit = 1u << STAGE90_GIC_TIMER_INTID;
    const uint32_t word = STAGE90_GICD_ICFGR0 + (STAGE90_GIC_TIMER_INTID / 16u) * 4u;
    const uint32_t shift = (STAGE90_GIC_TIMER_INTID % 16u) * 2u;

    if (g_irq_armed != 0u)
        return g_irq_enabled_line;

    /*
     * The precondition, read and not assumed. `fleh_irq_kernel` ends by loading `sp` from
     * `[cpu_data, CPU_ISTACKPTR]` (`locore.s:1377`) and `fleh_irq_user` from `[cpu, CPU_ISTACKPTR]`
     * through the CPU data (`:1304`), so an interrupt arriving with a zero there is a load from
     * address 0 in the middle of the exception entry - a data abort inside the abort, which the
     * watchdog would report as a hang. A zero here means the handler is not installed and the line
     * is not enabled; the run records the value either way, so "the machine was not ready" and "the
     * step did not run" are different lines in the log.
     */
    g_irq_istack = *(volatile uint32_t *)((uintptr_t)BootCpuData + STAGE90_CPU_ISTACKPTR);
    __asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));
    g_irq_cpsr = cpsr;

    IRQ_LIVE("xnu_live_irq_traced", STAGE90_IRQ_TRACED);
    IRQ_LIVE("xnu_live_irq_enable_line_compiled", STAGE90_IRQ_ENABLE_LINE);
    IRQ_LIVE("xnu_live_irq_istackptr", g_irq_istack);
    IRQ_LIVE("xnu_live_irq_cpsr", g_irq_cpsr);
    IRQ_LIVE("xnu_live_irq_intid", STAGE90_GIC_TIMER_INTID);
    /* **One name, one number - and the reason these two are worth a paragraph.** 496's first cut had
     * `g_irq_icfgr_word` holding the *value read* while `xnu_live_irq_icfgr_word` published the
     * *offset*: one name for two different numbers, in the file that defines both. They are the word
     * and the field in it, taken from the intid in one expression each (`word`, `shift`, above), and
     * 497's measurement is that a record whose only reader is the key beside its store is not a record
     * at all: the compiler forwards the value and the variable goes. So these read the expressions,
     * and `xnu_live_irq_icfgr` below is the *value* read out of that word - a different key, because
     * it is a different number. */
    IRQ_LIVE("xnu_live_irq_icfgr_word", word);
    IRQ_LIVE("xnu_live_irq_icfgr_shift", shift);

    if (g_irq_istack == 0u)
        return 0u;

    g_irq_armed = 1u;

    /*
     * The five words **before**: on this image they are zero, and that is 482's own finding - the
     * dispatch at 0x80015abc loads `INTERRUPT_HANDLER` into r5 and `blx r5`, and nothing has ever
     * written it. Recording the before-state is what makes the after-state a measurement rather
     * than a sequence of stores this file performed on itself.
     */
    {
        uint32_t i;
        const uint32_t offs[5] = { STAGE90_CPU_INTERRUPT_TARGET, STAGE90_CPU_INTERRUPT_REFCON,
                                   STAGE90_CPU_INTERRUPT_NUB, STAGE90_CPU_INTERRUPT_SOURCE,
                                   STAGE90_CPU_INTERRUPT_HANDLER };
        for (i = 0u; i < 5u; i++)
            before[i] = *(volatile uint32_t *)((uintptr_t)BootCpuData + offs[i]);
    }
    IRQ_LIVE("xnu_live_irq_handler_before", before[4]);
    IRQ_LIVE("xnu_live_irq_target_before", before[0]);
    IRQ_LIVE("xnu_live_irq_nub_before", before[2]);
    IRQ_LIVE("xnu_live_irq_source_before", before[3]);

    /*
     * Install, through Apple's own function rather than by writing the five fields here. This is the
     * one-definition rule applied to a *writer*: a second writer is a second definition, and the
     * offsets this image carries are compared against `assym.s` while Apple's stores are the ones
     * `fleh_irq_handler` will actually read. `getCpuDatap()` inside resolves to `BootCpuData` on the
     * boot CPU, and `ml_init_timebase`'s own guard already established that this thread is on it.
     */
    (void)ml_install_interrupt_handler((void *)BootCpuData, 0, (void *)&g_irq_target,
                                       entry_irq_handler, (void *)0);

    {
        uint32_t i;
        const uint32_t offs[5] = { STAGE90_CPU_INTERRUPT_TARGET, STAGE90_CPU_INTERRUPT_REFCON,
                                   STAGE90_CPU_INTERRUPT_NUB, STAGE90_CPU_INTERRUPT_SOURCE,
                                   STAGE90_CPU_INTERRUPT_HANDLER };
        for (i = 0u; i < 5u; i++)
            after[i] = *(volatile uint32_t *)((uintptr_t)BootCpuData + offs[i]);
    }
    IRQ_LIVE("xnu_live_irq_target_after", after[0]);
    IRQ_LIVE("xnu_live_irq_refcon_after", after[1]);
    IRQ_LIVE("xnu_live_irq_nub_after", after[2]);
    IRQ_LIVE("xnu_live_irq_source_after", after[3]);
    IRQ_LIVE("xnu_live_irq_handler_after", after[4]);

    /* The read-back is a gate and not a report: a handler that is not the one this file defines
     * means the dispatch would call something else, and the honest thing to do with the line is
     * leave it alone. */
    if (after[4] != (uint32_t)(uintptr_t)&entry_irq_handler ||
        after[0] != (uint32_t)(uintptr_t)&g_irq_target) {
        IRQ_LIVE("xnu_live_irq_install_ok", 0u);
        return 0u;
    }
    IRQ_LIVE("xnu_live_irq_install_ok", 1u);

    /*
     * The line, read before it is touched: 482 inferred edge behaviour from a pending bit that did
     * not follow the line back down, and this is the register that settles it. It is read and
     * recorded, not written - `ICFGR` for a PPI is at best implementation-defined and at worst
     * read-only, and 483 has no measurement that says what to write.
     */
    icfgr = gicd_read(word);
    IRQ_LIVE("xnu_live_irq_icfgr", icfgr);
    IRQ_LIVE("xnu_live_irq_icfgr_field", (icfgr >> shift) & 0x3u);

    priority_word = gicd_read(STAGE90_GICD_IPRIORITY0 + (STAGE90_GIC_TIMER_INTID & ~3u));
    targets_word = gicd_read(STAGE90_GICD_ITARGETSR0 + (STAGE90_GIC_TIMER_INTID & ~3u));
    IRQ_LIVE("xnu_live_irq_priority_word", priority_word);
    IRQ_LIVE("xnu_live_irq_targets_word", targets_word);

    /* Start from a clean slate at the distributor: clear anything the probe or the payload's own
     * timer left pending on this line before enabling it. */
    gicd_write(STAGE90_GICD_ICPENDR0, bit);
    pending = gicd_read(STAGE90_GICD_ISPENDR0);
    IRQ_LIVE("xnu_live_irq_pend_cleared", pending);

#if STAGE90_IRQ_ENABLE_LINE
    /*
     * The enable, and the last thing that happens before the countdown's mask comes off. Note what
     * is *not* touched: `GICC_CTLR` and `GICC_PMR`. The measured fact is that the CPU interface
     * delivers to this image as it stands - the payload's own IRQ path took `IAR = 0x13` in this
     * same boot with `gicc_ctlr = 1` and `pmr = 0xf0`, and 482 cleared and restored both without
     * ever needing to know which group bit the non-secure view presents. So this step changes
     * exactly one bit in the system, at the distributor, and the run's `GICC_CTLR`/`PMR` readings
     * are the evidence that nothing else moved.
     */
    gicd_write(STAGE90_GICD_ISENABLER0, bit);
    enabled = gicd_read(STAGE90_GICD_ISENABLER0);
    g_irq_enabled_line = ((enabled & bit) != 0u) ? 1u : 0u;
#else
    enabled = gicd_read(STAGE90_GICD_ISENABLER0);
    g_irq_enabled_line = 0u;
#endif

    IRQ_LIVE("xnu_live_irq_isenabler0", enabled);
    IRQ_LIVE("xnu_live_irq_line_enabled", g_irq_enabled_line);
    IRQ_LIVE("xnu_live_irq_armed", g_irq_armed);
    IRQ_LIVE("xnu_live_irq_cpu_ctlr", gicc_read(STAGE90_GICC_CTLR));
    IRQ_LIVE("xnu_live_irq_cpu_pmr", gicc_read(STAGE90_GICC_PMR));

    return g_irq_enabled_line;
}
