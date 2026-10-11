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

static volatile unsigned long long msm8974_cntfrq = MSM8974_CNTFRQ_HZ;

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
		/* **The tick.** EOI *first*, then service - the ordering `src/entry/entry_irq.c:58` chose
		 * and the reason is its own: `rtclock_intr` re-arms the countdown (through
		 * `setPop`->`ml_set_decrementer`), so EOI'ing before that work means a second expiry during
		 * it is a line already back in the distributor's hands, not one the CPU interface still
		 * believes is active. `rtclock_intr` runs `etimer_intr` and is what advances
		 * `mach_absolute_time`; without this call the countdown would never re-arm. */
		*gicc_eoir = id;
		msm8974_barrier();
		rtclock_intr((arm_saved_state_t *)context);
		return;
	}

	/* Any other line: acknowledged and EOI'd, but not serviced here - this board PE wires no
	 * driver dispatch (that is `irq_iokit_dispatch`, reached once a driver registers). EOI'ing is
	 * still the correct minimum: a line left un-EOI'd would re-enter forever. */
	*gicc_eoir = id;
	msm8974_barrier();
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
	(void)enable;
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
}

void
PE_init_SocSupport_stub(void)
{
	PE_early_puts("PE_init_SocSupport: initializing for MSM8974 (cancro)\n");
	PE_init_SocSupport_msm8974();
}

#endif /* BOARD_CONFIG_MSM8974 */