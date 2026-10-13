/*
 * pe_msm8974.c - the msm8974 (Xiaomi Mi 4 "cancro") board half of the Darwin-13 platform expert.
 *
 * This file is OURS (mi4-ios6), not lifted from HD2. The board files HD2 ships
 * (`pe_qsd8250_leo.c`, `pe_bcm2835.c`, ...) carry no reuse license and every address in them is
 * another SoC; only their *shape* - one `PE_init_SocSupport_stub()` filling the `gPESocDispatch`
 * table declared in the reusable, winocm-licensed `pexpert/arm/common/pe_socsupport.c` - is modelled
 * here. See docs/experiments/experiment-914-msm8974-pe-design.md.
 *
 * What this board must supply, and what it must NOT. Compiling Darwin-13 `osfmk/arm` alone leaves
 * 175 undefined symbols; the platform-expert ones are satisfied by the *generic* files
 * (`pexpert/arm/common/*`, `pexpert/gen/*`) EXCEPT the device methods the generic code reaches
 * through the table. `PE_cpu_machine_init` is NOT one of them on ARM - it is defined generically in
 * `iokit/Kernel/IOCPU.cpp:271`. So the whole board deliverable is the table below.
 *
 * Every device value here is CITED from our own entry image, never re-derived, so the PE and the
 * payload that jumped into it describe one machine (the "one value, two definitions" rule):
 *   GIC distributor / CPU interface   0xf9000000 / 0xf9002000   src/entry/entry_gic.h:27-28
 *   GIC timer PPI, spurious id        18/19, 0x3ff               src/entry/entry_gic.h:115-116,88
 *   GICC_IAR / GICC_EOIR offsets      0x00c / 0x010              src/entry/entry_gic.h:81-82
 *   virtual counter CNTVCT / CNTFRQ   CP15 c14, 19,200,000 Hz    src/entry/entry_timebase.h:108, src/timebase.c
 *   RAM console base                  0xde500000                 src/stage90.h:13
 *
 * Safety stance (matching the project's rule that a claim in a comment is not a check): this file
 * does NOT touch a device register that our entry image may not have mapped, and does NOT claim
 * success it cannot witness. `uart_putc` is a bounded RAM ring (a witness sink, like HD2's leo
 * `ios7leo_debug_putc` but ours); the interrupt path is written to agree with the entry image's GIC
 * rather than to reconfigure it. That is deliberate: `ml_init_interrupt` and the IRQ dispatch are
 * already OUR image's (`fleh_irq`, slot 6, src/entry/entry_irq.c); inventing a second controller
 * handler here is the defect this avoids.
 */

#if defined(BOARD_CONFIG_MSM8974)

#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm/protos.h>
#include <pexpert/arm/boot.h>
#include <machine/machine_routines.h>

/* ---- cited msm8974 constants (see file header for the in-repo source of each) ---- */
#define MSM8974_GICD_BASE     0xf9000000u
#define MSM8974_GICC_BASE     0xf9002000u
#define MSM8974_GIC_SPURIOUS  0x3ffu
#define MSM8974_GICC_IAR      0x00cu
#define MSM8974_GICC_EOIR     0x010u
/* The virtual-timer PPI. **Cited, not guessed**: our own entry image measured that the kernel's
 * countdown asserts this intid (`src/entry/entry_gic.h:135` `STAGE90_GIC_TIMER_INTID 20`; 482's
 * probe measured the countdown raising INTID 20), and `src/entry/entry_irq.c` services exactly this
 * line. The mandated reference (`HTC-Leo-Revival-Project/xnu` `pe_msm8974.c`) names its SoC's own
 * `INT_QTMR_FRM_0_PHYSICAL_TIMER_EXP`; this is the same role, this board's measured number.
 * [[mi4-one-value-two-definitions]] */
#define MSM8974_GIC_TIMER_INTID 20u

/* **985: the distributor's per-line enable for the timer PPI, and the CPU the byte targets.** PPI 20
 * lives in word 0 (`intid/32 == 0`), bit 20. `GICD_ISENABLER0` is the same offset the entry image
 * writes for the frame SPI (`src/entry/entry_gic.h:34`); it is one register, so it is one number.
 * The target byte is `1u` = CPU 0 - the same target the frame driver uses
 * (`src/platform/MSM8974Timer.cpp:456` `MSM8974_TIMER_LINE_TARGET 1u`) and the same value 983's press
 * measured already set for the timer line (`_line_target_before = 0x01010101`). For a PPI the target
 * is banked per-CPU and 0 means "this CPU", so writing 1 is also correct and explicit. */
#define MSM8974_GICD_ISENABLER0   0x100u
#define MSM8974_GIC_TIMER_TARGET  1u

/* **985: the D13 clock tick, which nothing armed before this step.** The root cause of 984's park is
 * that the whole D13 boot has NO timer interrupt: `CONFIG_TICKLESS` is defined nowhere, so
 * `osfmk/arm/rtclock.c`'s `setPop` never calls `pe_arm_set_timer_enabled` (its arm/disarm blocks are
 * all inside `#if CONFIG_TICKLESS`), and this file's `msm8974_timer_enabled` was an empty stub to
 * begin with. The countdown is therefore never programmed, PPI 20 (which the entry image MEASURED the
 * CNTV raising, `src/entry/entry_gic.h:135`) never fires, `rtclock_intr` -> `etimer_intr` never runs,
 * and the kernel's `etimer` list is never serviced - so every deadline sleep, including
 * `IOService::waitForService`'s 2 s timeout for "IORTC", parks forever (984). The fix is to give the
 * board a real periodic tick: arm CNTV, enable PPI 20, and re-arm after each `rtclock_intr`.
 *
 * **The period is derived from the ONE frequency definition, never a literal.** `timebase_init`
 * publishes `gPEClockFrequencyInfo.timebase_frequency_hz` from CNTFRQ (or `MSM8974_CNTFRQ_HZ`), and
 * `msm8974_get_timebase` hands back RAW CNTVCT ticks in the same unit, so ticking every
 * `frequency/1000` CNTV ticks is exactly 1 ms of the kernel's own timebase. A literal 19200 would be a
 * second definition of 19.2 MHz ([[mi4-one-value-two-definitions]]). 1 ms is the granularity XNU's
 * 4570 boards ran at and is far under any deadline the abandoned wake needs (`NSEC_PER_USEC` is the
 * layer's minimum), while leaving ample headroom for the SoC watchdog. */
