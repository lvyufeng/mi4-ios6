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

static volatile unsigned long long msm8974_cntfrq = MSM8974_CNTFRQ_HZ;

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
	volatile unsigned int *gicc_iar  = (volatile unsigned int *)(MSM8974_GICC_BASE + MSM8974_GICC_IAR);
	volatile unsigned int *gicc_eoir = (volatile unsigned int *)(MSM8974_GICC_BASE + MSM8974_GICC_EOIR);
	unsigned int id;

	(void)context;
	id = *gicc_iar & 0x3ffu;
	if (id != MSM8974_GIC_SPURIOUS) {
		*gicc_eoir = id;    /* a read of IAR acknowledged; only a real id is EOI'd */
	}
	msm8974_barrier();
}

/* ---- timebase ---- */

static void
msm8974_timebase_init(void)
{
	/*
	 * Read the frequency; do NOT block on an interrupt or a device. If the entry image left a sane
	 * CNTFRQ, keep it; otherwise publish our measured 19.2 MHz so the tick scale is never zero.
	 */
	unsigned int frq;
	__asm__ __volatile__("mrc p15, 0, %0, c14, c0, 0" : "=r"(frq));
	msm8974_cntfrq = (frq != 0) ? (unsigned long long)frq : MSM8974_CNTFRQ_HZ;
}

uint64_t
msm8974_get_timebase(void)
{
	return (uint64_t)((msm8974_read_cntvct() * 1000000000ULL) / msm8974_cntfrq);
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