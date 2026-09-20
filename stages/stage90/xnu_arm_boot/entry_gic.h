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

/* CPU interface (`GICC`). */
#define STAGE90_GICC_CTLR       0x000u
#define STAGE90_GICC_PMR        0x004u
#define STAGE90_GICC_IAR        0x00cu   /* a read *acknowledges* - 482 deliberately never reads */
#define STAGE90_GICC_EOIR       0x010u   /* 483's handler writes it; 482 has nothing to ack     */

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

#endif /* STAGE90_ENTRY_GIC_H */
