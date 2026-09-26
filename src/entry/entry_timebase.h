/*
 * The four words of `struct cpu_data` this step reads, and the one architectural register pair the
 * decrementer is programmed through.
 *
 * **Why a header.** The same reason `entry_saved_state.h` is one: the image cannot include Apple's
 * headers, so the numbers `entry_timebase.c` dereferences are transcribed, and a transcribed offset
 * is this project's most repeated defect. One definition, in a file about the thing it describes,
 * and a build step that refuses the link when it disagrees with the generated `assym.s`.
 *
 * **Where these numbers come from.** `out/xnu_assym/$XNU_KERNEL_CONFIG/assym.s`, which
 * `tools/gen_assym.sh` writes by compiling Apple's own `osfmk/arm/genassym.c` - the same source
 * `entry_saved_state.h`'s six frame offsets come from, and `tools/check_saved_state_offsets.py`
 * compares both sets against it. For this configuration it gives:
 *
 *     #define CPU_DECREMENTER             #104
 *     #define CPU_GET_DECREMENTER_FUNC    #108
 *     #define CPU_SET_DECREMENTER_FUNC    #112
 *     #define CPU_GET_FIQ_HANDLER         #116
 *
 * and those four are `struct cpu_data`'s own words, in Apple's own order - `osfmk/arm/cpu_data.h`:
 * a `uint32_t` software decrementer, then the three function pointers `cpu_timebase_init` copies out
 * of `rtclock_timebase_func`. `osfmk/arm/machine_routines_asm.s` is what reads them, and it is the
 * only reader: `ml_get_decrementer` and `ml_set_decrementer` each load
 * `[ACT_CPUDATAP + CPU_{GET,SET}_DECREMENTER_FUNC]` and `bxne` it, falling back to the software
 * counter when the pointer is NULL.
 *
 * **What 481 does with them, and what it is measuring.** Before this step `rtclock_timebase_func` was
 * zero - `ml_init_timebase`'s only call site is `pexpert/arm/pe_identify_machine.c:666`, and in this
 * configuration the `#if defined(ARM_BOARD_CLASS_*)` chain above it is compiled away entirely (no
 * `ARM_BOARD_CONFIG_*` is set, so `board_config.h` defines no class), leaving the chain's final
 * `return 0;` as the function's first statement - so the call is unreachable and, at `-O2`, absent
 * from the object (`entry_timebase.c`'s header carries the measurement). Two consequences are already
 * in this project's logs, and they are the two this step's readings have to move: `ml_set_decrementer`
 * falls through to its `#else` body, which programs **no hardware at all** and leaves
 * `cpu_data->cpu_decrementer` at the `0x7FFFFFFF` that `cpu_timebase_init` wrote
 * (`osfmk/arm/cpu.c:465`); and every deadline the kernel arms is a queue entry with nothing behind
 * it, which is why 57 threads are parked.
 *
 * So the step's first reading is that these four words changed, and the header exists because the
 * reading is a *comparison*: `entry_timebase.c` reads them at the first `fiq_context_init` (one
 * instruction after `cpu_timebase_init` has copied the table in) and the build refuses to link if the
 * offsets it reads them at are not the ones `assym.s` declares, because a wrong offset here reports a
 * plausible pointer that is some other word of the structure - the same failure shape the six frame
 * offsets have, and the same reason it is a build failure rather than a comment.
 *
 * **Three of those four words have a predicted value, and they are what makes the read a reading
 * rather than a claim that something is non-zero:** at that first `fiq_context_init`,
 * `CPU_GET_DECREMENTER_FUNC` / `CPU_SET_DECREMENTER_FUNC` / `CPU_GET_FIQ_HANDLER` must be this image's
 * three function addresses and `CPU_DECREMENTER` must be `0x7FFFFFFF`. The first three are also what
 * says Apple's guard accepted the registration: `cpu_timebase_init`'s copy is guarded on
 * `cpu_get_fiq_handler == NULL` and it is a *struct copy* of `rtclock_timebase_func`, so if
 * `ml_init_timebase` had refused (its own guard is `rtclock_timebase_func.tbd_fiq_handler == NULL`),
 * all four words would be zero and these three would read 0.
 */
#ifndef STAGE90_ENTRY_TIMEBASE_H
#define STAGE90_ENTRY_TIMEBASE_H

/* The five accesses at the bottom of this file are declared with fixed-width types, so the header
 * carries its own. It is not freestanding decoration: `entry_timebase.c` includes `<stdint.h>` at
 * the top and worked, and `entry_gic.c` - which includes this header first - did not, so the
 * dependency was real and invisible until a second file included it. */
#include <stdint.h>

/* `struct cpu_data` (`osfmk/arm/cpu_data.h`) - via genassym's `CPU_*` (see above). */
#define STAGE90_CPU_DECREMENTER            104
#define STAGE90_CPU_GET_DECREMENTER_FUNC   108
#define STAGE90_CPU_SET_DECREMENTER_FUNC   112
#define STAGE90_CPU_GET_FIQ_HANDLER        116