#define MSM8974_TICK_HZ_DIV   1000u

/* One ARM small page, `mach/arm/vm_param.h:21` `ARM_PGBYTES 4096`. Spelled here rather than by
 * including that header into a board file: it is the size `ml_io_map` rounds up, and the two GIC CPU
 * registers below (0x00c, 0x010) both live in the first page of the 0xf9002000 window. */
#define MSM8974_PAGE_BYTES    0x1000u

/* CNTVCT/CNTFRQ are CP15 c14; the read is always legal, unlike a device load. The frequency is the
 * one our own src/timebase.c measured on cancro; a wrong value here would scale every XNU tick, so
 * it is the measured 19.2 MHz, not a nominal 19.2/24 MHz guess. */
#define MSM8974_CNTFRQ_HZ     19200000ULL

/* **974: the console sink is the ENTRY IMAGE's captured one, not a private ring.** Before 973 the
 * boot STOPPED at `pe_init.c:146`'s `PE_init_SocSupport()` (the generated stub), so line 151
 * (`PE_kputc = gPESocDispatch.uart_putc`) was never reached, and XNU's console text went through
 * whatever sink the image put in `gPESocDispatch.uart_putc`. 973 made the board PE RUN and RETURN,
 * so line 151 now executes and `PE_kputc` - plus `PE_early_puts` (`pe_serial.c:44`) - becomes THIS
 * function. A private ring with no reader therefore makes every XNU message after that point
 * INVISIBLE, and the 973 press going silent is consistent with exactly that (the boot may be
 * resident, not hung). The entry image exports `entry_os_console_char(int, uint32_t)` (`T`,
 * src/entry/entry_stubs.c:2696) - the SAME sink `__wrap_vcputc` uses, which appends to the captured
 * RAM console block and is safe at any point (it holds text in `.bss` before the live tables are
 * installed). `which = 2u` is that source's own tag for the serial/uart route. So this function is
 * the board PE's `uart_putc`, feeding the capture - the 458 route, not a second definition.
 * [[mi4-one-value-two-definitions]] */
extern void entry_os_console_char(int ch, uint32_t which);

/* **rtc_configure** is the one symbol the generic ARM RTC layer exports for the board PE to call
 * (`osfmk/arm/rtclock.c:281`); it stores its argument into `rtclock_sec_divisor`, which
 * `rtclock_bus_configure` (`:287`) then panics on if it is still 0. No XNU header declares it - every
 * peer board file declares it locally, exactly here: pe_apq8060.c:86, pe_omap3530.c:65,
 * pe_bcm2835.c:55, pe_imx53.c:51. So this file mirrors that idiom rather than inventing a header edit.
 * [[mi4-one-value-two-definitions]] */
extern void rtc_configure(uint64_t hz);

/* **entry_irq_gic_map** is the entry image's own GIC-window setter (`src/entry/entry_irq.c`, declared
 * `src/entry/entry_gic.h:320`). The four accessors in that file add a base the image starts with at
 * zero - the raw `0xf9000000`/`0xf9002000` window, valid while `physBase == virtBase`. Once XNU owns
 * the page tables that window is gone, and the timer driver's `start()` (`src/platform/MSM8974Timer.cpp`,
 * an XNU object) reaches `entry_irq_enable_line` and faults reading `GICD_ISENABLER0` at `0xf9000104`
 * (982's press). So this file, which already `ml_io_map`s the CPU interface for its own handler, ALSO
 * maps the distributor and hands the image both VAs - ONE fix for the ONE window, not a second copy.
 * [[mi4-one-value-two-definitions]] */
extern void entry_irq_gic_map(uint32_t dist_va, uint32_t cpu_va);

/* **rtclock_intr** is the generic ARM RTC layer's interrupt entry (`osfmk/arm/rtclock.c:175`), which
 * runs the clock's own `etimer_intr`. It is what ADVANCES THE KERNEL TICK, and the interrupt that
 * delivers it is the timer PPI below - so a handler that acknowledges and EOI's that line WITHOUT
 * calling this one acknowledges a tick it never services, and the countdown is never re-armed: the
 * clock stops and every deadline derived from it never fires. The 906/904 peers all declare it
 * locally exactly here and call it on their timer intid - pe_apq8060.c:85,246, pe_omap3530.c:64,266,
 * pe_imx53.c:50,213 - and so does the mandated reference (`HTC-Leo-Revival-Project/xnu`
 * `pe_msm8974.c`, `rtclock_intr((arm_saved_state_t *) context)` in its timer branch). No XNU header
 * declares it, so this file mirrors that idiom. Before 982 this function had NO caller in our image:
 * the countdown line was enabled but nothing ran the tick. [[mi4-one-value-two-definitions]] */
extern void rtclock_intr(arm_saved_state_t *regs);

