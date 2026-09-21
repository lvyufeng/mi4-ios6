/*
 * The GIC registers the entry image reads while XNU is running, and the `struct cpu_data` words the
 * IRQ vector's own dispatcher loads.
 *
 * **Why a header.** The same reason `entry_saved_state.h` and `entry_timebase.h` are ones: this image
 * cannot include Apple's headers, so every number `entry_gic.c` writes is transcribed, and a
 * transcribed offset is this project's most repeated defect. One definition, in a file about the
 * thing it describes, and `tools/check_gic_routing.py` refuses the link when any of them disagrees
 * with the source that already owns it.
 *
 * **And here there are two sources rather than one, which is the point of the check.** The GIC
 * offsets are not Apple's - MSM8974's interrupt controller is described by the payload's own
 * `stages/stage90/gic.c` and by experiment 143's `xnu_msm8974_fiq_probe.c`, both of which drive the
 * same registers on the same device and both of which have hardware runs behind them. So the check
 * compares *these* numbers against *those* two files in both directions: every name this header
 * defines must appear in one of them with the same value, and every GIC offset either of them uses
 * must appear here if 482 reads it. That is the "one value, two definitions" guard applied to the
 * half of this step that is not Apple's ABI - the first time in this walk that the value's owner is
 * this repository rather than the tree.
 */
#ifndef STAGE90_ENTRY_GIC_H
#define STAGE90_ENTRY_GIC_H

/* The distributor and the CPU interface, from `gic.c`'s `gic_reg[]` and the DT probe it asserts
 * against (`ok &= distBase == 0xf9000000u`). Both are read as *virtual* addresses, which is sound
 * only because the entry image's premise is `physBase == virtBase`. */
#define STAGE90_GIC_DIST_BASE   0xf9000000u
#define STAGE90_GIC_CPU_BASE    0xf9002000u

/* Distributor (`GICD`) - `gic.c`'s own names and values. */
#define STAGE90_GICD_CTLR       0x000u
#define STAGE90_GICD_TYPER      0x004u
#define STAGE90_GICD_IGROUPR0   0x080u   /* intid 0-31: the PPIs - and so both timer candidates */
#define STAGE90_GICD_ISENABLER0 0x100u
#define STAGE90_GICD_ICENABLER0 0x180u
#define STAGE90_GICD_ISPENDR0   0x200u   /* the register this step reads to identify the line  */
#define STAGE90_GICD_ICPENDR0   0x280u
#define STAGE90_GICD_IPRIORITY0 0x400u   /* one byte per intid; +16 is the word holding 16..19  */
#define STAGE90_GICD_ITARGETSR0 0x800u   /* banked for PPIs: per-CPU, and 0 means "this CPU"     */

/*
 * **The register a *driver* raises an interrupt with, and 496's writing half.** `GICD_SGIR` is the
 * GICv2 software-generated-interrupt register: a write asks a CPU to take one of the sixteen SGIs,
 * with the intid in bits `[3:0]` and the target filter in `[25:24]`. It is named here rather than
 * only in the file that writes it because the payload owns this number: `gic.c`'s
 * `gic_sgi_selftest` pends SGI 0 to itself exactly this way and counted the delivery
 * (`gic_sgi_sgi0_count = 1`, `gic_sgi_last_iar = 0`, `gic_sgi_spurious_count = 0` in every run), so
 * a driver that writes it is the second user of a mechanism this project has already measured - and
 * `tools/check_gic_routing.py` refuses the link if the two spellings disagree.
 */
#define STAGE90_GICD_SGIR       0xf00u

/* `GICD_SGIR`'s target filter for "only the requesting CPU" (the architecture's `0b10`), and the
 * SGI the payload's own self-test used - both `gic.c`'s (`GICD_SGIR_TARGET_SELF`, `GIC_SGI0_ID`). */
#define STAGE90_GICD_SGIR_TARGET_SELF (2u << 24)
#define STAGE90_GIC_SGI0_ID     0u

/*
 * `GICD_ICFGR<n>`, one two-bit field per intid, and **the one offset in this header with no second
 * source in this repository**: `gic.c` does not name it and neither does 143's probe, so unlike every
 * other number here it cannot be compared against the payload. Its source is the architecture
 * (`GICD_ICFGR<n>` at `0xC00 + 4n`, field `2 * (intid mod 16)`) and the check therefore asserts the
 * *arithmetic* instead of the value: `entry_irq.c` must pick the word as
 * `STAGE90_GICD_ICFGR0 + (STAGE90_GIC_TIMER_INTID / 16) * 4` and the shift as
 * `(STAGE90_GIC_TIMER_INTID % 16) * 2`, so 483 cannot hardcode `0xC04` and `8` as a second definition
 * of a derivation. 482 read `ISPENDR0` twice and inferred from the pair that the line behaves as
 * **edge**-configured (the pending bit did not follow the line back down); this register is what
 * settles it, and this step reads it and records it rather than writing it.
 */
