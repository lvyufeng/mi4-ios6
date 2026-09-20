/*
 * 481: the decrementer gets an owner - registered at Apple's own point in `arm_init`, over the
 * architected *virtual* timer, with the interrupt deliberately masked.
 *
 * ------------------------------------------------------------------------------------------------
 * The one missing line, and the three facts that say where it goes
 * ------------------------------------------------------------------------------------------------
 *
 * Apple's armv7 kernel gets its timer in exactly one way: `pexpert/arm/pe_identify_machine.c`'s
 * `pe_arm_init_timer` calls `ml_init_timebase(args, tbd_funcs, eoi_addr, eoi_value)` (`:666`), and
 * `osfmk/arm/machine_routines.c:436` copies that table into `rtclock_timebase_func`. Everything
 * downstream reads *that*: `cpu_timebase_init` (`osfmk/arm/cpu.c:452`) copies its three pointers
 * into `cpu_data`, and `ml_get_decrementer` / `ml_set_decrementer`
 * (`osfmk/arm/machine_routines_asm.s`) branch through them on every call.
 *
 * On this machine that call never happens, and the reason is one step stronger than "no branch
 * matches": the branches are `#if defined(ARM_BOARD_CLASS_S5L8960X)` / `T7000` / `S7002` / `S8000` /
 * `T8002` / `T8010` / `T8011`, and this configuration defines **none** of them (no `ARM_BOARD_CONFIG_*`
 * is set, so `pexpert/pexpert/arm/board_config.h` defines no class). The preprocessor therefore
 * removes every `if (!strcmp(gPESoCDeviceType, ...)) ... else` arm of the chain and leaves
 * `return 0;` at `:663` as the function's *first* statement after the two assignments - so the
 * `ml_init_timebase` call at `:666` is unreachable by construction, at `-O2` it is not emitted at
 * all, and the measurement is `arm-none-eabi-nm -u` on
 * `out/xnu_obj/pexpert_arm_pe_identify_machine.o`: **no undefined `ml_init_timebase`**, which is what
 * `tools/check_timebase_registration.py` re-reads before every link. (`ARMA7` is defined by that same
 * build, which is why `__ARM_TIME_TIMEBASE_ONLY__` is 1 and `mach_absolute_time` has been reading
 * `CNTPCT` since long before this step - the two come from different places in this tree.)
 *
 * So `rtclock_timebase_func` stays zero for the whole boot, and the measured consequences are both
 * already in this project's logs: `ml_set_decrementer` takes its `#else` path (the assembled body is
 * `ldr r2, [r3, #112]` / `cmp r2, #0` / `bxne r2`, then `msr CPSR_c, #0xd1` and `str ip, [r8, #104]` -
 * with the pointer NULL it stores the value into `cpu_data->cpu_decrementer` in FIQ mode and programs
 * no hardware at all), and 57 threads sit parked on wakeups whose deadlines are queue entries with
 * nothing behind them. That is the whole of 480's "the timer is the only thing between this run and
 * the drivers". The one number to carry forward: that `#else` path is what leaves
 * `cpu_decrementer == 0x7FFFFFFF` in `cpu_data`, because `cpu_timebase_init` sets exactly that
 * (`osfmk/arm/cpu.c:465`) and nothing moves it again - so the read-back below has a *value* to
 * predict, not merely a non-zero.
 *
 * **Where the call has to go, read out of the linked image rather than out of the source.** In this
 * image `arm_init` is:
 *
 *     80007b50  bl  PE_init_platform          <- arm_init.c:383, PE_init_platform(TRUE, &BootCpuData)
 *     80007b58  bl  cpu_timebase_init         <- arm_init.c:384, the copy into cpu_data
 *     80007b60  bl  fiq_context_init          <- arm_init.c:385, which loads the FIQ bank from it
 *
 * so `ml_init_timebase` must run **before** `80007b58`. Apple's own code does it from inside the
 * call at `80007b50` (`pe_init.c:313 pe_arm_init_interrupts(args)`), and that call site is the hook
 * this step uses: **`--wrap=PE_init_platform`**, whose wrapper registers the timebase and then calls
 * the real function. Nothing in Apple's tree is edited, the ordering is the one the tree already
 * has, and the wrapper is the same mechanism `--wrap=getpid`/`mmap` used in 479/480 to sit in the
 * table's own slot rather than at a call site.
 *
 * The wrapper is discriminating, and the discrimination is checkable: the second argument of the
 * registering call is `&BootCpuData`, which is exactly the value `ml_init_timebase`'s own guard
 * compares against (`cpu_data_ptr == &BootCpuData`, `machine_routines.c:447`). `arm_init` makes
 * three `PE_init_platform` calls in all - `(FALSE, args)` at `80007888`, `(TRUE, &BootCpuData)` at
 * `80007b50`, `(TRUE, NULL)` at arm_init.c:463 - and `arm_init_cpu` a fourth, `(TRUE, &CpuData[n])`
 * at `80007c30`. Only one of them has both `vm_initialized == TRUE` and `args == &BootCpuData`, and
 * the log records `args` so that the equality is a reading and not an assumption.
 *
 * ------------------------------------------------------------------------------------------------
 * What the owner is: the virtual timer, masked on purpose
 * ------------------------------------------------------------------------------------------------
 *
 * `tbd_ops` is three functions (`osfmk/arm/machine_routines.h:242`), and this file's table gives all
 * three:
 *
 *   - `stage90_tbd_get_decrementer` reads `CNTV_TVAL`;
 *   - `stage90_tbd_set_decrementer` writes `CNTV_TVAL` and keeps `CNTV_CTL` enabled **and masked**;
 *   - `stage90_tbd_fiq_handler` records and returns. It is inert on this machine and it is here so
 *     the table has no NULL in it: the FIQ path is the one experiment 143 measured MSM8974 does not
 *     have, and this image's vector page puts Apple's decrementer handler in the FIQ slot - see
 *     `entry_timebase.h`, which is where the *choice of timer* and the *mask bit* are argued.
 *
 * **Why the virtual timer and not the physical one** is in `entry_timebase.h`; the short form is
 * that the payload owns `CNTP` (the log's `watchdog_cntp_ctl_armed` - one of the two recovery nets)
 * and that `CNTV_TVAL` is the register Apple's own `__ARM_TIME__` code writes.
 *
 * **The mask bit is the step's decision and it is what makes the step safe.** With
 * `CNTV_CTL.IMASK` set, the deadline is programmable and the countdown runs and *reaches zero*, and
 * no interrupt is asserted - so a wrong deadline can be measured, and cannot stop the boot. The
 * alternative was measured in advance and is not available: 143's run shows no FIQ is delivered on
 * this SoC, and the IRQ slot holds Apple's generic handler, which calls `cpu_data`'s
 * `interrupt_handler` - NULL here - through `blx r5`. **482 owns that routing decision** and cannot
 * take it until the arrival of an interrupt is observable.
 *
 * ------------------------------------------------------------------------------------------------
 * What the run is read for
 * ------------------------------------------------------------------------------------------------
 *
 * Four readings, each of which is a different *kind* of claim:
 *
 *   - **the registration happened at the registering call**: `xnu_entry_timebase_registered = 1`,
 *     `_args = the address of BootCpuData`, `_vm_init = 1`, `_seq = which PE_init_platform call it
 *     was`, and the three function addresses this file's table holds, so the log names the owner;
 *   - **the copy reached `cpu_data`**: at the *first* `fiq_context_init` - the instruction after
 *     `cpu_timebase_init` - the four words at `entry_timebase.h`'s offsets are reported
 *     (`_cpu_get_dec`, `_cpu_set_dec`, `_cpu_fiq`, `_cpu_dec`). This is what says the table did not
 *     merely get filled, it got *installed*, and it is the reading that would be missing if
 *     `cpu_get_fiq_handler` had been non-NULL at that moment (the copy is guarded on it);
 *   - **the kernel's own deadline machinery reached the owner**: `--wrap=setPop` records the
 *     deadline the kernel asked for and the decrementer value it computed, and
 *     `stage90_tbd_set_decrementer` records the value it was *handed*. A `setPop` call with a
 *     non-zero value and a writer that received that same number is a chain, not an inference;
 *   - **the hardware counted**: on the first write the timer is given a known value
 *     (`STAGE90_CNTV_SAMPLE_TICKS`), read back twice with a bounded spin between, and both the
 *     register and the free-running counter (`CNTVCT`) are reported at each end. A `TVAL` that went
 *     *down* by about what `CNTVCT` went *up* is the measurement; a `TVAL` that did not move would
 *     say the timer is not enabled, which is the failure this reading exists to catch.
 *
 * **And the control is the console.** Nothing here changes what the kernel does - the same deadlines
 * are armed, the same threads park - so 481's run should have 480's console byte for byte, and any
 * difference is a reading about the timer rather than about the fixture. That is stated here because
 * it is the one thing this step cannot prove from its own numbers.
 */