/* **985: the live channel, and the reason this file publishes at all.** `entry_live_write` is the
 * entry image's own record sink (`src/entry/entry_stubs.c`), declared locally here exactly as the
 * three sibling platform sources do (`MSM8974GIC.cpp:120`, `MSM8974RootResource.cpp:88`,
 * `MSM8974PlatformExpert.cpp:78`). The tick this step adds has two halves that can each be silent -
 * the arming and the delivery - so each half gets keys that are published as ZERO before the work and
 * overwritten as it succeeds, which makes "the tick never armed" and "it armed but never fired" two
 * different lines rather than one silence ([[mi4-silence-is-a-reading-only-if-success-is-silent]]). */
extern void entry_live_write(const char *key, uint32_t value);

/* **999 amendment #2: the timer discriminator's writers.** `entry_write_kv` is the DIRECT (uncounted)
 * half of the same sink - the cap cannot eat these records, and that is the requirement, not a
 * convenience: an edge record's ABSENCE is the evidence, and a record whose absence is evidence must
 * be cap-exempt ([[mi4-998-g1-confirmed-map-file-kr0]]'s law, which the 998 done-trio paid for).
 * `entry_live_ready` is the readiness guard `entry_wdt_pet` uses (`src/entry/entry_trace.c`), reused
 * by value so an edge taken before the live channel is up publishes nothing. */
extern void entry_write_kv(const char *key, uint32_t value);
extern uint32_t entry_live_ready(void);

/* **1003: the stall sampler's recorder (`src/entry/entry_stubs.c`).** `entry_note_stall_sample` takes
 * the interrupted frame the timer IRQ already holds and publishes `pc`/`cpsr`/`sp`/`lr` on a
 * powers-of-two ladder. Compiled in ONLY under `STAGE90_XNU_STALL` (the tick site below), so off-arm
 * this file is byte-identical and the recorder is never referenced. */
extern void entry_note_stall_sample(const uint32_t *frame);

static volatile unsigned long long msm8974_cntfrq = MSM8974_CNTFRQ_HZ;

/* ---- the D13 clock tick (985) ---- */

/* How many CNTV ticks one period is, from the ONE frequency definition. `timebase_init` sets
 * `gPEClockFrequencyInfo.timebase_frequency_hz` before `clock_timebase_init` runs, so this reads the
 * same number `msm8974_get_timebase` hands back and `rtclock_bus_configure` divides by - never a
 * literal ([[mi4-one-value-two-definitions]]). Falls back to CNTFRQ, then to the measured constant, so
 * the divisor is never 0 and the tick can never be a zero-period (which would be "always expired"). */
static unsigned int
msm8974_tick_ticks(void)
{
    unsigned int hz = (unsigned int)gPEClockFrequencyInfo.timebase_frequency_hz;
    unsigned int ticks;
    if (hz == 0u)
        hz = (unsigned int)msm8974_cntfrq;
    if (hz == 0u)
        hz = (unsigned int)MSM8974_CNTFRQ_HZ;
    ticks = hz / MSM8974_TICK_HZ_DIV;
    return (ticks != 0u) ? ticks : 1u;
}

/* The period actually in force, published so a reader can see the number the whole tick is scaled by
 * without recomputing it from the frequency. Set once, at the first arming. */
static unsigned int g_msm8974_tick_ticks;

/* Every timer-PPI delivery, counted. Its powers of two are published from the handler, so "the tick
 * armed but never fired" (this stays 0) and "the tick is running" (it climbs) are different lines -
 * the reading 984 could not make, because the line it would have come from did not exist. */
static unsigned int g_msm8974_tick_count;

/* **1000: the delivery's own lateness, in the counter the deadline is expressed in.** The arm site
 * stamps CNTVCT.lo immediately before it programs `CNTV_TVAL`; the handler reads CNTVCT.lo again at
 * the entry and the delta is "how long after the deadline this delivery arrived" - a reading about
 * the DELIVERY, independent of handler duration and of wall-clock. A healthy entry is the vector's
 * own latency (tens to hundreds of ticks); a masked window is a near-whole multiple of the 19,200
 * tick period. 999 measured the collapse (122 deliveries then +24 over >= 2.4 s) but could only say
 * "rate collapsed" - the A/B question (a second writer on TVAL vs delivery masked) needs the raw
 * `CNTV_TVAL`/`CNTV_CTL` readback at the entry, which is what the anomaly records carry. `_max_d`
 * and `_anom_count` are the aggregates the summaries publish; all three are plain statics written
 * only from this file's timer paths (single CPU). */
static uint32_t g_msm8974_tick_arm_cntv;   /* CNTVCT.lo at the last re-arm, before the TVAL store */
static uint32_t g_msm8974_tick_max_d;      /* worst elapsed seen; the negative-cell aggregate */
static uint32_t g_msm8974_tick_anom_count; /* deliveries later than one full period */

/* **999's timer discriminator: the count the deliveries FREEZE at.** Both 997 and 998 publish the
 * powers of two up to `xnu_live_tick_irq=0x40` (captures :7344 / :5463) and `0x80` never appears -
 * the last DELIVERED tick is 64 - so an edge at a count below it belongs to the healthy pre-freeze
 * clock and must not burn one of the four per-site slots `entry_timer_edge_probe` publishes in. */
#define MSM8974_TICK_FROZEN_AT 64u

/* Declared here because the arm site below stamps CNTVCT.lo while the helper's definition sits with
 * its twins beside `msm8974_read_cntvct` (:307-347). A call before a `static` definition is an
 * IMPLICIT (non-static) declaration, and the definition then collides with it - the first 1000 build
 * stopped at exactly that (`static declaration of 'msm8974_read_cntvct_lo' follows non-static
 * declaration`, out/xnu_platform_obj_d13/pe_msm8974.log). */
static inline uint32_t msm8974_read_cntvct_lo(void);