#define STAGE90_GICD_ICFGR0     0xc00u

/* CPU interface (`GICC`). */
#define STAGE90_GICC_CTLR       0x000u
#define STAGE90_GICC_PMR        0x004u
#define STAGE90_GICC_IAR        0x00cu   /* a read *acknowledges* - 482 deliberately never reads */
#define STAGE90_GICC_EOIR       0x010u   /* 483's handler writes it; 482 has nothing to ack     */

/* `IAR`'s payload: the intid in the low ten bits, and `0x3ff` is also the spurious value a CPU
 * interface returns when there is nothing to acknowledge. Both are `gic.c`'s own
 * (`GICC_IAR_INTID_MASK`, `GICC_SPURIOUS_ID`), which is why the check compares them. */
#define STAGE90_GICC_IAR_INTID_MASK 0x3ffu
#define STAGE90_GICC_SPURIOUS_ID    0x3ffu

/*
 * `GICC_CTLR`'s two enable bits. Bit 0 is `EnableGrp0` and bit 1 is `EnableGrp1`; the CPU interface
 * this device presents is the **non-secure** view, where bit 0 is `EnableGrp1` - which is why 143's
 * probe read `fiq_gicc_ctlr = 0x00000001` while trying to enable Group 0. 482 does not depend on
 * which view it is: it clears **both** bits for its window and restores the word it read, so the
 * guard is "whichever enable bit this interface has, it is clear" either way.
 */
#define STAGE90_GICC_CTLR_GRP0  0x00000001u
#define STAGE90_GICC_CTLR_GRP1  0x00000002u
#define STAGE90_GICC_CTLR_ANY   (STAGE90_GICC_CTLR_GRP0 | STAGE90_GICC_CTLR_GRP1)

/*
 * **The two candidate interrupt lines.** The device tree's timer node is
 * `interrupts = <1 2 0 1 3 0>` (`gic.c`: "GIC xlate maps PPI n to INTID n + 16"), so the architected
 * timer's two lines are INTID 18 and INTID 19 - and experiment 143 measured **19** to be the one
 * `CNTP` asserts (its Group 0 write was aimed at the CNTP PPI, and the run's `disarm_ispendr0`
 * shows bit 19 pending while `CNTP_CTL.ISTATUS` was set). 481 drives `CNTV`, not `CNTP`, so the
 * question 482 answers with a reading is whether the *virtual* timer is 18 - the sibling the DT
 * lists first - and the measurement is designed so that it can answer 19 or "neither" just as well.
 *
 * These two numbers are also the payload's own (`gic.c`'s `GIC_TIMER_PPI0_ID`/`PPI1_ID`), and the
 * check compares them, because "the instrument's idea of which line the timer is on" and "the
 * kernel's idea" being two definitions of one decision is precisely the failure shape this project
 * has paid for twenty-four times.
 */
#define STAGE90_GIC_TIMER_PPI0  18u
#define STAGE90_GIC_TIMER_PPI1  19u

/*
 * **The line the virtual timer is actually on, and it is neither of the two above.**
 *
 * 483's handler arms exactly this intid, and the number is not a reading of the tree or of Apple's
 * source: it is 482's measurement, and the four keys that carry it are
 * `xnu_live_gic_pend_added18 = xnu_live_gic_pend_added19 = xnu_live_gic_pend_added = 0x00100000`
 * and `xnu_live_gic_timer_intid = 0x00000014`. `0x100000` is bit 20, each candidate raised it under
 * its own countdown and raised nothing under its own masked control, and 19 is *not* free - the
 * payload's own IRQ path acknowledged it earlier in the same boot (`gic_timer_last_iar = 0x13` with
 * `gic_timer_last_timer_ctl = 0x5`), so 19 is `CNTP`'s line and the virtual timer has one of its own.
 *
 * **So this constant and the two above disagree on purpose, and the disagreement is the step.** The
 * check refuses the link unless the two candidates are still distinct and this number is neither of
 * them, because the failure this guards against is a build that quietly goes back to the device
 * tree's pair - which was measured wrong for `CNTV` - or to a "one of the two, whichever fires"
 * handler, which cannot distinguish a line from a coincidence.
 */