#include <stdint.h>

#include "entry_timebase.h"
#include "entry_gic.h"

/* Set by `build_entry.sh` from `STAGE90_ENTRY_TRACE`; the live channel exists only in a traced
 * build, and `xnu_entry_timebase_traced` carries the value into the report so a reader never has to
 * guess whether a zero `setPop` count means "never armed" or "not instrumented". */
#ifndef STAGE90_ENTRY_TIMEBASE_TRACED
#define STAGE90_ENTRY_TIMEBASE_TRACED 0
#endif

#if STAGE90_ENTRY_TIMEBASE_TRACED
extern void entry_live_write(const char *key, uint32_t value);
#define TB_LIVE(key, value) entry_live_write((key), (uint32_t)(value))
#else
#define TB_LIVE(key, value) do { (void)(key); (void)(value); } while (0)
#endif

/* The report buffer, which exists in every build. */
extern void entry_write_kv(const char *key, uint32_t value);

/* The boot processor's `struct cpu_data`, which `arm_init` hands to `PE_init_platform` and which
 * `ml_init_timebase` compares its own first argument against. The *address* is the whole point of
 * the symbol here, so it is declared as an array of unknown size. */
extern char BootCpuData[];

/* `osfmk/arm/machine_routines.c:436`. `tbd_ops_t` is this, spelled out because this image cannot
 * include Apple's header: `struct tbd_ops` (`machine_routines.h:242`) is these three members, in
 * this order, and `tools/check_timebase_registration.py` compares the definitions below against
 * that declaration rather than against a comment. */
typedef struct {
    void (*tbd_fiq_handler)(void);
    uint32_t (*tbd_get_decrementer)(void);
    void (*tbd_set_decrementer)(uint32_t dec_value);
} stage90_tbd_ops_t;

extern void ml_init_timebase(void *args, const stage90_tbd_ops_t *tbd_funcs,
                             uint32_t int_address, uint32_t int_value);

/* The two functions this file wraps. `boolean_t` is a 32-bit unsigned word on this target and both
 * wrappers pass it through unchanged, so the parameter is spelled as the word it is. */
extern void __real_PE_init_platform(uint32_t vm_initialized, void *args);
extern void __real_fiq_context_init(uint32_t enable_fiq);

/* How long the first write's countdown sample is given. At the 19.2 MHz the payload measured
 * (`watchdog_hz = 0x7ffd`) one microsecond is ~19 ticks and this loop is a few hundred - i.e. a few
 * thousand ticks - which is small enough not to matter inside `splclock` and large enough that the
 * two `CNTV_TVAL` reads cannot agree by rounding. */
#define STAGE90_CNTV_SAMPLE_TICKS  0x00100000u
#define STAGE90_CNTV_SPIN          20000u

/* --------------------------------------------------------------------------------------------- */
/* The architectural counter pair, and why these four instructions are the ones to compare        */
/* --------------------------------------------------------------------------------------------- */

/*
 * `CNTV_TVAL` is `c14, c3, 0` and `CNTV_CTL` is `c14, c3, 1`; Apple's own `__ARM_TIME__` code
 * writes exactly those two (`machine_routines_asm.s`, `ml_set_decrementer` and `fiq_context_init`),
 * and the check compares these lines against that file. `CNTVCT` is the *virtual* free-running
 * counter, `mrrc p15, 1, lo, hi, c14` - the same instruction Apple's `ml_get_timebase` uses for
 * `CNTPCT` with opc1 0, and the mirror is the check: the two counters differ in that one field.
 */