/* Arm the countdown: `CNTV_TVAL` = one period, `CNTV_CTL` = ENABLE|unmask. The order is the entry
 * image's own (`src/entry/entry_timebase.c:293,370`): program the value, then unmask - so the shortest
 * window between "this deadline is armed" and "it can fire" is the distance between two stores. The
 * read-back is the return: 1 only if the value landed, because a write to a device with no clock
 * behind it can vanish, and "the driver asked" and "the countdown is running" are different findings. */
static int
msm8974_tick_arm(void)
{
    unsigned int ticks = msm8974_tick_ticks();
    unsigned int back;

    if (g_msm8974_tick_ticks == 0u) {
        g_msm8974_tick_ticks = ticks;
        entry_live_write("xnu_live_tick_ticks", ticks);
        entry_live_write("xnu_live_tick_hz", (uint32_t)gPEClockFrequencyInfo.timebase_frequency_hz);
    }

    /* **1000: the stamp the handler's delta is measured from.** Read as late as possible before the
     * deadline is programmed, so the delta the handler computes is the period plus the delivery
     * latency and nothing else - the helper above names the counter pair and why it is CNTVCT's). */
    g_msm8974_tick_arm_cntv = msm8974_read_cntvct_lo();

    __asm__ __volatile__("mcr p15, 0, %0, c14, c3, 0" :: "r"(ticks));   /* CNTV_TVAL */
    __asm__ __volatile__("mcr p15, 0, %0, c14, c3, 1" :: "r"(1u));      /* CNTV_CTL: ENABLE */
    __asm__ __volatile__("mrc p15, 0, %0, c14, c3, 1" : "=r"(back));    /* read back CTL */
    return (back & 1u) ? 1 : 0;
}

/* Enable PPI 20 at the distributor - one bit in ONE register. The work is the entry image's own
 * `entry_irq_enable_line` (`src/entry/entry_irq.c:349`, declared `src/entry/entry_gic.h`): it writes
 * the pending clear, the target byte, the group made 0, and the enable bit - each read back - through
 * the gicd accessors that carry 983's mapped VA, so the distributor is reached through the SAME one
 * window the timer driver's SPI uses and a raw `0xf9000000` (the 982 fault) is nowhere in this file.
 * A line in the wrong group is enabled, pending and never delivered (`entry_irq.c:319-322`), which is
 * why the group is written and not assumed. The value this enables the payload's own timer self-test
 * already measured present on this machine, so it is an idempotent re-assertion, not a new
 * configuration. The return is the read-back enable bit: 0 means the distributor did not take the
 * write and the tick will not be delivered. */
extern uint32_t entry_irq_enable_line(uint32_t intid, uint32_t target);

static void
msm8974_tick_enable_line(void)
{
    uint32_t rc = entry_irq_enable_line(MSM8974_GIC_TIMER_INTID, MSM8974_GIC_TIMER_TARGET);
    entry_live_write("xnu_live_tick_line_rc", rc);
}

/* The whole tick, in the order that keeps the two halves distinguishable: enable the line FIRST (a
 * countdown that expires before its line is enabled asserts into a distributor that drops it), then
 * arm the countdown. Both returns are published, so the log separates "the line would not take" from
 * "the countdown did not arm" - two different next steps. */
static void
msm8974_tick_start(void)
{
    unsigned int rc;
    entry_live_write("xnu_live_tick_arm_calls", 1u);
    msm8974_tick_enable_line();
    rc = (unsigned int)msm8974_tick_arm();
    entry_live_write("xnu_live_tick_arm_rc", rc);
}

/* **The GIC CPU interface, reached through a MAPPED kernel VA, not its physical address.** This is
 * 982's fix and the whole of it. `ml_io_map` returns a `kernel_map` VA whose page tables XNU keeps
 * across `arm_vm_init`; the physical `0xf9002000` is *not* translated by XNU's pmap, which maps only
 * `[physBase, physBase+gMemSize)` (identity) and the managed window - `arm_vm_init.c:388-401` copies
 * the boot L1 and then overwrites exactly those two ranges, so the 0xf9000000 device section the
 * entry image installed does not survive into the table the MMU walks after `set_mmu_ttb`.
 *
 * Measured, 981's press: the first interrupt XNU dispatched reached `msm8974_handle_interrupt`, whose
 * `ldr r1, [r0]` at `0x801ae3ac` read `r0 = 0xf900200c` and took a **section translation fault**
 * (`sleh_abort`, `fsr=0x00000005`, `far=0xf900200c`) in interrupt context. Every peer board maps its
 * controller first and reads the VA - `pe_apq8060.c:128-129,236` (`ml_io_map(MSM_GIC_CPU_BASE)` then
 * `HwReg(gTouchpadqGICCPUBase + GIC_CPU_INTACK)`), and the mandated reference
 * `HTC-Leo-Revival-Project/xnu`'s `pe_msm8974.c` (`msm8974_mapping_init`, `readl(gMsmQGICCPUBase +
 * GIC_CPU_INTACK)`). This file reads raw physical only because the entry image used to leave the
 * section mapped; it does not, once XNU owns the tables.
 * [[mi4-one-value-two-definitions]] */
static volatile unsigned int *g_msm8974_gicc;

/* Map the GIC CPU interface into the kernel's own tables, once. Called from
 * `PE_init_SocSupport_msm8974` before any interrupt can be taken, so `g_msm8974_gicc` is non-NULL for
 * the handler below. `ml_io_map` (`osfmk/arm/machine_routines.c:97`) rounds the size up to a page and
 * maps it `VM_WIMG_IO`; both `GICC_IAR` (0x00c) and `GICC_EOIR` (0x010) live in that first page. */