#define STAGE90_GIC_TIMER_INTID 20u

/*
 * `struct cpu_data`'s interrupt words, and the reason they are here rather than in
 * `entry_saved_state.h`: `fleh_irq_handler` loads all five and calls the fourth as a function
 * pointer (`ldr r0, [r4, #192]` / `ldr r1, [r4, #196]` / `ldr r2, [r4, #184]` / `ldr r3, [r4, #188]`
 * / `ldr r5, [r4, #180]` / `blx r5`), which the check re-reads out of *this* image's own
 * disassembly rather than out of a comment. **The offset is the whole of 483's question**: the
 * dispatch that would run this image's timer handler is one load and one indirect branch, and it
 * goes to whatever `ml_install_interrupt_handler` last stored - which on this image is nothing, so
 * `r5` is 0 and `blx r5` would branch to address 0.
 */
#define STAGE90_CPU_INTERRUPT_HANDLER   180
#define STAGE90_CPU_INTERRUPT_NUB       184
#define STAGE90_CPU_INTERRUPT_SOURCE    188
#define STAGE90_CPU_INTERRUPT_TARGET    192
#define STAGE90_CPU_INTERRUPT_REFCON    196

/*
 * Four more `cpu_data` words 483 reads, and the reason they are in this header rather than a third
 * offset file: `fleh_irq_kernel` and `fleh_irq_handler` are the code that will run, and these are the
 * words *they* load - the interrupt stack the second-level handler runs on (`ldr sp, [sp,
 * CPU_ISTACKPTR]`, `locore.s:1377`), the saved context it can be handed (`str r5, [r4,
 * CPU_INT_STATE]`, `:1407`), and the two statistics words it increments on the way in (`:1409`/`:1413`).
 * The first of them is a **precondition and not a reading**: an interrupt stack pointer of 0 is a
 * `ldr sp, [r0]` from address 0 the moment an interrupt arrives, which is the one failure mode this
 * step cannot see from the source and can see from the link. `tools/check_irq_routing.py` compares all
 * four against the generated `assym.s`, and the run records the value of `ISTACKPTR` before the line
 * is enabled.
 */
#define STAGE90_CPU_ISTACKPTR           4
#define STAGE90_CPU_INT_STATE           176
#define STAGE90_CPU_STAT_IRQ            376
#define STAGE90_CPU_STAT_IRQ_WAKE       380

/*
 * How long the probe gives the countdown to reach zero before it reads the distributor, and the
 * deadline it programs.
 *
 * **Two rates, both measured, and the arithmetic between them is where this paragraph was first
 * wrong.** 481 measured one bounded spin of 20000 empty statements moving `CNTVCT` by exactly 813
 * while `CNTV_TVAL` fell by the same 813 - which is **≈41 ticks per 1000 iterations**, not the ~24
 * this comment said when it was written (813/20 = 40.65; the slip is the kind 213 counts). And that
 * rate belongs to *481's* loop, whose counter is a plain `uint32_t` in a register, while this file's
 * `gic_spin` keeps its counter `volatile` so no compiler can fold an empty loop away: a
 * load-and-store per iteration instead of a register decrement, which this probe's own run measures
 * at **≈100 ticks per 1000 iterations** - `xnu_live_gic_ticks_elapsed = 0x00027282` (160386) across
 * four windows of 400000 iterations, the only two numbers in that sentence, the four windows being
 * equal by construction and everything else in the span a handful of register accesses.
 *
 * So the forecast this comment first made ("~170000 iterations to reach 4096, a 2.4× margin, 0.9 ms")
 * is wrong twice over and conservative in the end: the deadline is reached in ~41000 iterations here,
 * and one window spans ~40000 ticks against a 4096-tick countdown - a margin of about 10×, not 2.4×.
 * The margin is nonetheless a *reading* in this probe rather than an estimate, by two independent
 * means: the post-spin `CNTV_CTL` of **both** windows carries `ISTATUS` (`0x7` masked and `0x5`
 * unmasked, so the countdown reached zero with the line masked and with it unmasked), and the elapsed
 * `CNTVCT` is recorded beside the result. A spin that is too short is the one failure this probe
 * cannot distinguish from "the line is not this one", which is why both of those are in the log and
 * why the source has to bracket its windows with the counter - `tools/check_gic_routing.py` refuses
 * the link if it stops doing so.
 */
