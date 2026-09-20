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
 * There is exactly one line this handler expects, and one intid it acts on. Everything else is
 * recorded and **named** rather than serviced or ignored, because the two silent failure modes are
 * both worse than a stop: an unhandled line that is EOI'd and returns is an interrupt that will
 * arrive again immediately (a run that looks like a hang with no message), and one that is not
 * EOI'd and returns is an interrupt that will arrive again immediately for a different reason. So
 * the third case names its intid and stops.
 */
static uint32_t g_irq_seq;
static uint32_t g_irq_args[4];
static uint32_t g_irq_timer_count;
static uint32_t g_irq_other_count;
static uint32_t g_irq_spurious_count;
static uint32_t g_irq_late_count;
static uint32_t g_irq_last_iar;
static uint32_t g_irq_first_iar;

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
        g_irq_first_iar = iar;
        IRQ_LIVE("xnu_live_irq_handler_entered", g_irq_seq);
        IRQ_LIVE("xnu_live_irq_target", g_irq_args[0]);
        IRQ_LIVE("xnu_live_irq_refcon", g_irq_args[1]);
        IRQ_LIVE("xnu_live_irq_nub", g_irq_args[2]);
        IRQ_LIVE("xnu_live_irq_source", g_irq_args[3]);
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

    if (intid == STAGE90_GICC_SPURIOUS_ID) {
        /* No EOI: the architecture says a spurious read acknowledges nothing, so writing `EOIR`
         * with `0x3ff` is an acknowledgement of an interrupt that does not exist. */
        g_irq_spurious_count++;
        IRQ_LIVE("xnu_live_irq_spurious_count", g_irq_spurious_count);
        return;
    }

    /* Any other line: nothing in this image enabled one, so this is a fact about the machine and
     * not a case to service. EOI it so that it cannot be asserted straight back at us, record it,
     * and stop with the intid in the channel - which is what 308's run needed and did not have. */
    gicc_write(STAGE90_GICC_EOIR, iar);
    g_irq_other_count++;
    g_irq_last_iar = iar;
    IRQ_LIVE("xnu_live_irq_other_count", g_irq_other_count);
    IRQ_LIVE("xnu_live_irq_other_iar", iar);
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
static uint32_t g_irq_icfgr_word;
static uint32_t g_irq_icfgr_shift;

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
    g_irq_icfgr_word = icfgr;
    g_irq_icfgr_shift = shift;
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