static void
msm8974_gic_map(void)
{
	volatile unsigned int *dist;
	if (g_msm8974_gicc != 0)
		return;
	/* The distributor (for the timer driver's `entry_irq_enable_line`) and the CPU interface (for
	 * this file's handler). Both `ml_io_map`ed; both VAs handed to the entry image's accessors so
	 * there is ONE window and not two opinions about it. 982: before this, only the CPU interface
	 * was mapped and the distributor was read raw, which is the fault the press measured. */
	dist = (volatile unsigned int *)ml_io_map(MSM8974_GICD_BASE, MSM8974_PAGE_BYTES);
	g_msm8974_gicc = (volatile unsigned int *)ml_io_map(MSM8974_GICC_BASE, MSM8974_PAGE_BYTES);
	entry_irq_gic_map((uint32_t)(uintptr_t)dist, (uint32_t)(uintptr_t)g_msm8974_gicc);
}

static inline unsigned long long
msm8974_read_cntvct(void)
{
	unsigned int lo, hi;
	/* mrrc p15, 0, Rt, Rt2, c14 -> Rt = CNTVCT[31:0], Rt2 = CNTVCT[63:32]. */
	__asm__ __volatile__("mrrc p15, 0, %0, %1, c14" : "=r"(lo), "=r"(hi));
	return ((unsigned long long)hi << 32) | lo;
}

/* **1000: the counter the deadline is actually expressed in.** `msm8974_read_cntvct` above is
 * `mrrc p15, 0` - the ARM ARM's CNTPCT (physical); it equals the virtual counter on this board only
 * because nothing writes CNTVOFF, and the name/comment above predate that distinction. The tick's
 * deadline lives in `CNTV_TVAL` and the comparator counts down from **CNTVCT** (`mrrc p15, 1`, the
 * opc1 the entry image's own timebase read uses, `src/entry/entry_timebase.c:217`), so the handler's
 * elapsed readback takes its delta from THIS one - a delta against the wrong twin would still be a
 * number, but not the deadline's own reading ([[mi4-one-value-two-definitions]]). */
static inline uint32_t
msm8974_read_cntvct_lo(void)
{
	unsigned int lo, hi;
	__asm__ __volatile__("mrrc p15, 1, %0, %1, c14" : "=r"(lo), "=r"(hi));
	return (uint32_t)lo;
}

/* `CNTV_TVAL` (c14, c3, 0) and `CNTV_CTL` (c14, c3, 1), the two registers `msm8974_tick_arm` writes -
 * read here so an anomaly record says what the deadline and the control word ACTUALLY held at the
 * entry, rather than what this file believes it wrote. `CNTV_CTL`: bit 0 ENABLE, bit 1 IMASK,
 * bit 2 ISTATUS. */
static inline uint32_t
msm8974_read_cntv_tval(void)
{
	uint32_t v;
	__asm__ __volatile__("mrc p15, 0, %0, c14, c3, 0" : "=r"(v));
	return v;
}

static inline uint32_t
msm8974_read_cntv_ctl(void)
{
	uint32_t v;
	__asm__ __volatile__("mrc p15, 0, %0, c14, c3, 1" : "=r"(v));
	return v;
}

static inline void
msm8974_barrier(void)
{
	__asm__ __volatile__("dsb sy" ::: "memory");
}

/* ---- console ---- */

void
msm8974_putc(char c)
{
	/* 974: the capture sink, not a private ring - see the extern's derivation above. One character
	 * per call; the sink appends to the RAM console block the runner reads. */
	entry_os_console_char((int)(unsigned char)c, 2u);
}

static int
msm8974_getc(void)
{
	return -1;              /* no receive path claimed */
}

static void
msm8974_uart_init(void)
{
	/* Nothing to claim: no hardware UART success is asserted here. */
}

/* ---- interrupt controller ---- */
/*
 * Agree with the entry image, do not reconfigure it. The GIC distributor and CPU interface were
 * programmed by src/entry/entry_gic.c before the jump; a second initialiser that reprogrammed them
 * would be the "lower rung's side effect poisons the rung above" defect. The handler is the standard
 * acknowledge/EOI pair at the offsets the image already uses.
 */
static void
msm8974_interrupt_init(void)
{
	/* Intentionally empty: see above. */
}

/* ---- 1000: the late delivery, recorded direct ---- */

/* The first four deliveries later than a full period, in full. What each key reads:
 *   _n    the ordinal (incremented BEFORE anything is written - the `entry_timer_edge_probe` shape -
 *         so a repeated invocation cannot publish the same slot twice);
 *   _d    elapsed CNTVCT ticks since the last re-arm: ~19,200 healthy, a near-multiple of it masked;
 *   _tval what `CNTV_TVAL` held AT the entry (consistent with `_d` reads as the countdown running
 *         through the whole gap = masked delivery; a value it could not have produced = a second
 *         writer armed the deadline);
 *   _ctl  `CNTV_CTL` at the entry (bit 1 IMASK set or bit 0 ENABLE clear = the second writer's
 *         second signature).
 * DIRECT (`entry_write_kv`, uncounted): 999's counted channel saturated before pid 1 even ran, so an
 * anomaly record's ABSENCE has to be evidence again ([[mi4-998-g1-confirmed-map-file-kr0]]'s law),
 * and the collapse's own records have to survive the cap this time. */
static uint32_t g_msm8974_tick_anom_published;

static void
msm8974_tick_anom_publish(uint32_t d, uint32_t tval, uint32_t ctl)
{
	if (entry_live_ready() == 0u)
		return;
	g_msm8974_tick_anom_published++;
	if (g_msm8974_tick_anom_published > 4u)
		return;
	entry_write_kv("xnu_live_tick_anom_n", g_msm8974_tick_anom_published);
	entry_write_kv("xnu_live_tick_anom_d", d);
	entry_write_kv("xnu_live_tick_anom_tval", tval);
	entry_write_kv("xnu_live_tick_anom_ctl", ctl);
}