#define STAGE90_GIC_PROBE_TICKS  0x00001000u
#define STAGE90_GIC_PROBE_SPIN   400000u

/*
 * The probe itself, called once from the first `ml_set_decrementer` this image makes - i.e. from
 * inside `setPop`, under `splclock`, after 481's own countdown sample and before the kernel's first
 * deadline is programmed. `entry_gic.c` carries the reasoning, the three delivery guards and the
 * measurement; the only thing worth repeating here is that it is **idempotent and returns without
 * touching a GIC register if the section it needs could not be installed**, so a build that calls it
 * twice or a machine whose L1 slot is already occupied cannot make it a fault.
 */
void entry_gic_probe(void);

/*
 * The distributor's identification register, as this image read it (`entry_gic.c`'s probe, from the
 * first `ml_set_decrementer` this image makes). **It exists as a named symbol because a second reader
 * reads it later, through a different mapping, and has to be able to compare.** `GICD_TYPER` is
 * read-only and constant, so the comparison is a comparison of two *ways of reaching one register* -
 * this probe's own 1 MB section at `0xf9000000` and the OS-side driver's mapping of the range the
 * device tree declares - and not of two moments in the value's life. `MSM8974GIC.cpp` declares it
 * `extern` and publishes `_probetyper` beside its own `_maptyper` and the verdict.
 */
extern uint32_t g_stage90_gic_dist_typer;

/*
 * The four accesses both of this window's users go through, and **483's reason for exporting them is
 * 482's reason for exporting the `CNTV` five**: the handler in `entry_irq.c` reads `GICC_IAR`, writes
 * `GICC_EOIR` and reads the distributor, and the alternative was a second set of `volatile` pointer
 * arithmetic in a second file - this project's most repeated defect, and one that a check on either
 * file alone would not catch. They live in `entry_irq.c` because that is the object this step's step
 * is in; in a build with `STAGE90_IRQ_ENABLE_LINE` off they are the only thing that file defines and
 * the only thing `entry_gic.c` uses from it.
 */
uint32_t gicd_read(uint32_t off);
void     gicd_write(uint32_t off, uint32_t value);
uint32_t gicc_read(uint32_t off);
void     gicc_write(uint32_t off, uint32_t value);

/* The second-level handler `ml_install_interrupt_handler` is handed, in Apple's own ABI - the four
 * registers `fleh_irq_handler` loads out of `cpu_data` before `blx r5` are r0..r3, and this signature
 * is that call. Declared here so the installation and the definition cannot drift. */
void entry_irq_handler(void *target, void *refCon, void *nub, int source);

/* The one ordered arming sequence, called once from the end of the first `ml_set_decrementer`. It
 * returns 1 **only when the line was enabled at the distributor and a countdown is therefore able to
 * become an interrupt**, which is the value that decides whether the caller may stop masking. A 0
 * means the machine is exactly as 482 left it. */
uint32_t entry_irq_arm(void);

/*
 * **The client registry: a driver owning a line of this image's dispatcher (496).**
 *
 * Until 496 the dispatcher's case for "some other line" was a stop - the shape that cannot loop, and
 * the right one while nothing in this image enabled a second line. 496 makes one line available to a
 * driver, and the mechanism is a *registration* rather than a second dispatcher: the driver hands
 * over an intid and a function, `entry_irq_register_client` files it, and `entry_irq_handler` calls
 * that function after it has acknowledged the line. Everything else about the dispatcher is
 * unchanged, including the stop: an intid with no client is still recorded, acknowledged and ended,
 * which is why the run for a driver that pends an *unregistered* line stops at the same place every
 * earlier run would have.
 *
 * **The handler is a `uint32_t` and not a function-pointer type, on purpose.** This is the one ABI
 * between a C++ driver and this C image, the driver cannot include this header (it declares its own
 * `extern "C"` prototypes, as it does for `entry_live_write`), and a function-pointer parameter
 * spelled in two files is two definitions of one signature with nothing comparing them. Three
 * `uint32_t`s - an intid, an address, an address - are checkable character by character, and both
 * sides publish the number they hold so the log carries the same value twice.
 *
 * `refCon` is passed back verbatim, and the call is `handler( refCon, intid )`.
 */
#define STAGE90_IRQ_CLIENTS     4u
#define STAGE90_IRQ_CLIENT_CAP  64u

uint32_t entry_irq_register_client(uint32_t intid, uint32_t handler, uint32_t refCon);
uint32_t entry_irq_unregister_client(uint32_t intid);

#endif /* STAGE90_ENTRY_GIC_H */