uint32_t stage90_cntv_tval_read(void)
{
    uint32_t value;

    __asm__ volatile ("mrc p15, 0, %0, c14, c3, 0" : "=r" (value));
    return value;
}

void stage90_cntv_tval_write(uint32_t value)
{
    __asm__ volatile ("mcr p15, 0, %0, c14, c3, 0" :: "r" (value) : "memory");
}

uint32_t stage90_cntv_ctl_read(void)
{
    uint32_t value;

    __asm__ volatile ("mrc p15, 0, %0, c14, c3, 1" : "=r" (value));
    return value;
}

void stage90_cntv_ctl_write(uint32_t value)
{
    __asm__ volatile ("mcr p15, 0, %0, c14, c3, 1" :: "r" (value) : "memory");
}

uint64_t stage90_cntvct_read(void)
{
    uint32_t lo, hi;

    __asm__ volatile ("mrrc p15, 1, %0, %1, c14" : "=r" (lo), "=r" (hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/* --------------------------------------------------------------------------------------------- */
/* The owner                                                                                      */
/* --------------------------------------------------------------------------------------------- */

static uint32_t g_dec_writes;             /* every call `ml_set_decrementer` made into the table */
static uint32_t g_dec_first_value = 0xFFFFFFFFu;
static uint32_t g_dec_first_readback;
static uint32_t g_dec_first_ctl;
/* 483: the nearest deadline the kernel ever armed, in decrementer counts, and on which call. The
 * first pop of the boot saturates to `DECREMENTER_MAX`; this is how the run says whether anything
 * nearer followed, i.e. whether an interrupt was *due* inside the window at all. */
static uint32_t g_dec_min_value = 0xFFFFFFFFu;
/*
 * **484: the other half of `g_dec_min_value`, and why the other half is not a maximum.** 483's
 * write-up says the kernel's clock "runs": `dec_min` fell to `0x4ab8` and stayed there for four
 * thousand armings. The write-up then states the consequence in its own words - "`dec_min` never left
 * `0x4ab6`-`0x4ab9` across 4096 calls" - and that sentence is a claim about the *range* of the 4096
 * values, which the instrument that produced the number cannot support: `dec_min` only ever falls, so
 * "every arming was `0x4ab8`" and "every arming was *at least* `0x4ab8`, and most were far more" print
 * the same log. The obvious repair - publish the maximum too - does not work either, and the reason is
 * in the first call: call 1 carries `EndOfAllTime` and `deadline_to_decrementer` saturates it to
 * `DECREMENTER_MAX`, so the maximum over the run is `0x7FFFFFFF` by construction and separating it out
 * would only move the same defect to the other end.
 *
 * What separates "a clock nothing waits on" from "a fixture that never asks to wait" is neither bound.
 * It is **repetition**: whether the value the kernel arms is the *same number* on every call, and if
 * not, which numbers ever appeared and on which call. So call 2's value is taken as the reference -
 * call 1 is the saturated one and is excluded for the reason above - every later call is counted as a
 * repeat of that reference or as something else, and the first `STAGE90_DEC_SHOWN` *distinct*
 * something-elses are published with the call number they first appeared on. A run whose census ends
 * with `other == 0` armed one number, `g_dec_ref_value`, on every call from the second onwards: a
 * metronome, and `xnu_live_dec_new_value` never appears in the log at all. A run that armed two or
 * three numbers has the first non-repeating deadline in the log by construction, at `_new_call`.
 *
 * The counts and the table are separate because they answer different questions and only one of them
 * needs to be bounded: `same`/`other` are running totals over all 4096 calls, and the table holds the
 * first eight *distinct* values so that a metronome with drift cannot fill the log with one record per
 * call. Once the table is full a further distinct value is counted in `g_dec_other_more` and only the
 * first of those is published - the census stays finite whether the kernel armed two values or four
 * thousand, which is the property the log's size depends on.
 */
#define STAGE90_DEC_SHOWN 8u
static uint32_t g_dec_ref_value;
static uint32_t g_dec_ref_valid;
static uint32_t g_dec_same;
static uint32_t g_dec_other;
static uint32_t g_dec_distinct;
static uint32_t g_dec_other_more;
static uint32_t g_dec_shown_value[STAGE90_DEC_SHOWN];
static uint32_t g_dec_shown_call[STAGE90_DEC_SHOWN];
static uint32_t g_dec_shown_count[STAGE90_DEC_SHOWN];
/* 483: 0 until `entry_irq_arm()` has said the line is enabled and the installer's read-back agreed.
 * Non-zero is the permission to write `CNTV_CTL` without `IMASK`. */
static uint32_t g_irq_unmasked;
static uint32_t g_cntv_tval_a, g_cntv_tval_b;
static uint32_t g_cntvct_a_lo, g_cntvct_b_lo;
static uint32_t g_cntv_spin = STAGE90_CNTV_SPIN;

/*
 * **This is the function the kernel calls from inside `splclock`, and it is the first time this
 * image programs a timer.** The order matters and is: enable-and-mask, sample, then program what the
 * kernel asked for. The sample is taken with the timer's own register, so the value it observes is
 * not the kernel's deadline and cannot be confused with it.
 */
static void stage90_tbd_set_decrementer(uint32_t dec_value)
{
    if (g_dec_writes == 0u) {
        uint32_t spin;

        /* Enable the countdown and mask its interrupt in one write: `CNTV_CTL.ENABLE` is what makes
         * `CNTV_TVAL` decrement at all, and `IMASK` is what keeps a deadline from becoming an
         * interrupt on a machine whose only installed handler is in the dead FIQ slot. */
        stage90_cntv_ctl_write(STAGE90_CNTV_ARM_MASK);
        g_dec_first_ctl = stage90_cntv_ctl_read();

        stage90_cntv_tval_write(STAGE90_CNTV_SAMPLE_TICKS);
        g_cntv_tval_a = stage90_cntv_tval_read();
        g_cntvct_a_lo = (uint32_t)stage90_cntvct_read();

        for (spin = 0u; spin < STAGE90_CNTV_SPIN; spin++)
            __asm__ volatile ("");

        g_cntv_tval_b = stage90_cntv_tval_read();
        g_cntvct_b_lo = (uint32_t)stage90_cntvct_read();

        TB_LIVE("xnu_live_cntv_tval_a", g_cntv_tval_a);
        TB_LIVE("xnu_live_cntv_tval_b", g_cntv_tval_b);
        TB_LIVE("xnu_live_cntvct_a", g_cntvct_a_lo);
        TB_LIVE("xnu_live_cntvct_b", g_cntvct_b_lo);
        TB_LIVE("xnu_live_dec_ctl", g_dec_first_ctl);

        /*
         * 482: and then the question this sample cannot answer - *which* interrupt line the countdown
         * that just demonstrably ran appears on. The call is here rather than at the registration
         * because it needs a countdown it has measured itself, and here rather than after the write
         * below because the probe parks `CNTV_TVAL` and `CNTV_CTL` and the kernel's own deadline must
         * be the last thing programmed. `entry_gic.c` carries the three reasons nothing can be
         * delivered while it runs.
         */
        entry_gic_probe();
    }

    stage90_cntv_tval_write(dec_value);

    g_dec_writes++;
    if (g_dec_writes == 1u) {
        g_dec_first_value = dec_value;
        g_dec_first_readback = stage90_cntv_tval_read();
        TB_LIVE("xnu_live_dec_written", g_dec_first_value);
        TB_LIVE("xnu_live_dec_readback", g_dec_first_readback);
    } else if ((g_dec_writes & (g_dec_writes - 1u)) == 0u) {
        TB_LIVE("xnu_live_dec_count", g_dec_writes);
    }

    /*
     * **483: the arming and the unmask, both after `g_dec_writes++` - and that order is the point.**
     *
     * `entry_irq_arm()` installs the second-level handler into `cpu_data`, reads `GICD_ICFGR` for the
     * line 482 measured, clears its pending bit and (when the switch says so) enables it at the
     * distributor; it returns 1 only if the installer's read-back agreed, and 0 leaves the countdown
     * masked, which is 482's state exactly - so "the machine was not ready" cannot be confused with
     * "the step did not run" in the log.
     *
     * **Why the call cannot be earlier.** `ml_install_interrupt_handler` ends in
     * `initialize_screen(NULL, kPEAcquireScreen)` (`osfmk/arm/machine_routines.c:420`), and that is
     * not a store: it walks the video-console path, which can reach `setPop` and so this function
     * again. If that re-entrant call arrived while `g_dec_writes` was still 0, its first-call branch
     * would be open and it would run **482's probe** - inside the arming, with this image's handler
     * already installed and `g_irq_armed` already set. `entry_gic_probe` drives its own countdowns and
     * rewrites the CPU interface's control and priority registers before restoring them, which is a
     * second definition of the state `entry_irq_arm` is in the middle of establishing. One increment
     * earlier and the branch is closed: `g_dec_writes == 1` on re-entry, no sample, no probe.
     *
     * `g_irq_armed` is the second guard for the same shape - a re-entrant `entry_irq_arm` returns
     * `g_irq_enabled_line`, which is still 0 at that point, so it cannot unmask either. Two guards for
     * one window, because the window is the step's whole risk.
     */
    if (g_irq_unmasked == 0u)
        g_irq_unmasked = entry_irq_arm();

    if (g_irq_unmasked != 0u) {
        /*
         * `ENABLE` without `IMASK`, which is the whole change 483 makes to the timer: the same
         * countdown, the same compare value, and the one bit that lets it reach the CPU interface.
         * It is written **after** `CNTV_TVAL` above - the deadline is programmed while the line is
         * still masked and the mask comes off with the deadline already loaded, so the shortest
         * possible window between "this deadline is armed" and "this deadline can fire" is the
         * distance between two stores rather than between a store and the kernel's next call.
         */
        stage90_cntv_ctl_write(STAGE90_CNTV_CTL_ENABLE);
    }

    /*
     * How near the nearest deadline ever was, on every call and not only on the powers of two. This
     * is the number that decides whether a run can *see* an interrupt at all: the first `setPop` of
     * the boot carries `EndOfAllTime` and saturates to `DECREMENTER_MAX` (~112 s at 19.2 MHz), so if
     * every later pop is also far, "armed and no interrupt arrived" is a fact about the kernel's
     * deadlines and not about the handler - and the two are only distinguishable if the minimum and
     * the call it happened on are in the log.
     */
    if (dec_value < g_dec_min_value) {
        g_dec_min_value = dec_value;
        TB_LIVE("xnu_live_dec_min", g_dec_min_value);
        TB_LIVE("xnu_live_dec_min_count", g_dec_writes);
    }

    /*
     * 484: the census, on the same call and after the minimum so that the two readings of a run are
     * ordered in the log the way they are derived here. `g_dec_writes` has already been incremented,
     * so this is call number `g_dec_writes` and the guard below is what excludes call 1 - the
     * saturated `DECREMENTER_MAX` arming that `deadline_to_decrementer` produces from `EndOfAllTime`
     * and that would otherwise become the reference every real deadline was measured against.
     */
    if (g_dec_writes >= 2u) {
        if (g_dec_ref_valid == 0u) {
            g_dec_ref_valid = 1u;
            g_dec_ref_value = dec_value;
            TB_LIVE("xnu_live_dec_ref", g_dec_ref_value);
            TB_LIVE("xnu_live_dec_ref_call", g_dec_writes);
        } else if (dec_value == g_dec_ref_value) {
            g_dec_same++;
            if ((g_dec_same & (g_dec_same - 1u)) == 0u) {
                TB_LIVE("xnu_live_dec_same", g_dec_same);
                TB_LIVE("xnu_live_dec_other", g_dec_other);
            }
        } else {
            uint32_t i;

            g_dec_other++;

            for (i = 0u; i < g_dec_distinct; i++) {
                if (g_dec_shown_value[i] == dec_value) {
                    break;
                }
            }

            if (i < g_dec_distinct) {
                /* A value already in the table, armed again. The count is published on its own powers
                 * of two, which is what turns "there were two values" into "and here is how often the
                 * second one recurred" - the difference between a metronome that drifted once and a
                 * clock that alternates. */
                g_dec_shown_count[i]++;
                if ((g_dec_shown_count[i] & (g_dec_shown_count[i] - 1u)) == 0u) {
                    TB_LIVE("xnu_live_dec_seen_value", dec_value);
                    TB_LIVE("xnu_live_dec_seen_count", g_dec_shown_count[i]);
                }
            } else if (g_dec_distinct < STAGE90_DEC_SHOWN) {
                /* **The record this whole step exists for.** The first arming of a value the kernel
                 * had not armed before, with the call it happened on - which is the log's answer to
                 * "which deadline was not a repeat" and the only place a reader can see it. */
                g_dec_shown_value[g_dec_distinct] = dec_value;
                g_dec_shown_call[g_dec_distinct] = g_dec_writes;
                g_dec_shown_count[g_dec_distinct] = 1u;
                g_dec_distinct++;
                TB_LIVE("xnu_live_dec_new_value", dec_value);
                TB_LIVE("xnu_live_dec_new_call", g_dec_writes);
                TB_LIVE("xnu_live_dec_distinct", g_dec_distinct);
            } else {
                g_dec_other_more++;
                if (g_dec_other_more == 1u) {
                    TB_LIVE("xnu_live_dec_more_value", dec_value);
                    TB_LIVE("xnu_live_dec_more_call", g_dec_writes);
                    TB_LIVE("xnu_live_dec_more", g_dec_other_more);
                }
            }
        }
    }
}

static uint32_t stage90_tbd_get_decrementer(void)
{
    return stage90_cntv_tval_read();
}

/*
 * **Inert on this machine, and in the table so that it has no NULL in it.** Apple's `fleh_fiq_generic`
 * is the reader: `fiq_context_init` loads this address into the FIQ bank's `r9` and the vector calls
 * it as the FIQ's "clear the source" step. 143's run measured that no FIQ reaches non-secure PL1 on
 * MSM8974 even with the Group 0 configuration accepted at both ends and the timer firing, so this
 * function is expected never to be entered - and it records if it is, because "expected never" and
 * "never" are different claims and only one of them is free.
 */
static void stage90_tbd_fiq_handler(void)
{
    static uint32_t entered;

    entered++;
    TB_LIVE("xnu_live_fiq_handler_entered", entered);
}

static const stage90_tbd_ops_t stage90_tbd_ops = {
    stage90_tbd_fiq_handler,
    stage90_tbd_get_decrementer,
    stage90_tbd_set_decrementer
};

/* --------------------------------------------------------------------------------------------- */
/* The registration                                                                               */
/* --------------------------------------------------------------------------------------------- */

static uint32_t g_pe_calls;
static uint32_t g_registered;
static uint32_t g_reg_args;
static uint32_t g_reg_vm_init;
static uint32_t g_reg_seq;
static uint32_t g_fiq_ctx_calls;

static uint32_t g_cpu_get_dec = 0xFFFFFFFFu;
static uint32_t g_cpu_set_dec = 0xFFFFFFFFu;
static uint32_t g_cpu_fiq = 0xFFFFFFFFu;
static uint32_t g_cpu_dec = 0xFFFFFFFFu;

/*
 * **`int_address`/`int_value` are passed as zero, and that is the honest value rather than a
 * placeholder.** On Apple those two words are the *interrupt acknowledge* pair - `pe_arm_init_timer`
 * gives `pic_base` and the timer's bit in the AIC's interrupt-status register, and
 * `fleh_fiq_generic` consumes them as `str r11, [r10]`. On this machine that interrupt is not
 * routed yet, so its acknowledge address and its number are not known, and 482 is the step that
 * measures them. Writing a guess here would put an unmeasured number in the image at exactly the
 * place a later step would read it as a measurement; zero is a value a reader can distinguish.
 */
static void stage90_timebase_register(void *args)
{
    g_registered = 1u;
    g_reg_args = (uint32_t)(uintptr_t)args;

    TB_LIVE("xnu_live_timebase_registered", g_registered);
    TB_LIVE("xnu_live_timebase_args", g_reg_args);
    TB_LIVE("xnu_live_timebase_seq", g_reg_seq);
    TB_LIVE("xnu_live_timebase_get_dec", (uint32_t)(uintptr_t)&stage90_tbd_get_decrementer);
    TB_LIVE("xnu_live_timebase_set_dec", (uint32_t)(uintptr_t)&stage90_tbd_set_decrementer);
    TB_LIVE("xnu_live_timebase_fiq", (uint32_t)(uintptr_t)&stage90_tbd_fiq_handler);

    ml_init_timebase(args, &stage90_tbd_ops, 0u, 0u);
}

void __wrap_PE_init_platform(uint32_t vm_initialized, void *args)
{
    g_pe_calls++;

    if ((g_registered == 0u) && (vm_initialized != 0u) && (args == (void *)BootCpuData)) {
        g_reg_seq = g_pe_calls;
        g_reg_vm_init = vm_initialized;
        stage90_timebase_register(args);
    }

    __real_PE_init_platform(vm_initialized, args);
}

/*
 * **The copy, read at the one instruction where it can be read.** `cpu_timebase_init` runs between
 * the registering `PE_init_platform` and the first `fiq_context_init` (the image's own order:
 * `80007b50`, `80007b58`, `80007b60`), and it copies the table into `cpu_data` **only if
 * `cpu_get_fiq_handler` is still NULL** - the guard is Apple's, and this reading is what says
 * whether it held. The four words are read *before* the real call, i.e. at the state
 * `cpu_timebase_init` left, which is the state every later `ml_set_decrementer` will branch on.
 */
void __wrap_fiq_context_init(uint32_t enable_fiq)
{
    g_fiq_ctx_calls++;

    if (g_fiq_ctx_calls == 1u) {
        g_cpu_get_dec = *(volatile uint32_t *)((uintptr_t)BootCpuData + STAGE90_CPU_GET_DECREMENTER_FUNC);
        g_cpu_set_dec = *(volatile uint32_t *)((uintptr_t)BootCpuData + STAGE90_CPU_SET_DECREMENTER_FUNC);
        g_cpu_fiq     = *(volatile uint32_t *)((uintptr_t)BootCpuData + STAGE90_CPU_GET_FIQ_HANDLER);
        g_cpu_dec     = *(volatile uint32_t *)((uintptr_t)BootCpuData + STAGE90_CPU_DECREMENTER);

        TB_LIVE("xnu_live_timebase_cpu_get_dec", g_cpu_get_dec);
        TB_LIVE("xnu_live_timebase_cpu_set_dec", g_cpu_set_dec);
        TB_LIVE("xnu_live_timebase_cpu_fiq", g_cpu_fiq);
        TB_LIVE("xnu_live_timebase_cpu_dec", g_cpu_dec);
        TB_LIVE("xnu_live_timebase_fiq_ctx_calls", g_fiq_ctx_calls);
    }

    __real_fiq_context_init(enable_fiq);
}

/* --------------------------------------------------------------------------------------------- */
/* The kernel's own deadline machinery, which is the thing all of the above exists to serve       */
/* --------------------------------------------------------------------------------------------- */

static uint32_t g_setpop_calls;
static uint64_t g_setpop_first_deadline;
static uint32_t g_setpop_first_returned;
static uint32_t g_setpop_first_deadline_lo;
static uint32_t g_setpop_first_deadline_hi;

/* Called from `entry_trace.c`'s `--wrap=setPop`, i.e. from inside `timer_resync_deadlines` - the
 * function that decides the nearest deadline and asks for it. `returned` is what the real `setPop`
 * computed, which is the decrementer value the kernel believes it programmed; the record of what
 * the *writer* received is `g_dec_first_value`, and the two agreeing is the chain. */
void entry_timebase_note_setpop(uint64_t deadline, uint32_t returned)
{
    g_setpop_calls++;

    if (g_setpop_calls == 1u) {
        g_setpop_first_deadline = deadline;
        g_setpop_first_returned = returned;
        g_setpop_first_deadline_lo = (uint32_t)deadline;
        g_setpop_first_deadline_hi = (uint32_t)(deadline >> 32);

        TB_LIVE("xnu_live_setpop_deadline_lo", g_setpop_first_deadline_lo);
        TB_LIVE("xnu_live_setpop_deadline_hi", g_setpop_first_deadline_hi);
        TB_LIVE("xnu_live_setpop_returned", g_setpop_first_returned);
    } else if ((g_setpop_calls & (g_setpop_calls - 1u)) == 0u) {
        TB_LIVE("xnu_live_setpop_count", g_setpop_calls);
    }
}

/* --------------------------------------------------------------------------------------------- */
/* 484: who arms the decrementer                                                                  */
/* --------------------------------------------------------------------------------------------- */

/*
 * **The census above says *what* the kernel armed. This says *who* asked for it.**
 *
 * `setPop` (`osfmk/arm/rtclock.c:344`) is one function with two callers - `timer_resync_deadlines`
 * (`arm_timer.c:190`, which is every timer interrupt plus every re-arming) and `ClearIdlePop`
 * (`rtclock.c:415`) - and the deadline it is handed is the *result* of a decision made in the caller:
 * `timer_resync_deadlines` picks the nearest of three absolute times held in `struct cpu_data`
 * (`rtclock_timer.deadline`, `idle_timer_deadline`, `quantum_timer_deadline`), each with a `> 0`
 * guard, and only calls `setPop` at all if the winner is non-zero or `EndOfAllTime`. The winner's
 * *identity* is therefore not in the value `setPop` receives, and it is the identity that decides how
 * 484 is read: `quantum_timer_deadline` is the scheduler's preemption timer, which a compute-bound
 * thread re-arms on every quantum expiry and which nothing *waits* on; the other two are real
 * deadline requests from the kernel's timer queue, which something waits on. The same number - a
 * quarter of a millisecond, a millisecond, ten - means opposite things depending on which field it
 * came out of.
 *
 * **Two ways to read the field, and why this is the other one.** The direct way is to read the three
 * fields out of `BootCpuData` at the moment of the arming and publish all four numbers. It needs the
 * three offsets in `struct cpu_data`, and those are not generated for this image: the `assym.s` this
 * project builds carries only the names Apple's own assembly references, and none of the three is one
 * of them. A hand-computed offset is exactly the defect this project has recorded most often - a
 * wrong offset reads a *neighbouring* field and publishes a plausible wrong number - so it would have
 * to be verified against `arm_timer.o`'s displacements before it could be believed, which is a
 * generator and a check of its own.
 *
 * The way taken instead names the arm-er at the *entry* to the field each time, which needs no offset
 * at all because each setter is a function with a name of its own:
 *
 *     1 `timer_call_enter`            - a timer queue entry, the deadline it was given
 *     2 `timer_call_enter_with_leeway`- the same, with a leeway
 *     3 `timer_call_quantum_timer_enter` - the one caller of `quantum_timer_set_deadline`
 *
 * **The family has four members and this list has three, and the missing one is a finding rather than
 * an omission.** `timer_call_enter1` is declared beside the other three in `osfmk/kern/timer_call.h`,
 * and its callers in this tree are `osfmk/kern/sfi.c` (seven sites) and `bsd/dev/dtrace/dtrace_glue.c`
 * (three) - Selective Forced Idle, which is not compiled for ARM, and dtrace, which is not in this
 * configuration. So the symbol is *defined* in this image (`osfmk/kern/timer_call.c` defines it) and
 * nothing references it, which the linker reports in the one way that matters: a `--wrap` on a name
 * with no reference produces no reachable wrapper, and `build_entry.sh`'s own reachability check
 * refuses the build for exactly that reason (it did, on this step's first build, listing
 * `timer_call_enter1` and `thread_quantum_expire`). The number 2 is kept as the leeway form's rather
 * than renumbering, because these numbers are what a *log* says and a reader comparing this step's
 * comment with its records should not have to know that one was removed.
 *
 * Sources 1 and 2 are the etimer queue - everything that ends up in `rtclock_timer.deadline`, since
 * `timer_set_deadline` itself has no caller anywhere in this tree (grep the source: the only
 * references are its definition, its declaration in `timer_queue.h`, and the i386 copy) and
 * `timer_queue_expire` is the only writer of that field. Source 3 is the quantum. The third field,
 * `idle_timer_deadline`, has no setter at all on this machine: it is written only by `timer_intr`'s
 * own idle notify, and nothing registers one, so a run in which it is ever the winner would have to
 * be explained by something this list does not contain.
 *
 * **`timer_call_setup` is here to make the entries *nameable*, and it is the reason the join is
 * possible at all.** The `call` pointer is the identity of a timer, but three of the four families
 * are embedded in structures that the heap allocates (`thread->wait_timer`, `thread->depress_timer`,
 * a thread_call group's `delayed_timers`), so the pointer resolves to no symbol in the image and the
 * log entry would say only "something was armed". `timer_call_setup` is called exactly once per timer,
 * at initialization, with the callback as its second argument - so publishing *every* `setup` call
 * (there are about fifteen) gives the reader a table to join the `call` pointers against, and the
 * function that gets armed is then a name. The publication is unconditional for that reason and not on
 * the powers of two the counters use: a `setup` that was not published is a `call` pointer that can
 * never be resolved, so a sampled table would be a table with holes in it.
 *
 * **The bounds are the other half of the design.** `entry_timer_enter` can be on the per-syscall path
 * (`syscall_subr.c`'s depress timer is armed from the syscall return), and a run of this boot makes
 * eight million `getpid` calls; an instrument that published each one would put millions of records
 * into a log whose interesting part is four thousand armings, and one that *did* work per call would
 * perturb what it measures. So the first `STAGE90_TMR_ENTER_SHOWN` events are published in full and
 * the rest are counted, on the powers of two, into an overflow key - the census is complete as a
 * *count* at any volume and complete as a *record* for the window that contains the metronome, which
 * starts at arming 15 of the run and is therefore inside the window by construction.
 */
#define STAGE90_TMR_SETUP_SHOWN   24u
#define STAGE90_TMR_ENTER_SHOWN   64u
#define STAGE90_TMR_QEXPIRE_SHOWN 16u
#define STAGE90_TMR_SRC_MAX       8u
static uint32_t g_tmr_setup_n;
static uint32_t g_tmr_setup_over;
static uint32_t g_tmr_enter_n;
static uint32_t g_tmr_enter_over[STAGE90_TMR_SRC_MAX];
static uint32_t g_tmr_enter_over_all;
static uint32_t g_tmr_src_calls[STAGE90_TMR_SRC_MAX];
static uint32_t g_tmr_qexp_n;
static uint32_t g_tmr_qexp_over;

/* Called from `entry_trace.c`'s `--wrap=timer_call_setup`, once per timer in the kernel. `func` is
 * the callback that will run when that timer fires and `param0` is the object it was set up for -
 * which for three of the four families is the thread or the processor the timer belongs to, so the
 * pair names both the code and the owner. */
void entry_timebase_note_timer_setup(uint32_t call, uint32_t func, uint32_t param0)
{
    g_tmr_setup_n++;

    if (g_tmr_setup_n <= STAGE90_TMR_SETUP_SHOWN) {
        TB_LIVE("xnu_live_tmr_setup_seq", g_tmr_setup_n);
        TB_LIVE("xnu_live_tmr_setup_call", call);
        TB_LIVE("xnu_live_tmr_setup_func", func);
        TB_LIVE("xnu_live_tmr_setup_param", param0);
    } else {
        g_tmr_setup_over++;
        if (g_tmr_setup_over == 1u) {
            TB_LIVE("xnu_live_tmr_setup_over", g_tmr_setup_over);
        }
    }
}

/* Called from the three `timer_call_enter` wrappers and from the quantum timer's own entry point.
 * `source` is the number documented above the state; `deadline` is the absolute time the caller asked
 * for, and `now` is read here rather than in the wrapper so that the delta and the timestamp are the
 * same instant. */
void entry_timebase_note_timer_enter(uint32_t source, uint32_t call, uint64_t deadline,
                                     uint32_t flags)
{
    uint32_t now;

    g_tmr_enter_n++;

    if (source < STAGE90_TMR_SRC_MAX) {
        g_tmr_src_calls[source]++;
    }

    if (g_tmr_enter_n <= STAGE90_TMR_ENTER_SHOWN) {
        now = (uint32_t)stage90_cntvct_read();
        TB_LIVE("xnu_live_tmr_enter_seq", g_tmr_enter_n);
        TB_LIVE("xnu_live_tmr_enter_src", source);
        TB_LIVE("xnu_live_tmr_enter_call", call);
        TB_LIVE("xnu_live_tmr_enter_flags", flags);
        TB_LIVE("xnu_live_tmr_enter_deadline_lo", (uint32_t)deadline);
        TB_LIVE("xnu_live_tmr_enter_deadline_hi", (uint32_t)(deadline >> 32));
        TB_LIVE("xnu_live_tmr_enter_now", now);
        TB_LIVE("xnu_live_tmr_enter_delta", (uint32_t)(deadline - (uint64_t)now));
    } else {
        g_tmr_enter_over_all++;
        if (source < STAGE90_TMR_SRC_MAX) {
            g_tmr_enter_over[source]++;
        }
        if ((g_tmr_enter_over_all & (g_tmr_enter_over_all - 1u)) == 0u) {
            TB_LIVE("xnu_live_tmr_enter_over", g_tmr_enter_over_all);
        }
    }

    if ((g_tmr_enter_n & (g_tmr_enter_n - 1u)) == 0u) {
        TB_LIVE("xnu_live_tmr_src0", g_tmr_src_calls[0]);
        TB_LIVE("xnu_live_tmr_src1", g_tmr_src_calls[1]);
        TB_LIVE("xnu_live_tmr_src2", g_tmr_src_calls[2]);
        TB_LIVE("xnu_live_tmr_src3", g_tmr_src_calls[3]);
        TB_LIVE("xnu_live_tmr_src4", g_tmr_src_calls[4]);
    }
}

/* Called from `entry_trace.c`'s `--wrap=thread_quantum_expire`, whose second argument is the thread
 * whose quantum ran out - the identity `quantum_timer_expire` reads out of the `timer_call` it is
 * firing, and therefore the thread the scheduler was preempting when the 1 ms arming in the census
 * above was produced. If the metronome turns out to be the quantum timer, this is the record that
 * says *whose* it was. */
void entry_timebase_note_quantum_expire(uint32_t thread)
{
    g_tmr_qexp_n++;

    if (g_tmr_qexp_n <= STAGE90_TMR_QEXPIRE_SHOWN) {
        TB_LIVE("xnu_live_tmr_qexp_seq", g_tmr_qexp_n);
        TB_LIVE("xnu_live_tmr_qexp_thread", thread);
    } else {
        g_tmr_qexp_over++;
        if ((g_tmr_qexp_over & (g_tmr_qexp_over - 1u)) == 0u) {
            TB_LIVE("xnu_live_tmr_qexp_over", g_tmr_qexp_over);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */
/* The report                                                                                     */
/* --------------------------------------------------------------------------------------------- */

__attribute__((noinline)) void entry_timebase_write_kv(void)
{
    entry_write_kv("xnu_entry_timebase_traced", STAGE90_ENTRY_TIMEBASE_TRACED);
    entry_write_kv("xnu_entry_timebase_pe_calls", g_pe_calls);
    entry_write_kv("xnu_entry_timebase_registered", g_registered);
    entry_write_kv("xnu_entry_timebase_seq", g_reg_seq);
    entry_write_kv("xnu_entry_timebase_vm_init", g_reg_vm_init);
    entry_write_kv("xnu_entry_timebase_args", g_reg_args);
    entry_write_kv("xnu_entry_timebase_bootcpu", (uint32_t)(uintptr_t)BootCpuData);
    entry_write_kv("xnu_entry_timebase_get_dec", (uint32_t)(uintptr_t)&stage90_tbd_get_decrementer);
    entry_write_kv("xnu_entry_timebase_set_dec", (uint32_t)(uintptr_t)&stage90_tbd_set_decrementer);
    entry_write_kv("xnu_entry_timebase_fiq", (uint32_t)(uintptr_t)&stage90_tbd_fiq_handler);
    entry_write_kv("xnu_entry_timebase_fiq_ctx_calls", g_fiq_ctx_calls);
    entry_write_kv("xnu_entry_timebase_cpu_get_dec", g_cpu_get_dec);
    entry_write_kv("xnu_entry_timebase_cpu_set_dec", g_cpu_set_dec);
    entry_write_kv("xnu_entry_timebase_cpu_fiq", g_cpu_fiq);
    entry_write_kv("xnu_entry_timebase_cpu_dec", g_cpu_dec);
    entry_write_kv("xnu_entry_setpop_calls", g_setpop_calls);
    entry_write_kv("xnu_entry_setpop_deadline_lo", g_setpop_first_deadline_lo);
    entry_write_kv("xnu_entry_setpop_deadline_hi", g_setpop_first_deadline_hi);
    entry_write_kv("xnu_entry_setpop_returned", g_setpop_first_returned);
    /* 484: the census of armed values and the armer's count. Every one of these is zero at the
     * epilogue, because the epilogue is inside `arm_init`'s own window and the first arming is later -
     * so a *non-zero* here would be the finding and a zero is the expected reading. They are in the
     * report as well as the live channel because the report is what a run that stops early carries,
     * and a reader comparing the two channels should not have to guess which keys the earlier one
     * could never have. */
    entry_write_kv("xnu_entry_dec_ref", g_dec_ref_value);
    entry_write_kv("xnu_entry_dec_same", g_dec_same);
    entry_write_kv("xnu_entry_dec_other", g_dec_other);
    entry_write_kv("xnu_entry_dec_distinct", g_dec_distinct);
    entry_write_kv("xnu_entry_dec_other_more", g_dec_other_more);
    entry_write_kv("xnu_entry_dec_shown0", g_dec_shown_value[0]);
    entry_write_kv("xnu_entry_dec_shown0_call", g_dec_shown_call[0]);
    entry_write_kv("xnu_entry_dec_shown0_count", g_dec_shown_count[0]);
    entry_write_kv("xnu_entry_dec_shown1", g_dec_shown_value[1]);
    entry_write_kv("xnu_entry_dec_shown1_call", g_dec_shown_call[1]);
    entry_write_kv("xnu_entry_dec_shown1_count", g_dec_shown_count[1]);
    entry_write_kv("xnu_entry_tmr_setup_n", g_tmr_setup_n);
    entry_write_kv("xnu_entry_tmr_setup_over", g_tmr_setup_over);
    entry_write_kv("xnu_entry_tmr_enter_n", g_tmr_enter_n);
    entry_write_kv("xnu_entry_tmr_enter_over", g_tmr_enter_over_all);
    entry_write_kv("xnu_entry_tmr_src1", g_tmr_src_calls[1]);
    entry_write_kv("xnu_entry_tmr_src2", g_tmr_src_calls[2]);
    entry_write_kv("xnu_entry_tmr_src3", g_tmr_src_calls[3]);
    entry_write_kv("xnu_entry_tmr_src4", g_tmr_src_calls[4]);
    entry_write_kv("xnu_entry_tmr_qexp_n", g_tmr_qexp_n);
    entry_write_kv("xnu_entry_tmr_qexp_over", g_tmr_qexp_over);
    entry_write_kv("xnu_entry_dec_writes", g_dec_writes);
    entry_write_kv("xnu_entry_dec_first_value", g_dec_first_value);
    entry_write_kv("xnu_entry_dec_first_readback", g_dec_first_readback);
    entry_write_kv("xnu_entry_dec_first_ctl", g_dec_first_ctl);
    entry_write_kv("xnu_entry_cntv_tval_a", g_cntv_tval_a);
    entry_write_kv("xnu_entry_cntv_tval_b", g_cntv_tval_b);
    entry_write_kv("xnu_entry_cntvct_a_lo", g_cntvct_a_lo);
    entry_write_kv("xnu_entry_cntvct_b_lo", g_cntvct_b_lo);
    entry_write_kv("xnu_entry_cntv_spin", g_cntv_spin);
    entry_write_kv("xnu_entry_cntv_arm_mask", STAGE90_CNTV_ARM_MASK);
}