/* One delivery's lateness, sampled from the handler's timer branch BEFORE its EOI - so the reading is
 * about the DELIVERY, not this handler's duration. Healthy: `d` = the 19,200-tick period plus the
 * vector's own latency (tens to hundreds of ticks), nothing published, cost = one CNTVCT read, a
 * subtract and two compares. Late by more than one full period: the two raw readings the A/B question
 * needs - `CNTV_TVAL` and `CNTV_CTL` AT the entry - then the first four events go out in full, while
 * `_anom_count`/`_max_d` aggregate everything for the summaries. 999's collapse (24 deliveries over
 * >= 2.4 s) would be a `d` near a whole multiple of 19,200 with `_tval` consistent with it - or not,
 * which is exactly why both raw words are recorded. `noinline` for the build check's sake: the call
 * must be a call in the handler's disassembly (the m168 lesson about checks that measure a shape). */
static __attribute__((noinline)) void
msm8974_tick_sample(void)
{
	uint32_t d = msm8974_read_cntvct_lo() - g_msm8974_tick_arm_cntv;

	if (d > g_msm8974_tick_max_d)
		g_msm8974_tick_max_d = d;
	if (g_msm8974_tick_ticks == 0u || d <= 2u * (uint32_t)g_msm8974_tick_ticks)
		return;
	g_msm8974_tick_anom_count++;
	msm8974_tick_anom_publish(d, msm8974_read_cntv_tval(), msm8974_read_cntv_ctl());
}

static void
msm8974_handle_interrupt(void *context)
{
	volatile unsigned int *gicc_iar;
	volatile unsigned int *gicc_eoir;
	unsigned int id;

	if (g_msm8974_gicc == 0) {
		/* No mapping: reading the physical address here is the 981 fault, so do nothing rather
		 * than fault in interrupt context. The line cannot have been enabled before the map
		 * (see `msm8974_gic_map`'s caller), so this is unreachable in a correct boot; it is the
		 * honest guard for the one path that would otherwise repeat the abort. */
		return;
	}
	gicc_iar  = g_msm8974_gicc + (MSM8974_GICC_IAR  / sizeof(unsigned int));
	gicc_eoir = g_msm8974_gicc + (MSM8974_GICC_EOIR / sizeof(unsigned int));

	/* A read of IAR *acknowledges* (`src/entry/entry_gic.h:81`); the id is the low 10 bits. */
	id = *gicc_iar & 0x3ffu;
	if (id == MSM8974_GIC_SPURIOUS) {
		/* The architecture says a spurious read acknowledges nothing, so it is NOT EOI'd: writing
		 * EOIR with 0x3ff would acknowledge an interrupt that does not exist. */
		return;
	}

	if (id == MSM8974_GIC_TIMER_INTID) {
		/* **1000: the delivery's lateness, sampled at the entry - before the EOI below, so the
		 * reading is about the DELIVERY and not this handler's duration.** One call, one CNTVCT
		 * read and two compares on the healthy path; on an anomalous one it also reads
		 * `CNTV_TVAL`/`CNTV_CTL` and publishes - see `msm8974_tick_sample`. */
		msm8974_tick_sample();

		/* **The tick.** EOI *first*, then service - the ordering `src/entry/entry_irq.c:58` chose:
		 * EOI'ing before the work means a second expiry during it is a line already back in the
		 * distributor's hands, not one the CPU interface still believes is active. `rtclock_intr`
		 * runs `etimer_intr`, which services the kernel's deadline list and is what advances
		 * `mach_absolute_time` - the thing every `wait_timer`/`timer_call` in the system is waiting
		 * on, and the thing 984's park proved absent. */
		*gicc_eoir = id;
		msm8974_barrier();
		rtclock_intr((arm_saved_state_t *)context);

		/* **985: re-arm the hardware, because on this build nothing else does.** The comment above
		 * used to say `rtclock_intr`'s `setPop` re-arms the countdown - true only when
		 * `CONFIG_TICKLESS` is defined, and it is defined NOWHERE in this tree, so `setPop`'s
		 * `pe_arm_set_timer_enabled` blocks are compiled out and this is the ONLY place the periodic
		 * tick can be reset. Arming here, after the service work, also means a re-assert that
		 * arrives during `etimer_intr` is already back in the distributor's hands rather than a
		 * deadline re-programmed underneath the handler. Each tick is counted and published on its
		 * powers of two, so the log distinguishes "the line fired once and stopped" from "the tick
		 * is running" without a per-tick record filling the channel. */
		++g_msm8974_tick_count;
		if ((g_msm8974_tick_count & (g_msm8974_tick_count - 1u)) == 0u) {
			entry_live_write("xnu_live_tick_irq", g_msm8974_tick_count);
#if STAGE90_XNU_STALL
			/* **1003: the stall sample, on the tick's own powers-of-two ladder.** Same cadence as
			 * the `xnu_live_tick_irq` above, so the samples are ~1 ms apart from each other at the
			 * doubling and get finer as the run goes - and the last samples bracket the frontier.
			 * `context` is the frame the interrupted CPU was in; a user-mode cpsr means the sample
			 * is a USER pc, which is the pid-1 question this rung asks. */
			entry_note_stall_sample((const uint32_t *)context);
#endif
		}
		msm8974_tick_arm();
		return;
	}

	/* Any other line: acknowledged and EOI'd, but not serviced here - this board PE wires no
	 * driver dispatch (that is `irq_iokit_dispatch`, reached once a driver registers). EOI'ing is
	 * still the correct minimum: a line left un-EOI'd would re-enter forever. */
	*gicc_eoir = id;
	msm8974_barrier();
}

/* ---- the 999 timer discriminator: the two edges that could end the freeze ---- */