/*
 * **The decrementer is the *virtual* timer, and that is a choice with two reasons.**
 *
 * The architectural timer has two countdown registers, `CNTP_TVAL` (physical) and `CNTV_TVAL`
 * (virtual), each with its own control word and its own interrupt line. They share one free-running
 * counter (`CNTPCT`, which `ml_get_timebase` already reads - `__ARM_TIME_TIMEBASE_ONLY__` is what
 * this host defines, so `mach_absolute_time` has been the architected counter since long before this
 * step). This step writes `CNTV_TVAL`, and the reasons are:
 *
 *   - **the payload owns `CNTP`.** It arms `CNTP_CTL` for its own dead-man (the log's
 *     `watchdog_cntp_ctl_armed`), and that dead-man is one of the two recovery nets that keep this
 *     device from needing a person. A kernel that re-programmed `CNTP_TVAL` would be writing a
 *     compare register that a recovery net is using; `CNTV_TVAL` cannot disturb it, because the two
 *     registers are separate hardware even though the counter behind them is one.
 *   - **it is the register Apple's own `__ARM_TIME__` code writes.** `machine_routines_asm.s`'s
 *     `ml_set_decrementer` is `mcr p15, 0, r0, c14, c3, 0` under `__ARM_TIME__`, and
 *     `fiq_context_init`'s `mcr p15, 0, r0, c14, c3, 1` is `CNTV_CTL` - so this step drives the
 *     timer the same way the tree's own configuration would, which is what makes 482's job (routing
 *     its interrupt) a change of *routing* rather than of mechanism.
 *
 * `CNTV_TVAL` is `mcr/mrc p15, 0, r, c14, c3, 0` and `CNTV_CTL` is `c14, c3, 1` - the same
 * `(CRn, CRm, opc2)` triples Apple's own instructions use, which is the fact
 * `tools/check_timebase_registration.py` compares against that file rather than against a comment.
 *
 * **`CNTKCTL`'s bit 0 is deliberately not touched here.** `fiq_context_init` sets it under
 * `__ARM_TIME__` to give EL0 its own counter read; with `__ARM_TIME__` off (this build) nothing
 * does, and this step does not, because that is a user-visible interface rather than a timer.
 */
/*
 * The value `cpu_timebase_init` leaves in `CPU_DECREMENTER` (`osfmk/arm/cpu.c:465`,
 * `cdp->cpu_decrementer = 0x7FFFFFFFUL`) - i.e. what the first `fiq_context_init` must report for
 * `xnu_live_timebase_cpu_dec` when the registration took, and what it must *not* report when Apple's
 * guard refused (in which case the whole table stays zero and all four words read 0). Named rather
 * than written as a literal at the comparison, so that the number the run predicts and the number
 * `tools/check_timebase_registration.py` reads out of `cpu_timebase_init`'s own object code are
 * literally the same constant.
 */
#define STAGE90_CPU_DECREMENTER_INITIAL    0x7FFFFFFFu
#define STAGE90_CNTV_CTL_ENABLE   0x00000001u   /* CNTV_CTL[0] - the countdown runs        */
#define STAGE90_CNTV_CTL_IMASK    0x00000002u   /* CNTV_CTL[1] - the interrupt is suppressed */
#define STAGE90_CNTV_CTL_ISTATUS  0x00000004u   /* CNTV_CTL[2] - reached zero (read-only)  */

/*
 * **And the mask bit is this step's own decision, stated here so the log can be read against it.**
 * 481 programs a countdown and does **not** deliver it: `STAGE90_CNTV_CTL_IMASK` is set on every
 * write, so `CNTV_CTL.ISTATUS` rises when the deadline passes and no interrupt is asserted, which is
 * the one state in which a deadline can be *measured* without anything being able to happen.
 *
 * The reason is a measurement this project already has: **experiment 143 measured that MSM8974
 * delivers no FIQ to non-secure PL1**, with the Group 0 configuration accepted at both ends and the
 * timer measurably reaching `ISTATUS`, and this image's vector page puts Apple's decrementer handler
 * in slot 7 - the FIQ slot. The IRQ slot holds `fleh_irq`, which calls the platform's
 * `interrupt_handler` through `cpu_data` and has none installed, so a timer interrupt delivered as a
 * group-1 IRQ today would branch to a NULL handler. **482's job is that routing decision** - which
 * physical interrupt line the virtual timer appears on for this SoC, and whether the decrementer
 * handler or the generic one owns the slot - and it cannot be taken until the interrupt's arrival is
 * something that can be observed rather than something that would stop the boot.
 */
#define STAGE90_CNTV_ARM_MASK   (STAGE90_CNTV_CTL_ENABLE | STAGE90_CNTV_CTL_IMASK)

/*
 * The five instructions themselves, as five functions rather than as five copies.
 *
 * 482's GIC probe arms and masks the same countdown this file owns, so it needs the same five
 * accesses - and the alternative was a second set of `mcr`/`mrc` lines in `entry_gic.c`, which is
 * this project's most repeated defect (two definitions of one value, neither compared). The triples
 * are compared against Apple's own source by `tools/check_timebase_registration.py` on the strength
 * of being **these** lines, which is a property a copy would not inherit: that check reads them out
 * of this file, and a second spelling would be a spelling nothing reads.
 */
uint32_t stage90_cntv_tval_read(void);
void     stage90_cntv_tval_write(uint32_t value);
uint32_t stage90_cntv_ctl_read(void);
void     stage90_cntv_ctl_write(uint32_t value);
uint64_t stage90_cntvct_read(void);

#endif /* STAGE90_ENTRY_TIMEBASE_H */