/* WHY THIS EXISTS, AND WHY ASSEMBLY HAS TO CALL IT. 998's timer chain stops delivering after tick 64
 * with a measured A fact (ISENABLER0 bit 20 CLEAR with no writer in a 4.78-7.30 s window) against a
 * competing B model (the kernel entered with I=1 and never unmasked). The discriminator is
 * presence-of-edge, so the hooks sit exactly AT the two candidate edges, read by value:
 *
 *   site 1 - `osfmk/arm/machine_routines_asm.s`'s `ml_set_interrupts_enabled`, the instruction after
 *            its `cpsie i` (:151-153). The lead's citation pointed at a C wrapper at
 *            `machine_routines.c:143-146` - there is none; that region is `ml_init_interrupt` CALLING
 *            this function, and the whole body is assembly, so the hook is a `bl` in the asm.
 *   site 2 - `osfmk/arm/cswitch.s`'s `thread_exception_return` -> `return_to_user` (:144, :167-170).
 *            The citation's `status.c:112-123` is `sanitise_cpsr`, a different function (it builds the
 *            user CPSR at thread-bootstrap time); `thread_exception_return` exists only as assembly.
 *            It is also the shared tail of `thread_syscall_return` (:137), so a record here is any
 *            return-to-user, whichever vector put the thread in the kernel.
 *
 * Each call publishes three direct, cap-exempt records, at most four per site - the per-site ordinal
 * is incremented BEFORE anything is written, so the re-entry site 1 invites by design (it runs with
 * interrupts ON, that is the edge) cannot publish the same slot twice. `_cpsr` is the CPSR read back
 * after the edge: for site 1 the asm's own `mrs` after the `cpsie`; for site 2 the word the edge is
 * about to install into `spsr_cxsf` (`[uss + 0x40]`), because the post-edge CPSR lives in user mode
 * and no kernel hook can read it. `_tick` is the raw delivery count above, so the reading compares
 * counts - never wall-clock. */
void
entry_timer_edge_probe(uint32_t site, uint32_t cpsr)
{
    static uint32_t mlie_published;
    static uint32_t texr_published;
    uint32_t raw = (uint32_t)g_msm8974_tick_count;

    /* The readiness pattern `entry_wdt_pet` uses, by value: nothing is published before the live
     * channel is up (state 1), and edges taken during the healthy clock publish nothing either. */
    if (entry_live_ready() == 0u)
        return;
    if (raw < MSM8974_TICK_FROZEN_AT)
        return;

    if (site == 1u) {
        if (mlie_published >= 4u)
            return;
        mlie_published++;
        entry_write_kv("xnu_live_edge_mlie_seq", mlie_published);
        entry_write_kv("xnu_live_edge_mlie_cpsr", cpsr);
        entry_write_kv("xnu_live_edge_mlie_tick", raw);
    } else {
        if (texr_published >= 4u)
            return;
        texr_published++;
        entry_write_kv("xnu_live_edge_texr_seq", texr_published);
        entry_write_kv("xnu_live_edge_texr_cpsr", cpsr);
        entry_write_kv("xnu_live_edge_texr_tick", raw);
    }
}

/* `entry_tick_summary`: the timer's aggregates, published on request through the DIRECT channel, so a
 * caller that has to survive the cap can read the tick's health at its own moment instead of at the
 * next counted record. Two callers, both places the counted channel goes quiet: the exec-done point
 * (`entry_stubs.c` `entry_note_exec_returned` - 999's cap tombstone sat immediately beside it) and
 * the wdt pet's sparse publish gate (`entry_trace.c` - the post-cap live path). `_maxd` is the worst
 * elapsed ever seen (healthy: tens to hundreds of ticks; 999's collapse: near-multiples of 19,200),
 * `_anom` counts deliveries later than a full period, `_count` is every delivery since boot - the
 * same number the handler's powers-of-two ladder publishes, read here WITHOUT its sampling rule. */
void
entry_tick_summary(void)
{
    if (entry_live_ready() == 0u)
        return;
    entry_write_kv("xnu_live_tick_sum_count", g_msm8974_tick_count);
    entry_write_kv("xnu_live_tick_sum_anom", g_msm8974_tick_anom_count);
    entry_write_kv("xnu_live_tick_sum_maxd", g_msm8974_tick_max_d);
}

/* ---- timebase ---- */

static void
msm8974_timebase_init(void)
{
	/*
	 * Read the frequency; do NOT block on an interrupt or a device. If the entry image left a sane
	 * CNTFRQ, keep it; otherwise fall back to our measured 19.2 MHz so the tick scale is never
	 * zero. The value is then PUBLISHED, not kept local: `rtclock_init` -> `rtclock_bus_configure`
	 * (`osfmk/arm/rtclock.c:317,293-297`) reads `gPEClockFrequencyInfo.timebase_frequency_hz` and
	 * panics if it is 0. Before this change the frequency lived only in the file-local
	 * `msm8974_cntfrq`, and `rtc_configure` - defined in the image at 0x8001b5e0 - had NO caller, so
	 * `rtclock_sec_divisor` stayed 0 and the second panic fired too. Both are set here, from ONE
	 * value, so the tick that `msm8974_get_timebase` divides by and the tick the RTC scales by can
	 * never disagree. [[mi4-one-value-two-definitions]]
	 */
	unsigned int frq;
	__asm__ __volatile__("mrc p15, 0, %0, c14, c0, 0" : "=r"(frq));
	msm8974_cntfrq = (frq != 0) ? (unsigned long long)frq : MSM8974_CNTFRQ_HZ;

	gPEClockFrequencyInfo.timebase_frequency_hz = (unsigned long)msm8974_cntfrq;
	rtc_configure((uint64_t)msm8974_cntfrq);   /* UNCONDITIONAL: rtclock_sec_divisor must never be 0 */
}

uint64_t
msm8974_get_timebase(void)
{
	/*
	 * TICKS, not nanoseconds. The generic RTC layer divides by the frequency itself:
	 *   osfmk/arm/rtclock.c:82-93  rtclock_timer_scale_convert():
	 *       nsecs = pe_arm_get_timebase(NULL);        // :85 - THIS call
	 *       ticks = (nsecs * NSEC_PER_SEC / tscFreq); // :91 - the ns conversion
	 * tscFreq == gPEClockFrequencyInfo.timebase_frequency_hz (rtclock.c:299,302), which
	 * timebase_init publishes as 19 200 000 below. So get_timebase must hand back RAW
	 * CNTVCT ticks, like the apq8060 peer - NOT a pre-scaled ns value. Before this,
	 * this function ALSO multiplied by NSEC_PER_SEC / 19.2e6, so mach_absolute_time
	 * came out 52.0833x too large (NSEC_PER_SEC / 19200000), and every scheduler/idle
	 * deadline derived from it was pushed past the SoC watchdog's 25 s reset - the 980
	 * run idled and was reset without the idle wake ever firing.
	 * [[mi4-one-value-two-definitions]].
	 */
	return (uint64_t)msm8974_read_cntvct();
}

static uint64_t
msm8974_timer_value(void)
{
	return (uint64_t)msm8974_read_cntvct();
}

static void
msm8974_timer_enabled(int enable)
{
	/* **985: the timer-enable hook, made real.** This was an empty stub, which was honest while
	 * nothing called it - `pe_arm_set_timer_enabled` is invoked only from `setPop`'s
	 * `#if CONFIG_TICKLESS` blocks, and `CONFIG_TICKLESS` is defined nowhere in this tree, so on D13
	 * the whole tickless rearm is compiled out and this hook is dormant. It is implemented now (arm on
	 * enable, disarm on disable) so that the ONE definition of "start/stop the board's countdown"
	 * lives here rather than being written a second time at the boot-time call site below - and so
	 * that if a future build turns tickless on, the kernel's own rearm reaches real hardware instead
	 * of a no-op. The boot-time arming (see `PE_init_SocSupport_msm8974`) is what makes the tick run
	 * on THIS build. [[mi4-one-value-two-definitions]] */
	if (enable) {
		msm8974_tick_start();
	} else {
		__asm__ __volatile__("mcr p15, 0, %0, c14, c3, 1" :: "r"(0u));  /* CNTV_CTL: disable */
	}
}

/* ---- the table, and the single symbol the generic PE calls ---- */

void
PE_init_SocSupport_msm8974(void)
{
	gPESocDispatch.uart_getc        = msm8974_getc;
	gPESocDispatch.uart_putc        = msm8974_putc;
	gPESocDispatch.uart_init        = msm8974_uart_init;

	gPESocDispatch.interrupt_init   = msm8974_interrupt_init;
	gPESocDispatch.timebase_init    = msm8974_timebase_init;

	gPESocDispatch.handle_interrupt = msm8974_handle_interrupt;
	gPESocDispatch.get_timebase     = msm8974_get_timebase;

	gPESocDispatch.timer_value      = msm8974_timer_value;
	gPESocDispatch.timer_enabled    = msm8974_timer_enabled;

	/* **982: map the GIC CPU interface before any interrupt can be dispatched.** This runs at
	 * `PE_init_SocSupport` (`pexpert/arm/common/pe_init.c:146`), which precedes
	 * `pe_arm_init_interrupts` (`:182`) - the call that would enable a line and make
	 * `msm8974_handle_interrupt` reachable - so the handler's mapped base is set by the time it
	 * matters. Doing it here rather than in `msm8974_interrupt_init` is deliberate: the interrupt
	 * init is empty *on purpose* (it must not reconfigure a GIC the entry image owns), and this is
	 * plumbing for THIS file's own reads, not a device init. */
	msm8974_gic_map();

	/* framebuffer_init is left NULL on purpose: no display rung is claimed here. */
	msm8974_timebase_init();

	/* **985: start the board's tick, here, once.** This is the ONE site that arms the countdown,
	 * because the generic ARM layer does not: `setPop`'s rearm is inside `#if CONFIG_TICKLESS`
	 * (`osfmk/arm/rtclock.c:129-144`), and `CONFIG_TICKLESS` is defined nowhere in this tree, so on
	 * D13 nothing in the kernel ever programs `CNTV`. Without it the timer PPI never fires,
	 * `rtclock_intr` -> `etimer_intr` never runs, and every deadline sleep (`thread->wait_timer`,
	 * `timer_call`) parks forever - 984's `waitForService("IORTC")` hang. Arming here is a claim about
	 * ORDER, and it is safe at this point by two reads: `PE_init_SocSupport` is reached through
	 * `PE_init_platform(TRUE)` at `osfmk/arm/arm_init.c:178`, which is *after* `cpu_init()`
	 * (`arm_init.c:130`) has run `timer_call_initialize_queue(&cdp->rt_timer.queue)` - so the queue
	 * `etimer_intr` walks is initialized - and after `arm_vm_init`, so the GIC VA this file maps just
	 * above is the one the MMU will walk. `msm8974_timer_enabled` keeps the SAME one definition for a
	 * future tickless build; this call is what makes it run on THIS build. [[mi4-one-value-two-definitions]] */
	msm8974_tick_start();
}

void
PE_init_SocSupport_stub(void)
{
	PE_early_puts("PE_init_SocSupport: initializing for MSM8974 (cancro)\n");
	PE_init_SocSupport_msm8974();
}

#endif /* BOARD_CONFIG_MSM8974 */