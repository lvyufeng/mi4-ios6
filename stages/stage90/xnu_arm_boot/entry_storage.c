/*
 * 692: the storage line's first on-device act - one section, one clock gate read before it, and six
 * register loads behind that gate.
 *
 * **Why this is the next step and not a new line.** 529, 530, 531 and 532 are all host-side readings of
 * this same controller: 529 showed that this tree has no HFS to mount with, 530 showed what a block
 * device must register, 531 read the vendor driver's programming surface - two windows, named
 * `hc_mem` at `0xf9824900` and `core_mem` at `0xf9824000`, and the mode bit in `CORE_HC_MODE 0x78` that
 * has to say SDHCI before a single standard register means anything - and 532 read the prerequisite
 * under all of it: neither window is in any table this image builds, and **both are inside the same
 * 1 MB section**, `0xf9824000 >> 20 == 0xf9824900 >> 20 == 0xF98` (3992). So 532 section 6's step 2 is
 * one `entry_mmio_section(0xf9824000, 0xf9824000, ...)` call and not two, and 532 section 6's step 3 is
 * three words read out of `core_mem` before anything is written to it. This probe is those two steps.
 *
 * **It reads, and it does not write - and that is the arm's design rather than a first cut.** The
 * vendor's own probe (`sdhci-msm.c:2841-2868`) opens with `writel_relaxed(0, core_mem + CORE_HC_MODE)`,
 * then sets `CORE_SW_RST` (bit 7 of `CORE_POWER`, offset 0), polls it clear, then sets `HC_MODE_EN`, and
 * 531 section 8 adds the third one from the other side: `POWER_CONTROL 0x29` is avoided by the driver
 * itself because on this SoC **writing 0 to it *is* a bus-off request**. Every one of those is a write
 * that changes the state of the medium's controller, and none of them is needed to answer the question
 * this arm asks, which is whether the block is there, whether it is clocked and what it already says.
 * `xnu_live_storage_writes` is published as `0` for exactly this reason: the arm declares the count of
 * writes it made to the controller, so "nothing was written" is a reading in the log rather than a
 * property a reader has to take from this comment.
 *
 * **The gate, why it comes first, and 692's measurement of what it costs.** A load from a block whose
 * clock is off is not a fault on this SoC's fabrics - it is a busy-wait that nothing ends, which is
 * the one failure this project cannot read a log out of (there is no return, so `ram_console` is never
 * recovered). 531 section 8 read SDCC1's two gates out of the vendor clock driver as `clk_ops_branch`
 * clocks, i.e. `BIT(0)` into their own CBCR, and `SDCC1_BCR 0x04C0` (`clock-8974.c:244`) gives the pair
 * at `0xFC4004C0` (BCR) and `0xFC4004C4` (CBCR) - the `+4` being the project's own measurement rather
 * than the file's, since 528 section 8 read `BLSP1_UART2_BCR 0x0700`'s gate at `0xFC400704`. So the
 * gate read is taken **before** the storage block is installed: a gate of 0 skips the six loads and
 * publishes that it did, which turns a probable press-losing hang into a reading. The interlock can
 * only ever fire in the safe direction - if the offset is wrong the word reads as something else and
 * the arm proceeds exactly as it would have without it.
 *
 * **And 692 pressed that, and the gate read is what died.** The published claim was that `0xFC4` is
 * already mapped "proved by `entry_epilogue`'s own PS_HOLD store at `0xfc4ab000` on every returning
 * run" - a *store inside a code path* read as evidence that an *address is mapped*. It is false, and
 * the arm's own log is the disproof: `xnu_live_sleh_far_frame = 0xfc4004c0`,
 * `fsr_frame = 0x00000005` (a section translation fault, on a read), `pc = 0x8000d0c4` =
 * `entry_storage_probe+0xcc` = `ldr r1, [r3, #1216]` with `r3 = 0xfc400000`, and the panic's own
 * `r0 = 0x80487fc8` is the pointer to the string `xnu_live_storage_bcr`. **So this arm's first act is
 * one more `entry_mmio_section`, for the GATE's megabyte, and the storage block's install follows it
 * only once the gate has said the branch is on.** The two installs cannot collide: `0xFC4` and `0xF98`
 * are different L1 indices in the same table, so 532 section 3.2's occupied-slot refusal cannot fire.
 *
 * **And that megabyte is the reset path's as well.** `entry_epilogue`'s two stores are at `0x0fa0065c`
 * and `0xfc4ab000` - **two different unmapped megabytes** - and 684 measured the first faulting on
 * 678's arm while 692's own ninth abort shows the payload's deliberate ending faulting at `0x0fa0065c`
 * too. `0xfc4ab000` shares this arm's `0xFC4` index, so `xnu_live_storage_gcc_map = 1` beside a
 * readable `_bcr` is also the measurement that the PS_HOLD half of the reset path becomes reachable -
 * the repair 684 named, taken here as a side effect of the gate's own mapping and not as a second
 * experiment.
 *
 * **The four refusals are the mapper's, and they are not one reading.** `entry_mmio_section` returns 0
 * for `g_live_state != 1` (no live channel), for a table outside the kernel's window, for an index past
 * the table, and for an **occupied** slot - the last being a skip and not a clobber, which is the one
 * 532 section 3.2 says a two-call arm trips. **This arm makes two calls, at two different indices, so
 * that refusal cannot be how the second one ends** - and each call publishes its own four numbers
 * (`xnu_live_storage_gcc_map`/`_slot_before`/`_desc` for the GATE's megabyte,
 * `xnu_live_storage_map`/`_slot_before`/`_desc` for the storage block's, plus `_l1`/`_l1_moved` read at
 * the second), because 532 section 6's step 2 is that the arm must be able to tell them apart. And when
 * an install refuses, this probe reads **no** register through it - the GIC probe's own rule, and the
 * reason is the same: a load through a translation this arm cannot vouch for is a fault, not a
 * measurement. 692 measured that such a fault does come back with a log; the rule is about not
 * spending the arm's reading on an address whose descriptor this run did not write.
 *
 * **696: and then the block answered, and the answer made the next act a WRITE.** 694 pressed the
 * read-only probe and all twenty-eight keys came back, none through a fault: both windows are reachable
 * through a descriptor this arm writes, the branch clock is enabled (`_cbcr = 0x00004ff1`), the block
 * answers (`_mci_version = 0x10000011`), and **`HC_MODE_EN` is CLEAR** (`_hc_mode = 0x00002000`). So
 * 531 section 6's "prerequisite or re-do" is answered in the direction that costs a write, and this
 * file gains the vendor's own bring-up - four stores, one bounded poll, and a readback after each store
 * - as **rung 2** of the switch above it. It is the first act in this project that writes to a device
 * block rather than reading one, and everything about its shape is a consequence of that:
 *
 *   * the **gate now guards stores**, where it was written to guard loads, and a load from an unclocked
 *     block is still the failure no bound can end (see the poll's comment);
 *   * the sequence is **pre-registered** as a write, with its cells and its failure path, in
 *     `docs/experiments/experiment-696-…`;
 *   * the count of stores is **published** (`xnu_live_storage_writes`) and it is a number about this
 *     arm's behaviour rather than about what the file contains - `0` on every path that refuses;
 *   * and `POWER_CONTROL 0x29` is still not touched, because writing 0 to it *is* a bus-off request.
 */
#include <stdint.h>

#include "entry_storage.h"
#include "entry_timebase.h"   /* 696: stage90_cntvct_read, for the reset poll's time bound */

/*
 * **696: the switch is a rung, and the `#error` says so where a `-D` would otherwise reach the
 * preprocessor as a value nobody defined.** `STORAGE_PROBE` was a flag - 0 the object links and
 * compiles to nothing, 1 the read-only probe - and the mode sequence is a *different kind of act*
 * (four stores to the controller rather than loads from it), so it gets its own value and the three
 * values are one ladder: how far up the storage line this image goes. The record is the only place a
 * reader learns which rung was built, which is why a value above the ladder is refused here rather
 * than shaping an image whose switches claim something else.
 *
 * **698: the fourth value, and it is a return to the read-only kind.** Rung 2 is the first arm in this
 * project that writes to a device block; rung 3 adds **the standard register file's census** and no
 * store at all, because the next act on this path - the driver's own `sdhci_reset(SDHCI_RESET_ALL)` -
 * changes four of the registers this census reads, and a before-value can only be taken before. The
 * ladder is therefore not "how much does this arm do" but "how far up the line", and a rung that adds
 * reads after a rung that added writes is the shape the ladder was built to allow.
 */
#ifndef STAGE90_XNU_STORAGE_PROBE
#define STAGE90_XNU_STORAGE_PROBE 0
#endif
#if STAGE90_XNU_STORAGE_PROBE < 0 || STAGE90_XNU_STORAGE_PROBE > 6
#error "STAGE90_XNU_STORAGE_PROBE is a rung: 0 = inert, 1 = the read-only probe, 2 = the probe and the vendor's mode sequence (four stores to the controller), 3 = 2 plus the standard register file's census (ten reads, no store), 4 = 3 plus the driver's own SDHCI_RESET_ALL (ONE byte store to SOFTWARE_RESET 0x2F, plus a bounded poll of that same byte) - the rung that writes through hc_mem for the first time - 5 = 4 plus the CLOCK SURFACE read at its own widths (the GCC's four SDCC1 branches and the apps root's five RCG words, plus CORE_VENDOR_SPEC 0x10C), a rung of reads that stores NOTHING anywhere, and 6 = 5 plus THE DRIVER'S FIRST CLOCK SET (sdhci_msm_set_clock at 400 kHz): four CBCR read-modify-writes of BIT(0) on the GCC each followed by a bounded halt check, two CORE_VENDOR_SPEC 0x10C read-modify-writes (MCLK select <- DFLT, HC_SELECT_IN cleared), and the standard's two CLOCK_CONTROL halfwords with the stability poll between them - SIX stores and NO rate, because sup_clock == msm_host->clk_rate on the first call (experiment-706 section 2) - and it writes neither BCR 0x04C0 nor any RCG word, nor POWER_CONTROL 0x29. A value above the ladder is refused here rather than shaping an image whose switches claim something else."
#endif

/*
 * **The declarations are outside the `#if`, and 692 measured why they have to be.** The no-op arm of
 * `ST_LIVE` below still evaluates its arguments, so an `extern` inside the `#if` is a name the untraced
 * build cannot see - and that is not hypothetical: `entry_gic.c` carried its five in the guarded arm
 * since 484 and would not compile at `STAGE90_ENTRY_GIC_TRACED=0`, which is exactly what
 * `STAGE90_ENTRY_TRACE=0` sets. That is repaired in the same step, and this file is written the way the
 * repair leaves its sibling. A declaration is not code, so this costs no byte of any traced image.
 */
extern void entry_live_write(const char *key, uint32_t value);
extern uint32_t g_live_state;
/* 484's four numbers, read by `entry_mmio_section` at the install - see `entry_gic.c`. */
extern uint32_t g_live_mmio_l1;
extern uint32_t g_live_mmio_l1_moved;
extern uint32_t g_live_mmio_ttbr0;
extern uint32_t g_live_mmio_ttbr1;
#if STAGE90_XNU_STORAGE_PROBE
#define ST_LIVE(key, value) entry_live_write((key), (uint32_t)(value))
#else
#define ST_LIVE(key, value) do { (void)(key); (void)(value); } while (0)
#endif

extern uint32_t entry_mmio_section(uint32_t va, uint32_t pa, uint32_t *slot_before_out,
                                   uint32_t *desc_out);

/* --------------------------------------------------------------------------------------------- */
/* The two windows, and the clock gate that decides whether they may be read                       */
/* --------------------------------------------------------------------------------------------- */

/*
 * **531 section 1's table, and every address in it comes from `msm8974.dtsi:500-528` - the node that
 * declares both windows by name** (`reg = <0xf9824900 0x11c>, <0xf9824000 0x800>`;
 * `reg-names = "hc_mem", "core_mem"`). The four offsets are the vendor driver's own `#define`s, quoted
 * with the line each one is read from rather than retyped: `CORE_POWER 0x0` and `CORE_SW_RST (1 << 7)`
 * (`sdhci-msm.c:59-60`), `CORE_MCI_DATA_CTRL 0x2C` (`:132`), `CORE_MCI_VERSION 0x050` (`:139`),
 * `CORE_HC_MODE 0x78` and `HC_MODE_EN 0x1` (`:55-56`). The two at the end are the SDHCI standard's own
 * - `HCI_VERSION 0x00` and `CAPABILITIES 0x40` - and they are read out of the *other* window precisely
 * because they are standard: a word at `hc_mem + 0x00` that carries a plausible spec version is
 * evidence the second window answers, which `core_mem`'s words cannot give on their own.
 *
 * **698: the name was wrong, and the two words were never the standard register file.** The paragraph
 * above says `HCI_VERSION 0x00`; the vendor's own header says `sdhci.h:27` `SDHCI_DMA_ADDRESS 0x00` and
 * `:241` `SDHCI_HOST_VERSION 0xFE`, and `sdhci-msm.c:2909` reads the version **at `0xFE`**. 697 is what
 * made that visible: the pre/post pair is `0x10 -> 0x00`, which is a *soft* register the core reset
 * clears (a DMA address) and not a version word, while `0x40`'s strapping constant did not move. So the
 * define is renamed to what it reads, the real version register is added, and the misnamed keys are
 * renamed beside it. **The readers were enumerated first** ([[mi4-one-value-two-definitions]], m699:
 * one rename, two readers - and a missed reader fails *silent*): two sites in this file and one line of
 * 694's document, and nothing in `stages/stage90`'s shell scripts or in `tools/` names either key.
 */
#define ST_CORE_MEM_BASE        0xf9824000u
#define ST_HC_MEM_BASE          0xf9824900u
#define ST_CORE_POWER           0x00u
#define ST_CORE_MCI_DATA_CTRL   0x2Cu
#define ST_CORE_MCI_VERSION     0x050u
#define ST_CORE_HC_MODE         0x78u
#define ST_SDHCI_DMA_ADDRESS    0x00u   /* sdhci.h:27 - the name this file carried as HCI_VERSION until 698 */
#define ST_SDHCI_CAPABILITIES   0x40u   /* sdhci.h:181 */
#define ST_SDHCI_CAPABILITIES_1 0x44u   /* sdhci.h:215 */
#define ST_SDHCI_MAX_CURRENT    0x48u   /* sdhci.h:217 */
#define ST_SDHCI_PRESENT_STATE  0x24u   /* sdhci.h:64  */
#define ST_SDHCI_HOST_CONTROL   0x28u   /* sdhci.h:76  - a BYTE, and at an offset no 32-bit access reaches */
#define ST_SDHCI_POWER_CONTROL  0x29u   /* sdhci.h:87  - a BYTE; read only, see the census below */
#define ST_SDHCI_CLOCK_CONTROL  0x2Cu   /* sdhci.h:100 - 16-bit */
#define ST_SDHCI_SOFTWARE_RESET 0x2Fu   /* sdhci.h:113 - a BYTE */
#define ST_SDHCI_RESET_ALL      0x01u   /* sdhci.h:114 - the mask the driver resets with, and it
                                         * is also the bit the poll below tests: the standard says
                                         * the bit is self-clearing */
#define ST_SDHCI_SLOT_INT_STAT  0xFCu   /* sdhci.h:239 - 16-bit */
#define ST_SDHCI_HOST_VERSION   0xFEu   /* sdhci.h:241 - 16-bit, and the register 697 left owed */
#define ST_SDHCI_CARD_PRESENT   0x00010000u /* sdhci.h:71 - must NOT be read as "no card" here, see below */

/*
 * **The widths are not a style choice, and the reason is an ARMv7 rule rather than a preference**: an
 * *unaligned* access to Device memory **faults**, and this arm's mappings are Strongly-ordered
 * shareable device sections (694 measured both descriptors' attribute). So `POWER_CONTROL 0x29`,
 * `HOST_CONTROL 0x28` and `SOFTWARE_RESET 0x2F` are byte registers at offsets no 4-byte access can
 * reach, `CLOCK_CONTROL 0x2C`/`SLOT_INT_STATUS 0xFC`/`HOST_VERSION 0xFE` are 16-bit with `0xFE` not
 * 4-aligned either, and a 32-bit load at any of those four addresses would fault exactly where 692
 * faulted - by *alignment* this time rather than by translation. The widths below are the vendor's own
 * accessors, quoted from `sdhci.c:98-128`'s register dump (`readw` for the version and the two 16-bit
 * registers, `readb` for the three bytes, `readl` for the rest) and from `:246`/`:259` for
 * `SOFTWARE_RESET`. `build_entry.sh` refuses the build if any access in this body breaks the rule.
 */
#define ST_CORE_PWRCTL_MASK     0xE0u   /* sdhci-msm.c:63, readl at :2946 */
#define ST_CORE_PWRCTL_CTL      0xE8u   /* sdhci-msm.c:65 - the vendor reads it 32-bit (:2879) and writes it
                                         * 8-bit at one site (:2069); 0xE8 is 4-aligned, so a 32-bit read is
                                         * legal and is what the read at :2879 does */

/* 531 section 8's SDCC1 pair, out of `clock-8974.c:244` and the `+4` the UART pair measured. */
#define ST_GCC_BASE             0xfc400000u
#define ST_SDCC1_BCR            0x04C0u
#define ST_SDCC1_CBCR           0x04C4u
#define ST_SDCC1_CLK_ENABLE     0x1u

/*
 * **704: the third block's surface, and the four names below are `clock-8974.c`'s own.** The file
 * already carried the BCR and one CBCR (`ST_SDCC1_BCR`/`ST_SDCC1_CBCR`, the pair the gate comes out of);
 * what rung 5 adds is the other three branches of the same controller and the **root clock generator**
 * their parent is, plus the one register on this path that is not a clock branch at all - all of it read
 * and none of it written, because the writer is `sdhci_msm_set_clock` and that is the next step
 * (`docs/experiments/experiment-704-...`, the pre-registration this rung is built from).
 *
 * **Why the AHB branch is the rung's open question.** `sdhci_msm_prepare_clocks` (`sdhci-msm.c:2315`)
 * enables `pclk` *and* `clk`, and `msm8974.dtsi`'s SDCC1 node binds `pclk` to this branch
 * (`SDCC1_AHB_CBCR`, `clock-8974.c:340`, `gcc_sdcc1_ahb_clk` at `:2339-2341`). **No run in this project
 * has ever read this register**, and the controller answers its register file today - which *suggests*
 * the branch is on, and is not the same statement. Its `branch_clk` is also the one that carries
 * `has_sibling = 1`, which the framework's own rule turns into `-EPERM` for `round_rate` and
 * `list_rate` (`clock-local2.c:444-446`, `:460-462`) - so enabling it is a different act from enabling
 * the apps branch and rung 6 will have to say so.
 */
#define ST_GCC_SDCC1_APPS_CBCR          0x04C4u   /* clock-8974.c:339 - the clk branch, and it is the
                                                  * SAME WORD the gate comes out of (`ST_SDCC1_CBCR`
                                                  * above): the gate is this branch's `BIT(0)`. The
                                                  * second name is 704's, kept because every rung-1..5
                                                  * reading is published under `_gate`/`_cbcr` and
                                                  * renaming those keys would move a record a press
                                                  * already answered */
#define ST_GCC_SDCC1_AHB_CBCR           0x04C8u   /* clock-8974.c:340 - the pclk branch */
#define ST_GCC_SDCC1_APPS_RCG           0x04D0u   /* clock-8974.c:140, :1584 - sdcc1_apps_clk_src's CMD_RCGR */
#define ST_GCC_SDCC1_CDCCAL_SLEEP_CBCR  0x04E4u   /* clock-8974.c:341 */
#define ST_GCC_SDCC1_CDCCAL_FF_CBCR     0x04E8u   /* clock-8974.c:342 */

/*
 * **The RCG's five words are four bytes apart and they are `clock-local2.c`'s own arithmetic**
 * (`:49-53`: `CMD_RCGR_REG(x) (*(x)->base + (x)->cmd_rcgr_reg)`, `CFG_RCGR_REG` `+0x4`, `M_REG` `+0x8`,
 * `N_REG` `+0xC`, `D_REG` `+0x10`), not an address anyone found in a table. All five are 4-aligned, so
 * they are 32-bit reads and 698's width census checks them for free.
 */
#define ST_RCG_CMD              0x00u
#define ST_RCG_CFG              0x04u
#define ST_RCG_M                0x08u
#define ST_RCG_N                0x0Cu
#define ST_RCG_D                0x10u

/*
 * **The bits, each quoted from the file that defines it.** The three CBCR bits are
 * `clock-local2.c:62-63` and `:67`; the BCR's one bit is `:66`; the RCG's four are `:61-65` and
 * `:68-71`. They are named here rather than shifted inline because §2 of the rung-5 pre-registration is
 * about exactly this: `CBCR_BRANCH_ENABLE_BIT` and `CBCR_BRANCH_OFF_BIT` are **two** bits about one
 * quantity, and this file carried only the first of them as "the gate" from 692 until 704.
 */
#define ST_CBCR_ENABLE_BIT      (1u << 0)     /* CBCR_BRANCH_ENABLE_BIT - what the driver WRITES */
#define ST_CBCR_OFF_BIT         (1u << 31)    /* CBCR_BRANCH_OFF_BIT    - what "running" means */
#define ST_CBCR_HW_CTL_BIT      (1u << 1)     /* CBCR_HW_CTL_BIT - set => the framework skips its halt check */
#define ST_BCR_ARES_BIT         (1u << 0)     /* BCR_BLK_ARES_BIT - the block reset, not on this path */
#define ST_RCG_ROOT_EN_BIT      (1u << 1)     /* CMD_RCGR_ROOT_ENABLE_BIT */
#define ST_RCG_UPDATE_BIT       (1u << 0)     /* CMD_RCGR_CONFIG_UPDATE_BIT - the bit rcg_update_config polls */
#define ST_RCG_ROOT_STATUS_BIT  (1u << 31)    /* CMD_RCGR_ROOT_STATUS_BIT */
#define ST_RCG_CFG_DIV_MASK     0x0000001Fu   /* CFG_RCGR_DIV_MASK, BM(4,0) */
#define ST_RCG_CFG_SRC_MASK     0x00000700u   /* CFG_RCGR_SRC_SEL_MASK, BM(10,8) */
#define ST_RCG_CFG_SRC_SHIFT    8u
#define ST_RCG_CFG_MND_MASK     0x00003000u   /* MND_MODE_MASK, BM(13,12) - 0x2 is the dual-edge value */

/*
 * **`CORE_VENDOR_SPEC 0x10C`, the fifth `core_mem` offset and the one register `sdhci_msm_set_clock`
 * reads before it decides anything** (`sdhci-msm.c:92`). It is a 4-aligned 32-bit register read with
 * `st_read32`; the two fields below are the only two the driver's own code touches (`:93-96`), and the
 * MCLK select is the one it writes on the non-HS400 path (`:2466-2494`). Rung 5 reads both and writes
 * neither, which is what makes them rung 6's before-values.
 */
#define ST_CORE_VENDOR_SPEC     0x10Cu        /* sdhci-msm.c:92 - the fifth offset in this window */
#define ST_VENDOR_PWRSAVE_BIT   (1u << 1)     /* CORE_CLK_PWRSAVE, :93 */
#define ST_VENDOR_MCLK_MASK     0x00000300u   /* CORE_HC_MCLK_SEL_MASK, :96 - BM(9,8) */
#define ST_VENDOR_MCLK_SHIFT    8u

/*
 * **696: the vendor's sequence, and every offset below is a `#define` in the same file it is read out
 * of (`sdhci-msm.c:55-60`) rather than retyped.** `ST_HC_MODE_EN` and `ST_SDCC1_CLK_ENABLE` are the
 * same number and it is the same *bit position* - bit 0 - of two different registers: one selects
 * SDHCI mode in `CORE_HC_MODE` and the other enables the branch's clock in the CBCR. They are kept as
 * two names because the registers are two and neither bit's meaning follows from the other's, and the
 * arm's own pair of readings says the two are independent: the gate reads `0x00004ff1` (bit 0 set)
 * while `_hc_mode` reads bit 0 clear.
 */
#define ST_HC_MODE_EN           0x1u
#define ST_CORE_SW_RST          (1u << 7)
#define ST_FF_CLK_SW_RST_DIS    (1u << 13)
#define ST_CORE_PWRCTL_STATUS   0xDCu

/*
 * **The poll's bounds, and the sentence that must be read beside them: a bound cannot bound a read that
 * never returns.** A load from a block whose clock is off is a bus wait nothing ends on this SoC's
 * fabrics, so if the gate reading were wrong the *first* `CORE_POWER` read inside the poll would hang
 * and no number here would end it - which is why the sequence runs only with `gate == 1`, on a pair of
 * gate words 694 measured on hardware rather than on the offset's arithmetic. What the bounds do
 * address is the case they can: a reset that does not complete. The vendor polls to a 1 ms ceiling
 * (`readl_poll_timeout(..., 10, 1000)`, against a reset its own comment sizes at ~40 us); this ceiling
 * is 100x that, 1/20 of the fixture's 2000 ms park and 1/60 of this arm's 6 s ending, and it is a
 * *time* rather than a count because the rate is measured - `xnu_live_post_cntfrq` read 19,200,000 Hz
 * on 691's and 694's runs, which is 19200 ticks/ms.
 */
#define ST_MODE_RST_TICK_BUDGET 1920000u  /* 100 ms at 19.2 MHz */
#define ST_MODE_RST_STEPS_MAX   4096u     /* the backstop: ~126 ms of reads, so it cannot fire first */
#define ST_MODE_RST_INNER       1024u     /* device reads between two samples of the clock */
/*
 * **701: the driver's own poll bound, and it is the vendor's number rather than a new one.**
 * `sdhci.c:251-252` is the comment 'Wait max 100 ms' over `timeout = 100`, and the loop
 * `mdelay(1)`s per decrement (`:267`), so the driver's bound is **100 ms of wall clock** - the same
 * quantity `ST_MODE_RST_TICK_BUDGET` is, at the same 19,200,000 Hz 699's ending read out of `cntfrq`.
 * The arm's bound and the driver's bound being the same number is the point: a run that times out here
 * is a run in which the driver's own `Reset 0x%x never completed` path (`:260-264`) would have fired.
 */
#define ST_RST_TICK_BUDGET 1920000u  /* 100 ms at 19.2 MHz */
#define ST_RST_STEPS_MAX   4096u     /* the backstop: ~126 ms of byte reads, so it cannot fire first */
#define ST_RST_INNER       1024u     /* byte reads between two samples of the clock */

static uint32_t st_read32(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

/*
 * **698: the two narrower widths, and they exist because the register file is not uniform.** The four
 * offsets they reach (`0x28`, `0x29`, `0x2F` byte; `0x2C`, `0xFC`, `0xFE` halfword) are read that way by
 * the vendor's own accessors (`sdhci.c:98-128`), and the reason is not style: an unaligned access to a
 * Strongly-ordered device section **faults on ARMv7**, so a 32-bit read of `0x29` or of `0xFE` would die
 * on the bench exactly where 692 died - by alignment this time rather than by translation. The `ldrb`/
 * `ldrh` these compile to are the whole of the arm's protection here, and `build_entry.sh`'s alignment
 * census refuses the build if any access in this body breaks the rule.
 */
static uint8_t st_read8(uint32_t addr)
{
    return *(volatile uint8_t *)(uintptr_t)addr;
}

static uint16_t st_read16(uint32_t addr)
{
    return *(volatile uint16_t *)(uintptr_t)addr;
}

/*
 * **The store, and the `dsb sy` is what makes "the readback was taken after the store" a property of
 * this code rather than of the bus.** The vendor uses `writel_relaxed`/`readl_relaxed` and relies on
 * the interconnect; this image has one barrier already written down in the same shape (`entry_irq.c`'s
 * write helper) and the project's own reset path is specified as a store followed by `dsb sy`, so the
 * device write here is that shape too. Ten of them per run at most - the vendor's mode sequence's four,
 * then rung 6's four branch enables and two CORE_VENDOR_SPEC read-modify-writes - beside the byte store
 * of the reset and the two CLOCK_CONTROL halfwords, and every one is a read-modify-write (or an
 * already-set bit's own value) of a field the vendor's own driver writes, against a block whose clock
 * this arm has already read as enabled.
 */
static void st_write32(uint32_t addr, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)addr = value;
    __asm__ volatile ("dsb sy" ::: "memory");
}

#if STAGE90_XNU_STORAGE_PROBE >= 4
/*
 * **701: the byte store, and rung 4 is the reason it exists.** `SOFTWARE_RESET 0x2F` is a byte register
 * at an offset no 4-byte access reaches, so the driver's reset cannot be written with `st_write32` - a
 * 32-bit store there is the unaligned Device access the alignment clause refuses the build over. The
 * `dsb sy` is in this helper for the same reason it is in the one above: the poll that follows reads
 * the byte just written, and the barrier is what makes "the poll was taken after the store" a property
 * of this code rather than of the bus.
 */
static void st_write8(uint32_t addr, uint8_t value)
{
    *(volatile uint8_t *)(uintptr_t)addr = value;
    __asm__ volatile ("dsb sy" ::: "memory");
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 4 - the helper has one caller, and -Werror is on */

static uint32_t g_storage_probed;

#if STAGE90_XNU_STORAGE_PROBE >= 2
/*
 * **698: the rung-3 census's interlock, and it is the sequence's own outcome rather than a second
 * decision.** The census may only read a register file that is in SDHCI mode, and `_mode_stage == 8`
 * is what says the block reached that state. A flag rather than a re-read of `CORE_HC_MODE` because a
 * re-read could observe a word the *sequence* did not write, and the census's premise is the sequence's
 * result and not the register's history.
 */
static uint32_t g_storage_mode_complete;
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 2
/*
 * **696: rung 2 - the vendor's mode sequence, and it is this project's first write to a device block.**
 * `sdhci-msm.c:2841-2868` is the whole of it, and the four stores below are that text in order: write 0
 * to `CORE_HC_MODE`, set `CORE_SW_RST` in `CORE_POWER`, poll the bit clear, write `HC_MODE_EN`, then
 * set `FF_CLK_SW_RST_DIS`. It reads the register back after every store, so the log carries what the
 * block answered rather than what this code asked for, and the count it publishes
 * (`xnu_live_storage_writes`) is the count of stores it actually made.
 *
 * **The state this arm starts from says the sequence's ends matter.** The handed-over `_hc_mode` reads
 * `0x00002000` = `FF_CLK_SW_RST_DIS` with `HC_MODE_EN` clear, i.e. the vendor's own destination one bit
 * short. So store 1 (which writes 0) *clears* bit 13, and store 4 is what puts it back: an arm that
 * skipped the ends would leave the block worse than it found it.
 *
 * **The one guard this adds to the gate's.** A block that answered `CORE_MCI_VERSION` with 0 or with a
 * saturated word is a block that is not answering, and the sequence's second store is to `CORE_POWER`.
 * So the version word is the condition of the whole sequence: `_mode_refused = 1` with `_writes = 0` is
 * reached without a single store, and it is the only cell in which this arm changes nothing.
 *
 * **What it deliberately does not touch.** `POWER_CONTROL 0x29` (the SDHCI standard's own power
 * register, in `hc_mem`) - writing 0 to it is a bus-off request on this SoC, and the bus is already
 * powered. And `CORE_PWRCTL_CLEAR 0xE4`: the vendor acknowledges the power-IRQ status there because it
 * is about to register a handler, and this arm registers none, so the acknowledge would be a write with
 * no purpose. The status itself is *read* (`_mode_pwrctl_status`) and left latched - it is the reading
 * that says whether the reset latched anything, which the vendor's own comment predicts when the
 * previous power state was BUS_ON, as `_core_power = 0x441` says it was.
 */
static void st_mode_sequence(uint32_t core_power, uint32_t mci_version)
{
    uint32_t w0, w1, w2, pwr, pwr_wr, polls = 0u, steps = 0u, cleared = 0u, i;
    uint32_t t0, ticks;

    ST_LIVE("xnu_live_storage_mode_calls", 1u);

    if (mci_version == 0u || mci_version == 0xffffffffu) {
        ST_LIVE("xnu_live_storage_mode_refused", 1u);
        ST_LIVE("xnu_live_storage_mode_stage", 0u);
        ST_LIVE("xnu_live_storage_writes", 0u);
        return;
    }
    ST_LIVE("xnu_live_storage_mode_refused", 0u);

    /* Store 1: the vendor's `writel_relaxed(0, core_mem + CORE_HC_MODE)`. */
    ST_LIVE("xnu_live_storage_mode_stage", 1u);
    st_write32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE, 0u);
    ST_LIVE("xnu_live_storage_writes", 1u);
    w0 = st_read32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE);
    ST_LIVE("xnu_live_storage_mode_w0_read", w0);

    /* Store 2: `CORE_POWER |= CORE_SW_RST`. */
    ST_LIVE("xnu_live_storage_mode_stage", 2u);
    pwr_wr = core_power | ST_CORE_SW_RST;
    st_write32(ST_CORE_MEM_BASE + ST_CORE_POWER, pwr_wr);
    ST_LIVE("xnu_live_storage_writes", 2u);
    ST_LIVE("xnu_live_storage_mode_power_wr", pwr_wr);

    /*
     * The poll, and both of its bounds are published with the result. The clock is sampled once per
     * `ST_MODE_RST_INNER` device reads, which is what keeps the sampler from being the thing being
     * measured; `steps` reaching its ceiling and `ticks` reaching its budget are two different
     * statements and the log carries both.
     */
    ST_LIVE("xnu_live_storage_mode_stage", 3u);
    t0 = (uint32_t)stage90_cntvct_read();
    for (steps = 0u; steps < ST_MODE_RST_STEPS_MAX; steps++) {
        for (i = 0u; i < ST_MODE_RST_INNER; i++) {
            pwr = st_read32(ST_CORE_MEM_BASE + ST_CORE_POWER);
            polls++;
            if ((pwr & ST_CORE_SW_RST) == 0u) {
                cleared = 1u;
                break;
            }
        }
        if (cleared != 0u)
            break;
        if ((uint32_t)stage90_cntvct_read() - t0 >= ST_MODE_RST_TICK_BUDGET)
            break;
    }
    ticks = (uint32_t)stage90_cntvct_read() - t0;

    ST_LIVE("xnu_live_storage_mode_rst_polls", polls);
    ST_LIVE("xnu_live_storage_mode_rst_steps", steps);
    ST_LIVE("xnu_live_storage_mode_rst_ticks", ticks);
    ST_LIVE("xnu_live_storage_mode_rst_cleared", cleared);
    ST_LIVE("xnu_live_storage_mode_timeout", (cleared == 0u) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_mode_pwrctl_status",
            st_read32(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_STATUS));

    if (cleared == 0u) {
        /*
         * The failure path, and its shape is "no further store": the block is left exactly as stores 1
         * and 2 left it, and `_mode_w0_read` and `_mode_power_wr` above say what that is.
         */
        ST_LIVE("xnu_live_storage_mode_stage", 4u);
        ST_LIVE("xnu_live_storage_writes", 2u);
        return;
    }

    /* Store 3: the vendor's `writel_relaxed(HC_MODE_EN, core_mem + CORE_HC_MODE)`. */
    ST_LIVE("xnu_live_storage_mode_stage", 5u);
    st_write32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE, ST_HC_MODE_EN);
    ST_LIVE("xnu_live_storage_writes", 3u);
    w1 = st_read32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE);
    ST_LIVE("xnu_live_storage_mode_w1_read", w1);

    /* Store 4: the vendor's read-modify-write that ends on `FF_CLK_SW_RST_DIS`. */
    ST_LIVE("xnu_live_storage_mode_stage", 6u);
    st_write32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE, w1 | ST_FF_CLK_SW_RST_DIS);
    ST_LIVE("xnu_live_storage_writes", 4u);
    w2 = st_read32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE);
    ST_LIVE("xnu_live_storage_mode_w2_read", w2);
    ST_LIVE("xnu_live_storage_mode_bit_after", w2 & ST_HC_MODE_EN);

    /*
     * **And the standard words again, now through a block in SDHCI mode.** 694's pair
     * (`0x10`, `0x742dc8b2`) was taken through a block that was *not*, and 694 section 3 says why that
     * makes it a pre-mode pair rather than a spec-valid one. Read here, one store-group later, the two
     * pairs become the reading the arm exists for.
     *
     * **698 corrected the first of the two names and added the third read.** `hc_mem + 0x00` is
     * `SDHCI_DMA_ADDRESS` (`sdhci.h:27`) and not a version register, which 697 measured rather than
     * argued: the word went `0x10 -> 0x00` across the sequence - a *soft* register the core reset clears
     * - while `0x40`'s strapping constant did not move. So the key is renamed to what it reads and
     * `HOST_VERSION 0xFE` (`sdhci.h:241`, read by `sdhci-msm.c:2909`) is read beside it at its own
     * width: **that is the cell 697's pre-registration left owed**, and it is the first spec-valid
     * statement about this register file this project can make. `SDHCI_VENDOR_VER_MASK 0xFF00` /
     * `SDHCI_SPEC_VER_MASK 0x00FF` (`sdhci.h:242-245`) split the word.
     */
    ST_LIVE("xnu_live_storage_mode_stage", 7u);
    ST_LIVE("xnu_live_storage_mode_dma_address", st_read32(ST_HC_MEM_BASE + ST_SDHCI_DMA_ADDRESS));
    ST_LIVE("xnu_live_storage_mode_capabilities", st_read32(ST_HC_MEM_BASE + ST_SDHCI_CAPABILITIES));
    ST_LIVE("xnu_live_storage_mode_host_version", st_read16(ST_HC_MEM_BASE + ST_SDHCI_HOST_VERSION));
    ST_LIVE("xnu_live_storage_mode_stage", 8u);
    /* The whole sequence ran: this is what the rung-3 census is conditional on. */
    g_storage_mode_complete = 1u;
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 2 */

#if STAGE90_XNU_STORAGE_PROBE >= 3
/*
 * **698: rung 3 - the standard register file's census, and it is the before-value arm.**
 *
 * Every key this project has published about `hc_mem` until now is one of two *words* (`0x00` and
 * `0x40`). The registers a driver's bring-up actually reads and writes - `PRESENT_STATE`,
 * `HOST_CONTROL`, `POWER_CONTROL`, `CLOCK_CONTROL`, `SOFTWARE_RESET`, `SLOT_INT_STATUS` - have never
 * been read, and the next act on this path is `sdhci_reset(SDHCI_RESET_ALL)`, which touches four of
 * them (`sdhci.c:246` writes `SOFTWARE_RESET`, `:250` clears the driver's own `clock`, and the restore
 * path reads and re-writes `HOST_CONTROL`). **A before-value can only be taken before**, so this rung
 * exists between the mode sequence and the reset, and it adds no store: the store census in
 * `build_entry.sh` asserts exactly the four the vendor's sequence makes and passes unchanged here,
 * which is itself the proof that a read-only rung wrote nothing new.
 *
 * **The two registers this rung is most careful about.**
 *
 *   * `POWER_CONTROL 0x29` is **read and never written**, and 696 section 2's reason for not reading it
 *     ("a load from an address whose meaning this arm is not going to act on is a load that can only
 *     fault") no longer holds: the window is proven twice over and the value is now decision-relevant,
 *     because `sdhci.c:1342`/`:1353` are `sdhci_writeb(host, 0, SDHCI_POWER_CONTROL)` - the generic
 *     SDHCI core turns the bus off by writing 0 to this byte, which on this SoC **is** a bus-off
 *     request (531 section 8). Whether `SDHCI_RESET_ALL` clears it is the one hazard the next step
 *     carries, and this key is where the answer starts.
 *   * `CARD_PRESENT` (bit 16 of `PRESENT_STATE`, `sdhci.h:71`) **must not be read as "no card"**:
 *     `sdhci-msm.c:2896` sets `SDHCI_QUIRK_BROKEN_CARD_DETECTION`, the DT describes a soldered eMMC
 *     (`qcom,bus-width = <8>`), and detection on this board is a GPIO. The key is published with the
 *     bit named so that a reader cannot make that mistake quietly, and the census publishes the whole
 *     word so the other bits are readable without a second press.
 *
 * **The count is bottom-up.** `_reg_loads` is incremented at each read rather than written down, so the
 * record's "ten reads" and the log's number are two derivations of one fact (m688's rule: a table of
 * things-to-count is a scope claim unless the counter is the table's own).
 */
static void st_standard_census(void)
{
    uint32_t loads = 0u;
    uint8_t host_control, power_control, software_reset;
    uint16_t clock_control, slot_int_status;
    uint32_t present_state, capabilities_1, max_current, pwrctl_mask, pwrctl_ctl;

    /*
     * `SOFTWARE_RESET 0x2F` first among the bytes: it is self-clearing, so a non-zero read would mean a
     * reset is stuck in progress - and that would falsify 696's and 697's "no reset ran" rather than
     * anything this arm assumes. Read first so that the reading is in the log before the rest.
     */
    software_reset = st_read8(ST_HC_MEM_BASE + ST_SDHCI_SOFTWARE_RESET);
    ST_LIVE("xnu_live_storage_reg_software_reset", (uint32_t)software_reset);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    power_control = st_read8(ST_HC_MEM_BASE + ST_SDHCI_POWER_CONTROL);
    ST_LIVE("xnu_live_storage_reg_power_control", (uint32_t)power_control);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    host_control = st_read8(ST_HC_MEM_BASE + ST_SDHCI_HOST_CONTROL);
    ST_LIVE("xnu_live_storage_reg_host_control", (uint32_t)host_control);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    clock_control = st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL);
    ST_LIVE("xnu_live_storage_reg_clock_control", (uint32_t)clock_control);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    slot_int_status = st_read16(ST_HC_MEM_BASE + ST_SDHCI_SLOT_INT_STAT);
    ST_LIVE("xnu_live_storage_reg_slot_int_status", (uint32_t)slot_int_status);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    present_state = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
    ST_LIVE("xnu_live_storage_reg_present_state", present_state);
    ST_LIVE("xnu_live_storage_reg_card_present", present_state & ST_SDHCI_CARD_PRESENT);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    capabilities_1 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_CAPABILITIES_1);
    ST_LIVE("xnu_live_storage_reg_capabilities_1", capabilities_1);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    max_current = st_read32(ST_HC_MEM_BASE + ST_SDHCI_MAX_CURRENT);
    ST_LIVE("xnu_live_storage_reg_max_current", max_current);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    /*
     * **The other half of 696 section 3's hazard, and the before-value for the vendor's own
     * acknowledge.** 697 measured the power-IRQ *status* (0, nothing latched); the mask says whether a
     * power IRQ would have been *routed* at all, which a status register cannot answer. `CTL` is what
     * the vendor reads before it writes the success bits back (`:2879` reads, `:2884` writes, `:2069`
     * writes a byte at one site) - so it is the register the next step's acknowledge would change.
     */
    pwrctl_mask = st_read32(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_MASK);
    ST_LIVE("xnu_live_storage_reg_pwrctl_mask", pwrctl_mask);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    pwrctl_ctl = st_read32(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_CTL);
    ST_LIVE("xnu_live_storage_reg_pwrctl_ctl", pwrctl_ctl);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    ST_LIVE("xnu_live_storage_regs_done", loads);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 3 */

#if STAGE90_XNU_STORAGE_PROBE >= 4
/*
 * **701: rung 4 - the driver's own reset, and it is this project's first store through `hc_mem`.**
 *
 * Every store this project has made to a device so far is on `core_mem` and is one of the vendor's four
 * mode-sequence stores. The driver's own bring-up begins one block over, with
 * `sdhci_reset(host, SDHCI_RESET_ALL)` - and read out of the vendor's source (`sdhci.c:229-279`) that
 * function is **one byte write and a bounded poll**, because everything else in it is guarded off by
 * state a freshly initialized host does not have:
 *
 *   * `sdhci_writeb(host, mask, SDHCI_SOFTWARE_RESET)` (`:246`) - `0x01` to `hc_mem + 0x2F`. **This is
 *     the rung's whole store.**
 *   * `host->clock = 0` (`:249`) - the driver's own variable, no register.
 *   * `check_power_status(host, REQ_BUS_OFF)` (`:254-256`) - skipped because `host->pwr` is 0, and it is
 *     the guard that matters most: `sdhci_msm_check_power_status` (`sdhci-msm.c:2179-2209`) writes no
 *     register at all and ends in `wait_for_completion(&msm_host->pwr_irq_completion)` - **an unbounded
 *     wait on a power IRQ**. A payload that "finished the driver's init" by emulating it would be waiting
 *     for an interrupt nothing in this image raises. Rung 4 therefore stops short of it *deliberately*.
 *   * `platform_reset_enter`/`_exit` (`:243`, `:271`) - `sdhci_msm_ops` (`sdhci-msm.c:2653-2666`) has no
 *     such member, so the MSM does not hook this reset beyond the power-status check it cannot reach.
 *   * the poll (`:259-268`) - `sdhci_readb(SDHCI_SOFTWARE_RESET) & mask`, up to 100 x `mdelay(1)`, with
 *     the driver's own `Reset 0x%x never completed` path (`:260-264`) if the bound expires. **This rung
 *     polls the same byte with the same 100 ms bound**, published as ticks on this machine's own counter.
 *
 * **And the clocks are not on this path either** (701 section 2): `sdhci_msm_ops` replaces `.set_clock`,
 * and `sdhci_msm_set_clock` (`:2402-2535`) writes `CORE_VENDOR_SPEC 0x10C` - a fifth `core_mem` offset -
 * and calls `clk_set_rate` (`:2520`) on the GCC's SDCC clocks, i.e. the `0xfc400000` megabyte 692 pressed
 * and measured as **not mapped**. So this rung sets no clock, and `CLOCK_CONTROL` is read before and
 * after only as a before/after pair of a register this rung does not write.
 *
 * **The before-values are 699's, which is what makes the reset's effect a measurement**: `_reg_software_reset`
 * and `_reg_power_control` both read 0, `_reg_present_state`'s inhibit bits are clear, and
 * `_reg_pwrctl_mask` reads `0x0000000f` - four bits of power IRQ routed, which is what makes
 * `_reg_pwrctl_status_after` below a reading and not a formality (696 section 3 warned the reset may latch
 * a power-IRQ status when the previous state was `BUS_ON`, and 699 measured `_core_power=0x00000441`).
 *
 * **The guard is the register's own contract.** `SOFTWARE_RESET` is self-clearing, so a byte that reads
 * non-zero at entry is a reset already in progress - the one state in which writing this byte would be
 * acting on a block that is mid-reset. 699 measured 0, so that branch is the refusal path and not the
 * expected one, and it is published rather than silent.
 */
static void st_driver_reset(void)
{
    uint32_t t0, ticks, polls = 0u, steps = 0u, stores = 0u, i, cleared = 0u, done;
    uint8_t reset_before;

    ST_LIVE("xnu_live_storage_rst_calls", 1u);

    reset_before = st_read8(ST_HC_MEM_BASE + ST_SDHCI_SOFTWARE_RESET);
    ST_LIVE("xnu_live_storage_rst_before", reset_before);
    if ((reset_before & ST_SDHCI_RESET_ALL) != 0u) {
        ST_LIVE("xnu_live_storage_rst_refused", 1u);
        ST_LIVE("xnu_live_storage_rst_wrote", 0u);
        ST_LIVE("xnu_live_storage_rst_stores", 0u);
        ST_LIVE("xnu_live_storage_rst_stage", 0u);
        return;
    }
    ST_LIVE("xnu_live_storage_rst_refused", 0u);

    /* The store: the driver's own `sdhci_writeb(host, SDHCI_RESET_ALL, SDHCI_SOFTWARE_RESET)`. */
    ST_LIVE("xnu_live_storage_rst_stage", 1u);
    st_write8(ST_HC_MEM_BASE + ST_SDHCI_SOFTWARE_RESET, (uint8_t)ST_SDHCI_RESET_ALL);
    stores++;
    ST_LIVE("xnu_live_storage_rst_wrote", 1u);
    ST_LIVE("xnu_live_storage_rst_stores", stores);

    /*
     * The poll, and both of its bounds are published with the result - the same shape as the mode
     * sequence's poll, and for the same reason: the clock is sampled once per `ST_RST_INNER` byte reads,
     * which keeps the sampler from being the thing being measured, and `steps` reaching its ceiling,
     * `ticks` reaching its budget and the bit clearing are three different statements.
     */
    ST_LIVE("xnu_live_storage_rst_stage", 2u);
    t0 = (uint32_t)stage90_cntvct_read();
    for (steps = 0u; steps < ST_RST_STEPS_MAX; steps++) {
        for (i = 0u; i < ST_RST_INNER; i++) {
            polls++;
            if ((st_read8(ST_HC_MEM_BASE + ST_SDHCI_SOFTWARE_RESET) & ST_SDHCI_RESET_ALL) == 0u) {
                cleared = 1u;
                break;
            }
        }
        if (cleared != 0u)
            break;
        if ((uint32_t)stage90_cntvct_read() - t0 >= ST_RST_TICK_BUDGET)
            break;
    }
    ticks = (uint32_t)stage90_cntvct_read() - t0;

    ST_LIVE("xnu_live_storage_rst_polls", polls);
    ST_LIVE("xnu_live_storage_rst_steps", steps);
    ST_LIVE("xnu_live_storage_rst_ticks", ticks);
    ST_LIVE("xnu_live_storage_rst_cleared", cleared);
    ST_LIVE("xnu_live_storage_rst_timeout", (cleared == 0u) ? 1u : 0u);

    /*
     * **The after-values, and the first of them is the cell 698 section 2 owed.** `POWER_CONTROL 0x29`
     * is read here and still never written: a value with bit 0 set after the reset would mean the
     * standard reset *changed the block's belief about the bus*, and the driver would then have to
     * reconcile that with `CORE_PWRCTL`; a 0 says the reset left it exactly as 699 found it. The other
     * three are the registers the *generic* core's reset path can touch on other platforms
     * (`HOST_CONTROL` is restored under a quirk this SoC does not set; the inhibit bits say whether a
     * transfer was aborted), so reading them makes "this reset moved one register" a measurement.
     */
    ST_LIVE("xnu_live_storage_reg_software_reset_after", st_read8(ST_HC_MEM_BASE + ST_SDHCI_SOFTWARE_RESET));
    ST_LIVE("xnu_live_storage_reg_power_control_after", st_read8(ST_HC_MEM_BASE + ST_SDHCI_POWER_CONTROL));
    ST_LIVE("xnu_live_storage_reg_pwrctl_status_after", st_read32(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_STATUS));
    ST_LIVE("xnu_live_storage_reg_host_control_after", st_read8(ST_HC_MEM_BASE + ST_SDHCI_HOST_CONTROL));
    ST_LIVE("xnu_live_storage_reg_present_state_after", st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));

    done = 1u;
    ST_LIVE("xnu_live_storage_rst_done", done);
    ST_LIVE("xnu_live_storage_rst_stage", 3u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 4 */

#if STAGE90_XNU_STORAGE_PROBE >= 5
/*
 * **704: rung 5 - the clock surface, read and never written.** The rung-5 pre-registration
 * (`docs/experiments/experiment-704-...`) is the design and §2 of it is the reason this function
 * decomposes the gate into three bits instead of reusing the one the probe already has.
 *
 * **The defect this rung exists to correct, in one line.** `entry_storage_probe` has guarded this whole
 * line since 692 with
 *
 *     gate = cbcr & ST_SDCC1_CLK_ENABLE;              // ST_SDCC1_CLK_ENABLE is BIT(0)
 *
 * and `BIT(0)` of a CBCR is `CBCR_BRANCH_ENABLE_BIT` - **the bit the driver writes**, i.e. the *request*
 * (`clock-local2.c:62`, written at `:380-382`). What "the clock is running" means is a second bit two
 * lines away, `CBCR_BRANCH_OFF_BIT` = `BIT(31)` (`:63`), which the framework then *polls* to a bound
 * before it believes the clock is on (`:386-387` -> `branch_clk_halt_check`, `:333-360`) and which its
 * own handoff test reads **alone**, without looking at `BIT(0)` at all (`branch_clk_handoff`, `:490-497`:
 * `BIT(31)` set is `HANDOFF_DISABLED_CLK`). **The two can disagree**, and the ordinary state in which
 * they do is a branch whose enable is requested while the root above it is off - the clock has not
 * propagated, and a read through it is the bus wait nothing ends. 694's `_cbcr = 0x00004ff1` satisfies
 * both halves, so nothing measured so far is overturned; what is corrected is that the guard was reading
 * **half of itself**, and this rung publishes both halves for all four branches.
 *
 * **Why this rung writes nothing, stated as a reading and not as a comment.** The rung's whole claim is
 * that these registers can be read at all and what they say; the writer is `sdhci_msm_set_clock`, whose
 * first write would be a read-modify-write of a field on a branch whose halt state this run has never
 * read. `_clk_writes` is published as 0 on every path, and the store census in `build_entry.sh` refuses
 * a build in which this window holds any store at any rung - so the sentence "this rung does not write
 * the clock" is a property of the linked image rather than of this paragraph.
 *
 * **What it does not do**: no rate. The MND and source-select *fields* are published, and converting
 * them to Hz needs the parent's rate (`gpll0`/`gpll4`/`cxo`, `clock-8974.c:1564-1574`), which is a table
 * this image does not carry. Publishing a computed frequency beside the framework's own cached
 * `msm_host->clk_rate` would be the same one-value-two-definitions defect one level up.
 */
static void st_clock_census(void)
{
    uint32_t loads = 0u;
    uint32_t bcr, apps, ahb, cd_sleep, cd_ff;
    uint32_t rcg_cmd, rcg_cfg, rcg_m, rcg_n, rcg_d;
    uint32_t vendor;

    ST_LIVE("xnu_live_storage_clk_calls", 1u);
    ST_LIVE("xnu_live_storage_clk_writes", 0u);

    /*
     * **The block reset word, and the bit 694 published as a number.** `_bcr` has been in the log since
     * 694 without a key saying what a bit of it means; `BCR_BLK_ARES_BIT` (`clock-local2.c:66`) is the
     * only one that matters here, and `ares = 0` is what makes the branch readings below mean "the clock
     * is off" rather than "the block is held in reset and nothing it says is about anything".
     */
    bcr = st_read32(ST_GCC_BASE + ST_SDCC1_BCR);
    loads++;
    ST_LIVE("xnu_live_storage_clk_bcr", bcr);
    ST_LIVE("xnu_live_storage_clk_bcr_ares", (bcr & ST_BCR_ARES_BIT) ? 1u : 0u);

    /*
     * **The four branches, each as a word and as the three bits the framework itself distinguishes.**
     * `en` is the request, `off` is the halt state, and `hw` is `CBCR_HW_CTL_BIT` (`:67`) - set means the
     * branch is under hardware gating, and the framework skips its own halt check there (`:346-347`), so
     * an `off` bit is not consulted in that mode and a reader has to see it to know that.
     */
    apps = st_read32(ST_GCC_BASE + ST_SDCC1_CBCR);
    loads++;
    ST_LIVE("xnu_live_storage_clk_apps_cbcr", apps);
    ST_LIVE("xnu_live_storage_clk_apps_en", (apps & ST_CBCR_ENABLE_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_apps_off", (apps & ST_CBCR_OFF_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_apps_hw", (apps & ST_CBCR_HW_CTL_BIT) ? 1u : 0u);

    ahb = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_AHB_CBCR);
    loads++;
    ST_LIVE("xnu_live_storage_clk_ahb_cbcr", ahb);
    ST_LIVE("xnu_live_storage_clk_ahb_en", (ahb & ST_CBCR_ENABLE_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_ahb_off", (ahb & ST_CBCR_OFF_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_ahb_hw", (ahb & ST_CBCR_HW_CTL_BIT) ? 1u : 0u);

    cd_sleep = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_CDCCAL_SLEEP_CBCR);
    loads++;
    ST_LIVE("xnu_live_storage_clk_cdccal_sleep_cbcr", cd_sleep);
    ST_LIVE("xnu_live_storage_clk_cdccal_sleep_en", (cd_sleep & ST_CBCR_ENABLE_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_cdccal_sleep_off", (cd_sleep & ST_CBCR_OFF_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_cdccal_sleep_hw", (cd_sleep & ST_CBCR_HW_CTL_BIT) ? 1u : 0u);

    cd_ff = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_CDCCAL_FF_CBCR);
    loads++;
    ST_LIVE("xnu_live_storage_clk_cdccal_ff_cbcr", cd_ff);
    ST_LIVE("xnu_live_storage_clk_cdccal_ff_en", (cd_ff & ST_CBCR_ENABLE_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_cdccal_ff_off", (cd_ff & ST_CBCR_OFF_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_cdccal_ff_hw", (cd_ff & ST_CBCR_HW_CTL_BIT) ? 1u : 0u);

    /*
     * **The root clock generator the apps branch hangs off, and `_rcg_update` is the premise of the
     * poll the next rung will run.** `rcg_update_config` (`clock-local2.c:90-108`) sets
     * `CMD_RCGR_CONFIG_UPDATE_BIT` and then polls *that same bit* clear to a bound of
     * `UPDATE_CHECK_MAX_LOOPS 500` (`:44`) - so a `_rcg_update = 0` before anything writes is what makes
     * "a bit that does not clear afterwards is a failed update" a reading rather than an assumption.
     * Read in the same order the poll would find them: command, then configuration, then the three MND
     * words, which are `set_rate_mnd`'s own inputs (`:126-146`) written **before** the config word.
     */
    rcg_cmd = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_APPS_RCG + ST_RCG_CMD);
    loads++;
    ST_LIVE("xnu_live_storage_clk_rcg_cmd", rcg_cmd);
    ST_LIVE("xnu_live_storage_clk_rcg_root_en", (rcg_cmd & ST_RCG_ROOT_EN_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_rcg_update", (rcg_cmd & ST_RCG_UPDATE_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_rcg_root_status", (rcg_cmd & ST_RCG_ROOT_STATUS_BIT) ? 1u : 0u);

    rcg_cfg = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_APPS_RCG + ST_RCG_CFG);
    loads++;
    ST_LIVE("xnu_live_storage_clk_rcg_cfg", rcg_cfg);
    ST_LIVE("xnu_live_storage_clk_rcg_src", (rcg_cfg & ST_RCG_CFG_SRC_MASK) >> ST_RCG_CFG_SRC_SHIFT);
    ST_LIVE("xnu_live_storage_clk_rcg_div", rcg_cfg & ST_RCG_CFG_DIV_MASK);
    ST_LIVE("xnu_live_storage_clk_rcg_mnd_mode", (rcg_cfg & ST_RCG_CFG_MND_MASK) >> 12);

    rcg_m = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_APPS_RCG + ST_RCG_M);
    loads++;
    ST_LIVE("xnu_live_storage_clk_rcg_m", rcg_m);
    rcg_n = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_APPS_RCG + ST_RCG_N);
    loads++;
    ST_LIVE("xnu_live_storage_clk_rcg_n", rcg_n);
    rcg_d = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_APPS_RCG + ST_RCG_D);
    loads++;
    ST_LIVE("xnu_live_storage_clk_rcg_d", rcg_d);

    /*
     * **The one register on this path that is not a clock branch**, and the fifth offset in a window the
     * census has bounded to four since 698. `sdhci_msm_set_clock` reads it at five sites and writes a
     * field of it at five more; rung 5 reads both fields and writes neither, which is what makes them the
     * write rung's before-values.
     */
    vendor = st_read32(ST_CORE_MEM_BASE + ST_CORE_VENDOR_SPEC);
    loads++;
    ST_LIVE("xnu_live_storage_clk_vendor_spec", vendor);
    ST_LIVE("xnu_live_storage_clk_vendor_pwrsave", (vendor & ST_VENDOR_PWRSAVE_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_vendor_mclk_sel",
            (vendor & ST_VENDOR_MCLK_MASK) >> ST_VENDOR_MCLK_SHIFT);

    /*
     * **The after-value, and it is the same register 703 read after the reset.** `CLOCK_CONTROL 0x2C`
     * read last means "the census moved nothing" is a measurement: bit 2 (`SD clock enable`) still clear
     * says this rung did not set a clock, and bits 0/1 still set say it did not clear one either.
     */
    ST_LIVE("xnu_live_storage_clk_clock_control_after",
            (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL));
    loads++;

    ST_LIVE("xnu_live_storage_clk_loads", loads);
    ST_LIVE("xnu_live_storage_clk_done", 1u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 5 */

#if STAGE90_XNU_STORAGE_PROBE >= 6
/*
 * **706: rung 6 - the driver's first clock set, and the rate write that is not on this path.**
 * `docs/experiments/experiment-706-...md` is the pre-registration and the whole of this block is its
 * section 1 read out of the vendor's own source, in the vendor's own order:
 *
 *   `sdhci_do_set_ios` (`sdhci.c:1635-1636`) -> `sdhci_set_clock(host, 400000)` -> the vendor hook
 *   (`sdhci.c:1211-1214`) -> `sdhci_msm_set_clock` (`sdhci-msm.c:2402-2535`) -> then the standard's own
 *   divisor arithmetic and its two `CLOCK_CONTROL` halfwords (`sdhci.c:1254-1267`, `:1285-1306`).
 *
 * **`clock = 400000` is a reading, not a choice.** `mmc_rescan_try_freq(host, host->f_min)` sets
 * `host->f_init = freq` (`drivers/mmc/core/core.c:3077-3079`, called at `:3251`) and `host->f_min` is
 * `sup_clk_table[0]` (`sdhci-msm.c:2233`) = the first entry of the DT's `qcom,clk-rates`
 * (`msm8974pro.dtsi:1768`), i.e. 400000.
 *
 * **And the rate write is NOT on this path, which is section 2's finding and the reason this rung is
 * small.** `sdhci_msm_get_sup_clk_rate(host, 400000)` returns 400000 and `msm_host->clk_rate` was
 * initialised to `get_min_clock(host)` = the same 400000 (`:2795`), so `sup_clock != msm_host->clk_rate`
 * is false and `clk_set_rate` - the only thing here that writes the RCG - is skipped. The driver believes
 * the clock is 400 kHz while 705 measured the register file at `gpll4`/div 4 (192 MHz): two readings of
 * one quantity that disagree by 480x, and nothing reconciles them until the first *speed change*. So the
 * RCG's five words are read here (rung 5 did that) and **written nowhere**, `_clk_set_rate_writes = 0`.
 *
 * **Why the writes below cannot gate a clock, and what the build refuses instead.** The four CBCR stores
 * are read-modify-writes of `BIT(0)` - a bit 705 measured *set* on all four branches - so they cannot
 * disable one; the two `CORE_VENDOR_SPEC` stores touch only `MCLK_SEL` (`2 << 8`) and the `HC_SELECT_IN`
 * pair, neither of which gates anything. **`BCR 0x04C0` (block reset) and the whole RCG (`0x04D0`-`0x04E0`)
 * are refused by `build_entry.sh` rather than avoided here** - the GCC window's store set is asserted to
 * be exactly the four branch offsets in order - and `POWER_CONTROL 0x29` stays unreachable in the
 * `hc_mem` window, because the driver's power path writes **0** there (a bus-off request) and then waits
 * unbounded (`sdhci.c:1352-1355`, `sdhci-msm.c:2179-2209`): that act is the next rung's subject.
 *
 * **And the AHB branch's `has_sibling` does not make its enable a different act, which the rung-5 census's
 * own comment asked this rung to say.** `gcc_sdcc1_ahb_clk` carries `has_sibling = 1`, and
 * `clock-local2.c:444-446`/`:460-462` turns that into `-EPERM` for `round_rate` and `list_rate` - both of
 * which are *queries about a rate*, reached from `clk_set_rate` and `clk_round_rate`. The path this rung
 * takes is `clk_prepare_enable`, and that is `:373-390`: a CBCR read-modify-write of `BIT(0)` followed by
 * the halt check, with no branch on `has_sibling` anywhere. 705 measured that bit already set
 * (`_clk_ahb_cbcr = 0x2000cff1`), so this store writes back the word it read - the same shape as the other
 * three. What `has_sibling` *does* mean here is the one thing the halt check has to handle and this image
 * already handles: the AHB branch is the one whose ON value is `BRANCH_NOC_FSM_ON_VAL` and not
 * `BRANCH_ON_VAL` (`clock-local2.c:325-328`, `:357-358`), which is why `st_branch_enable` accepts both.
 */
#define ST_BRANCH_CHECK_MASK      0xF0000000u    /* BM(31, 28), clock-local2.c:325 */
#define ST_BRANCH_ON_VAL          0x00000000u    /* BRANCH_ON_VAL,      :326 */
#define ST_BRANCH_NOC_FSM_ON_VAL  0x20000000u    /* BRANCH_NOC_FSM_ON_VAL, :328 - the AHB branch's own
                                                  * value, and 705 measured it: `_clk_ahb_cbcr = 0x2000cff1`
                                                  * reads 0x2 in bits 31:28, which `:357-358` accepts as ON */
#define ST_HALT_CHECK_MAX_LOOPS   500u           /* :36 - with a 1 us delay between failed reads */
#define ST_HALT_TICK_BUDGET       9600u          /* 500 us at this machine's 19,200,000 Hz */
#define ST_VENDOR_MCLK_DFLT       0x00000200u    /* CORE_HC_MCLK_SEL_DFLT = (2 << 8), sdhci-msm.c:94 */
#define ST_VENDOR_SELECT_IN_EN    (1u << 18)     /* CORE_HC_SELECT_IN_EN,   :98 */
#define ST_VENDOR_SELECT_IN_MASK  (7u << 19)     /* CORE_HC_SELECT_IN_MASK, :100 */
#define ST_SDHCI_CLOCK_INT_EN     0x0001u        /* sdhci.h:109 */
#define ST_SDHCI_CLOCK_INT_STABLE 0x0002u        /* sdhci.h:108 - read-only */
#define ST_SDHCI_CLOCK_CARD_EN    0x0004u        /* sdhci.h:107 - THE SD CLOCK ENABLE BIT */
#define ST_SET_INIT_CLOCK         400000u        /* host->f_init = host->f_min = sup_clk_table[0] */
#define ST_SET_MAX_CLK            384000000u     /* msm8974pro.dtsi:1768's last entry, via
                                                  * sdhci-msm.c:2240 get_max_clock - the divisor
                                                  * arithmetic's one non-register input */
#define ST_SET_MAX_CLK_STD        200000000u     /* msm8974.dtsi:339 - the ALTERNATIVE table, and the cell
                                                  * that tells the two apart is `_clk_set_divisor_alt` */
#define ST_SET_DIV_MAX            2046u          /* SDHCI_MAX_DIV_SPEC_300, sdhci.h:255 */
#define ST_CC_TICK_BUDGET         384000u        /* the driver's `timeout = 20` ms (sdhci.c:1292) */
#define ST_CC_POLL_STEPS          4096u          /* the backstop: 4096 halfword reads cannot outlast
                                                  * 20 ms, so the clock bound is the one that fires */

/*
 * **The halfword store, and rung 6 is the reason it exists.** `CLOCK_CONTROL 0x2C` is a 16-bit register
 * (`sdhci.h:100`) written with `sdhci_writew` (`sdhci.c:1289`, `:1306`), so a 32-bit store there would be
 * a *wider* access than the vendor's own contract - and `0x2C` is 4-aligned, so the alignment census
 * would let it through: the width here is a property of the register, not of the address, which is why
 * `build_entry.sh`'s `hc_mem` clause checks the mnemonic and not only the offset.
 */
static void st_write16(uint32_t addr, uint16_t value)
{
    *(volatile uint16_t *)(uintptr_t)addr = value;
    __asm__ volatile ("dsb sy" ::: "memory");
}

/*
 * **One branch enable, as `branch_clk_enable` writes it** (`clock-local2.c:373-390`): read the CBCR, set
 * `BIT(0)`, write it back, then run the halt check (`:333-371`) - which for every branch on this path is
 * the `HALT` arm, because `gcc_sdcc1_ahb_clk` and `gcc_sdcc1_cdccal_sleep_clk` declare `has_sibling = 1`
 * and none of the four declares a `halt_check` (`clock-8974.c:2339-2381`), so the field's value is 0 =
 * `HALT` (`clock-local2.h`). The poll's terminal state is published rather than inferred from the count,
 * and the count is published rather than inferred from the state: a first-read pass (expected) and a
 * 500th-read pass (a bound that was spent) are different readings of the same bit.
 */
struct st_branch_poll {
    uint32_t before;
    uint32_t after;
    uint32_t polls;
    uint32_t ticks;
    uint32_t halted;
};

/*
 * **`always_inline`, and the reason is a clause's subject rather than speed.** `build_entry.sh`'s
 * store census reads `entry_storage_probe`'s OWN body and classifies every store in it, which is what
 * makes "the offsets are the property" true of the artifact the gate boots - the four branch enables
 * must therefore be *this body's* stores. GCC does not inline a helper with four call sites (it does
 * inline its single-call siblings: `st_clock_census`, `st_clock_set`) and the first build of this rung
 * measured exactly that: `st_branch_enable` came out as a symbol at `0x8000cff8` with the probe pushed
 * to `0x8000d09c`, the census reported `core_mem [120 0 120 120 ]` (the two `0x10C` stores were in the
 * inlined `st_clock_set` and did land), the GCC window came out EMPTY, and the arm's own record was
 * refused by two clauses at once. A helper the arm's safety claim has to name is not a helper this
 * image wants outlined, so the attribute makes the compiler's choice the arm's choice and the stores
 * land where the record says they are.
 */
static inline __attribute__((always_inline)) void
st_branch_enable(uint32_t addr, struct st_branch_poll *r)
{
    uint32_t value, t0, polls = 0u, halted = 0u;

    r->before = st_read32(addr);
    value = r->before | ST_CBCR_ENABLE_BIT;      /* clock-local2.c:381 */
    st_write32(addr, value);                     /* clock-local2.c:382 - the read-modify-write */
    r->after = st_read32(addr);

    t0 = (uint32_t)stage90_cntvct_read();
    for (polls = 1u; polls <= ST_HALT_CHECK_MAX_LOOPS; polls++) {
        uint32_t v = st_read32(addr) & ST_BRANCH_CHECK_MASK;

        if (v == ST_BRANCH_ON_VAL || v == ST_BRANCH_NOC_FSM_ON_VAL) {   /* :357-358 */
            halted = 1u;
            break;
        }
        if ((uint32_t)stage90_cntvct_read() - t0 >= ST_HALT_TICK_BUDGET)
            break;
    }
    r->ticks = (uint32_t)stage90_cntvct_read() - t0;
    r->polls = polls;
    r->halted = halted;
}

static void st_clock_set(void)
{
    struct st_branch_poll b;
    uint32_t writes = 0u, gcc_writes = 0u, rate_writes = 0u;
    uint32_t vendor, v1, v2, vendor_after;
    uint32_t cc_before, div, real_div = 0u, word_int, word_card, cc_after;
    uint32_t stable = 0u, cc_polls = 0u, cc_steps = 0u, t0;
    uint32_t alt = 0u;

    ST_LIVE("xnu_live_storage_clk_set_calls", 1u);

    /*
     * **1. `sdhci_msm_prepare_clocks(host, true)` (`sdhci-msm.c:2315-2374`), and the order is the
     * vendor's**: `pclk` (= `gcc_sdcc1_ahb_clk`, `clock-8974.c:2339`), `clk` (= `gcc_sdcc1_apps_clk`,
     * `:2350`), then `bus_clk`, `ff_clk` (= `gcc_sdcc1_cdccal_ff_clk`, `:2361`) and `sleep_clk`
     * (= `gcc_sdcc1_cdccal_sleep_clk`, `:2372`). `bus_clk` is `devm_clk_get(&pdev->dev, "bus_clk")`
     * (`:2758`) and this board's SDCC1 node has no such clock, so `IS_ERR_OR_NULL` skips it - which is
     * why four branches are enabled and not five.
     */
    st_branch_enable(ST_GCC_BASE + ST_GCC_SDCC1_AHB_CBCR, &b);
    writes++;
    gcc_writes++;
    ST_LIVE("xnu_live_storage_clk_set_ahb_before", b.before);
    ST_LIVE("xnu_live_storage_clk_set_ahb_after", b.after);
    ST_LIVE("xnu_live_storage_clk_set_ahb_polls", b.polls);
    ST_LIVE("xnu_live_storage_clk_set_ahb_ticks", b.ticks);
    ST_LIVE("xnu_live_storage_clk_set_ahb_halted", b.halted);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);
    ST_LIVE("xnu_live_storage_clk_set_gcc_writes", gcc_writes);

    st_branch_enable(ST_GCC_BASE + ST_GCC_SDCC1_APPS_CBCR, &b);
    writes++;
    gcc_writes++;
    ST_LIVE("xnu_live_storage_clk_set_apps_before", b.before);
    ST_LIVE("xnu_live_storage_clk_set_apps_after", b.after);
    ST_LIVE("xnu_live_storage_clk_set_apps_polls", b.polls);
    ST_LIVE("xnu_live_storage_clk_set_apps_ticks", b.ticks);
    ST_LIVE("xnu_live_storage_clk_set_apps_halted", b.halted);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);
    ST_LIVE("xnu_live_storage_clk_set_gcc_writes", gcc_writes);

    st_branch_enable(ST_GCC_BASE + ST_GCC_SDCC1_CDCCAL_FF_CBCR, &b);
    writes++;
    gcc_writes++;
    ST_LIVE("xnu_live_storage_clk_set_ff_before", b.before);
    ST_LIVE("xnu_live_storage_clk_set_ff_after", b.after);
    ST_LIVE("xnu_live_storage_clk_set_ff_polls", b.polls);
    ST_LIVE("xnu_live_storage_clk_set_ff_ticks", b.ticks);
    ST_LIVE("xnu_live_storage_clk_set_ff_halted", b.halted);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);
    ST_LIVE("xnu_live_storage_clk_set_gcc_writes", gcc_writes);

    st_branch_enable(ST_GCC_BASE + ST_GCC_SDCC1_CDCCAL_SLEEP_CBCR, &b);
    writes++;
    gcc_writes++;
    ST_LIVE("xnu_live_storage_clk_set_sleep_before", b.before);
    ST_LIVE("xnu_live_storage_clk_set_sleep_after", b.after);
    ST_LIVE("xnu_live_storage_clk_set_sleep_polls", b.polls);
    ST_LIVE("xnu_live_storage_clk_set_sleep_ticks", b.ticks);
    ST_LIVE("xnu_live_storage_clk_set_sleep_halted", b.halted);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);
    ST_LIVE("xnu_live_storage_clk_set_gcc_writes", gcc_writes);

    /*
     * **2. The vendor spec's two read-modify-writes (`sdhci-msm.c:2495-2513`), the non-HS400 arm**, and
     * the `curr_pwrsave` pair above them (`:2427-2441`) is **not** taken at this clock: both of its arms
     * need `clock > 400000` or `curr_pwrsave`, and rung 5 measured `_clk_vendor_pwrsave = 0`. So the two
     * stores here are the MCLK select and the HC_SELECT_IN clears, and nothing else on 0x10C.
     */
    vendor = st_read32(ST_CORE_MEM_BASE + ST_CORE_VENDOR_SPEC);
    v1 = (vendor & ~ST_VENDOR_MCLK_MASK) | ST_VENDOR_MCLK_DFLT;            /* :2497-2499 */
    st_write32(ST_CORE_MEM_BASE + ST_CORE_VENDOR_SPEC, v1);
    writes++;
    ST_LIVE("xnu_live_storage_clk_set_vendor_before", vendor);
    ST_LIVE("xnu_live_storage_clk_set_vendor_w1", v1);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);

    v2 = st_read32(ST_CORE_MEM_BASE + ST_CORE_VENDOR_SPEC)
         & ~ST_VENDOR_SELECT_IN_EN & ~ST_VENDOR_SELECT_IN_MASK;           /* :2510-2512 */
    st_write32(ST_CORE_MEM_BASE + ST_CORE_VENDOR_SPEC, v2);
    writes++;
    vendor_after = st_read32(ST_CORE_MEM_BASE + ST_CORE_VENDOR_SPEC);
    ST_LIVE("xnu_live_storage_clk_set_vendor_w2", v2);
    ST_LIVE("xnu_live_storage_clk_set_vendor_after", vendor_after);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);

    /*
     * **3. The rate write, and it is not reached.** `sup_clock = sdhci_msm_get_sup_clk_rate(host, 400000)
     * = 400000` (the table's first entry equals the request) and `msm_host->clk_rate = 400000` from
     * `:2795`, so `:2517`'s condition is false and the RCG - whose before-values rung 5 published - is
     * not written. This key is the rung's central claim, published rather than argued.
     */
    ST_LIVE("xnu_live_storage_clk_set_rate_writes", rate_writes);

    /*
     * **4. The standard's own two halfwords and the poll between them** (`sdhci.c:1254-1306`). The
     * divisor is the count's, computed with the driver's own loop and the one input that is not a
     * register: `max_clk = get_max_clock(host) = sup_clk_table[sup_clk_cnt-1]` = the DT's last entry.
     * `_clk_set_divisor_alt` is what tells the two candidate tables apart in the log.
     */
    ST_LIVE("xnu_live_storage_clk_set_max_clk", ST_SET_MAX_CLK);

    if (ST_SET_MAX_CLK <= ST_SET_INIT_CLOCK) {
        div = 1u;                                                        /* :1257-1258 */
    } else {
        for (div = 2u; div < ST_SET_DIV_MAX; div += 2u)                  /* :1260-1261 */
            if ((ST_SET_MAX_CLK / div) <= ST_SET_INIT_CLOCK)
                break;
    }
    real_div = div;
    div >>= 1u;                                                          /* :1266 */
    if (real_div == (ST_SET_MAX_CLK_STD / ST_SET_INIT_CLOCK))
        alt = 1u;

    ST_LIVE("xnu_live_storage_clk_set_real_div", real_div);
    ST_LIVE("xnu_live_storage_clk_set_div", div);
    ST_LIVE("xnu_live_storage_clk_set_divisor_alt", alt);

    cc_before = (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL);
    word_int = (uint32_t)(((div & 0xFFu) << 8)
                          | (((div & 0x300u) >> 8) << 6)             /* :1285-1286 */
                          | ST_SDHCI_CLOCK_INT_EN);                  /* :1288 */
    st_write16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL, (uint16_t)word_int);
    writes++;
    ST_LIVE("xnu_live_storage_clk_set_cc_before", cc_before);
    ST_LIVE("xnu_live_storage_clk_set_cc_int", word_int);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);

    t0 = (uint32_t)stage90_cntvct_read();
    for (cc_steps = 1u; cc_steps <= ST_CC_POLL_STEPS; cc_steps++) {
        cc_polls = (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL);
        if ((cc_polls & ST_SDHCI_CLOCK_INT_STABLE) != 0u) {           /* sdhci.c:1293-1294 */
            stable = 1u;
            break;
        }
        if ((uint32_t)stage90_cntvct_read() - t0 >= ST_CC_TICK_BUDGET)
            break;
    }
    ST_LIVE("xnu_live_storage_clk_set_cc_stable", stable);
    ST_LIVE("xnu_live_storage_clk_set_cc_polls", cc_steps);
    ST_LIVE("xnu_live_storage_clk_set_cc_ticks", (uint32_t)stage90_cntvct_read() - t0);

    word_card = word_int | ST_SDHCI_CLOCK_CARD_EN;                    /* :1305 */
    st_write16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL, (uint16_t)word_card);
    writes++;
    cc_after = (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL);
    ST_LIVE("xnu_live_storage_clk_set_cc_card", word_card);
    ST_LIVE("xnu_live_storage_clk_set_cc_after", cc_after);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);
    ST_LIVE("xnu_live_storage_clk_set_gcc_writes", gcc_writes);
    ST_LIVE("xnu_live_storage_clk_set_done", 1u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 6 - st_branch_enable is called four times (and is
        * always_inline so its stores are the probe's own) and st_clock_set once, and -Werror is on */

void entry_storage_probe(void)
{
    uint32_t slot_before = 0u, desc = 0u, mapped, section, hc_section;
    uint32_t gcc_mapped, gcc_slot_before = 0u, gcc_desc = 0u, gcc_section;
    uint32_t bcr, cbcr, gate;
    uint32_t core_power, mci_data_ctrl, mci_version, hc_mode, hci_version, capabilities;
    uint32_t loads = 0u;

    if (g_storage_probed != 0u)
        return;
    g_storage_probed = 1u;

    /*
     * **The addresses before they are dereferenced.** 532 section 3.1's arithmetic, published as
     * numbers: the index the one install covers, and the index the *other* window falls in. They are
     * equal on this device and they are two keys rather than one because a device whose two windows
     * straddled a 1 MB boundary would make them differ - and nothing in the installer would say so
     * (532 section 3.3). **`_gcc_section` is the third**, and it is the one 692's press made
     * load-bearing: it is the index of the GATE's megabyte, and `0xfc4ab000` - the reset path's
     * PS_HOLD store - is in it too.
     */
    section = ST_CORE_MEM_BASE >> 20;
    hc_section = ST_HC_MEM_BASE >> 20;
    gcc_section = ST_GCC_BASE >> 20;

    ST_LIVE("xnu_live_storage_calls", 1u);
    ST_LIVE("xnu_live_storage_live_state", g_live_state);
    ST_LIVE("xnu_live_storage_section", section);
    ST_LIVE("xnu_live_storage_hc_mem_section", hc_section);
    ST_LIVE("xnu_live_storage_windows_share_section", (section == hc_section) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_gcc_section", gcc_section);
    ST_LIVE("xnu_live_storage_gcc_share_section", (gcc_section == section) ? 1u : 0u);

    /*
     * **The GATE's own megabyte, installed first, and it is 692's whole correction.** The read below
     * is the probe's third line and it is the load that faulted on 692's press: `0xFC4` is in no table
     * this image builds, and the claim that a store elsewhere in the same megabyte proved it mapped is
     * the defect this install removes. Its own four numbers are published under `_gcc_*` so the two
     * installs are never confused for one another - `_map`/`_slot_before`/`_desc` belong to the
     * storage block below and stay that way.
     *
     * And when THIS install refuses, the gate is not read either, by the same rule the storage block
     * gets: a load through a translation this arm cannot vouch for is a fault, not a measurement. 692
     * showed such a fault comes back with a log, so the rule is not about surviving it - it is about
     * not spending the arm's one reading on an address whose descriptor this run did not write.
     */
    gcc_mapped = entry_mmio_section(ST_GCC_BASE, ST_GCC_BASE, &gcc_slot_before, &gcc_desc);
    ST_LIVE("xnu_live_storage_gcc_map", gcc_mapped);
    ST_LIVE("xnu_live_storage_gcc_slot_before", gcc_slot_before);
    ST_LIVE("xnu_live_storage_gcc_desc", gcc_desc);

    if (gcc_mapped == 0u) {
        ST_LIVE("xnu_live_storage_gate_read", 0u);
        ST_LIVE("xnu_live_storage_gated_out", 0u);
        ST_LIVE("xnu_live_storage_loads", 0u);
        ST_LIVE("xnu_live_storage_writes", 0u);
        return;
    }

    /*
     * **The storage block's install, and it is one call.** 532 section 3.2: a second call for `hc_mem`
     * would find the slot already holding a block descriptor, so `entry_section_install` returns 0
     * without writing and the arm would report a mapping failure *after* mapping the controller
     * correctly. One call, both windows, and the mistake is loud rather than silent.
     *
     * **It is placed before the gate read and not after it**, which is the one ordering change 692's
     * measurement forced - and the safety property is untouched by it, because an install is a
     * *page-table* write and not an access to the medium's controller. The gate still guards every
     * load of the block below; what the reordering buys is that a run which ends up gated out still
     * publishes the four numbers that say whether the section was installed, so "the block was not
     * touched" and "the block could not be reached" stay two different cells.
     */
    mapped = entry_mmio_section(ST_CORE_MEM_BASE, ST_CORE_MEM_BASE, &slot_before, &desc);
    ST_LIVE("xnu_live_storage_map", mapped);
    ST_LIVE("xnu_live_storage_slot_before", slot_before);
    ST_LIVE("xnu_live_storage_desc", desc);
    ST_LIVE("xnu_live_storage_l1", g_live_mmio_l1);
    ST_LIVE("xnu_live_storage_l1_moved", g_live_mmio_l1_moved);
    ST_LIVE("xnu_live_storage_ttbr0", g_live_mmio_ttbr0);
    ST_LIVE("xnu_live_storage_ttbr1", g_live_mmio_ttbr1);

    /*
     * **The gate, out of the megabyte this arm installed first.** `BIT(0)` of the CBCR is the branch
     * enable; `BCR` itself is read beside it because the pair is what makes "this offset is the
     * register I think it is" checkable rather than assumed. `_gate_read` is published as 1 so that a
     * run whose gate could not be reached cannot be confused with one whose gate was read and found
     * closed - and `_gcc_map = 0` above is the only way `_gate_read` is 0.
     */
    bcr = st_read32(ST_GCC_BASE + ST_SDCC1_BCR);
    cbcr = st_read32(ST_GCC_BASE + ST_SDCC1_CBCR);
    gate = cbcr & ST_SDCC1_CLK_ENABLE;

    ST_LIVE("xnu_live_storage_gate_read", 1u);
    ST_LIVE("xnu_live_storage_bcr", bcr);
    ST_LIVE("xnu_live_storage_cbcr", cbcr);
    ST_LIVE("xnu_live_storage_gate", gate);

    /*
     * Two ways to reach the same cell - no section, or a section and a closed branch - and the two
     * keys that tell them apart are `_map` and `_gate`. Neither reads a register of the block.
     */
    if (mapped == 0u) {
        ST_LIVE("xnu_live_storage_gated_out", 0u);
        ST_LIVE("xnu_live_storage_loads", 0u);
        ST_LIVE("xnu_live_storage_writes", 0u);
        return;
    }

    /*
     * The interlock, and the arm's own statement of which side of it the run is on. `gated_out` is 1
     * only when the gate was readable and closed; a run with `gated_out = 1` and `loads = 0` says the
     * section is installed and the block was not touched, which is the whole of that cell.
     */
    if (gate == 0u) {
        ST_LIVE("xnu_live_storage_gated_out", 1u);
        ST_LIVE("xnu_live_storage_loads", 0u);
        ST_LIVE("xnu_live_storage_writes", 0u);
        return;
    }

    ST_LIVE("xnu_live_storage_gated_out", 0u);

    /*
     * **Six loads, in the order 532 section 6 states, and every one of them a read.** `CORE_POWER`'s
     * bit 7 is `CORE_SW_RST` and it is published as its own key because "the core is held in reset" and
     * "the core answers a version word" are two different machines and a reader should not have to
     * shift a word to tell them apart - the same reason the mode bit gets one.
     *
     * **698: and the six is counted rather than written down.** It was the literal `6u` until this step,
     * which is a claim about this block that only a reader could check; `loads` is incremented at each
     * read and published once, so the record's "six loads" and the log's number are two derivations of
     * one fact (m688: a table of things-to-count is a scope claim unless the counter is the table's own).
     * The later blocks publish their own counts for the same reason - `_reg_loads` at rung 3 - and the
     * three numbers are per-block on purpose: one number for the whole function would hide which block
     * grew.
     */
    loads = 0u;
    core_power = st_read32(ST_CORE_MEM_BASE + ST_CORE_POWER);
    loads++;
    mci_data_ctrl = st_read32(ST_CORE_MEM_BASE + ST_CORE_MCI_DATA_CTRL);
    loads++;
    mci_version = st_read32(ST_CORE_MEM_BASE + ST_CORE_MCI_VERSION);
    loads++;
    hc_mode = st_read32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE);
    loads++;
    hci_version = st_read32(ST_HC_MEM_BASE + ST_SDHCI_DMA_ADDRESS);
    loads++;
    capabilities = st_read32(ST_HC_MEM_BASE + ST_SDHCI_CAPABILITIES);
    loads++;

    ST_LIVE("xnu_live_storage_loads", loads);
    ST_LIVE("xnu_live_storage_writes", 0u);
    ST_LIVE("xnu_live_storage_core_power", core_power);
    ST_LIVE("xnu_live_storage_mci_data_ctrl", mci_data_ctrl);
    ST_LIVE("xnu_live_storage_mci_version", mci_version);
    ST_LIVE("xnu_live_storage_hc_mode", hc_mode);
    ST_LIVE("xnu_live_storage_dma_address", hci_version);
    ST_LIVE("xnu_live_storage_capabilities", capabilities);
    /*
     * **The two bits this arm exists for.** 531 section 6 asked whether the mode sequence is a
     * *prerequisite* or a *re-do*: `xnu_live_storage_mode_bit` reads `CORE_HC_MODE & HC_MODE_EN`, so 1
     * says the block the payload inherited is already in SDHCI mode and 0 says the vendor's first two
     * writes are still ahead of it. And `xnu_live_storage_sw_rst` reads `CORE_POWER & CORE_SW_RST`, so 1
     * says the core is being held in software reset by whatever ran before - which would make the
     * version word's answer meaningless rather than absent.
     */
    ST_LIVE("xnu_live_storage_sw_rst", (core_power >> 7) & 1u);
    ST_LIVE("xnu_live_storage_mode_bit", hc_mode & ST_HC_MODE_EN);

#if STAGE90_XNU_STORAGE_PROBE >= 2
    /*
     * **696: rung 2, and it is placed here for the same reason the probe itself is placed before the
     * clock.** Everything above is a reading this sequence is conditional on - the gate that decides
     * whether the block may be touched at all, the version word the sequence refuses itself on, and the
     * two words the sequence's own stores are read against - so if the sequence is reached at all, every
     * one of those is already published and a run that dies inside the sequence still carries the state
     * it was entered in. `_writes` is republished by the sequence with its true count, which is why the
     * `0` above is a statement about the read-only path and not about this one.
     */
    st_mode_sequence(core_power, mci_version);
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 3
    /*
     * **698: rung 3, and it is placed last because it is the only block whose subject is the state the
     * rungs above left.** The mode sequence's four stores are what makes the standard register file mean
     * anything (694 section 3), so a census taken before them would be a census of a block that is not
     * in SDHCI mode - which is exactly the reading 694 could already give. Taken here, it is the state a
     * driver would find on this boot, and it is the before-value for the reset the next step performs.
     *
     * It is guarded by the sequence having run: `_mode_stage == 8` and `_writes == 4` are the two keys
     * that say so, and a run that refused itself (`_mode_refused = 1`, the block not answering) must
     * **not** have its register file read, because a block that did not answer a version word is a block
     * whose census would be a census of nothing. That is the same interlock the sequence uses, applied
     * one rung up, and it is the only condition under which this rung does not run.
     */
    if (g_storage_mode_complete != 0u)
        st_standard_census();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 4
    /*
     * **701: rung 4, and it is placed after the census because the census is its before-values.** The
     * reset's effect on `SOFTWARE_RESET`, `POWER_CONTROL`, `PRESENT_STATE` and `HOST_CONTROL` can only be
     * read as a *change* if those four were read first, which is why 698 was a rung of reads before a
     * rung of writes - and it is guarded by the same interlock one rung up, `g_storage_mode_complete`,
     * because a block that did not answer a version word is a block whose reset must not be written: the
     * census would not have run, so the before-values would be absent and the reset would be an act on a
     * register file this image has never read.
     */
    if (g_storage_mode_complete != 0u)
        st_driver_reset();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 5
    /*
     * **704: rung 5, and it is placed after the reset for the same reason the reset is placed after the
     * census.** The clock surface is the *next* act's before-values (`sdhci_msm_set_clock` is what writes
     * these registers), and a before-value can only be taken before - so a run that dies in the write rung
     * still carries what the block's clock tree looked like on a boot nobody had touched. It is guarded by
     * the same `g_storage_mode_complete` interlock every rung above 2 uses: a block that did not answer a
     * version word is a block whose clock tree must not be read either, because the same branch gates
     * both. The rung writes nothing at any path, so the guard is about *meaning* and not about safety.
     */
    if (g_storage_mode_complete != 0u)
        st_clock_census();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 6
    /*
     * **706: rung 6, the first rung that writes the clock controller, and it is placed last for the
     * reason every rung above 2 is guarded**: `g_storage_mode_complete` says the block answered a version
     * word, so the four branch enables and the MMC clock are written only into a controller this image has
     * already read. The three things it does NOT write are the ones that matter - the RCG (a rate is not
     * on this path, section 2), `BCR 0x04C0` (block reset) and `POWER_CONTROL 0x29` (where the driver's
     * own power path writes 0, a bus-off request) - and the build's clauses are what keep all three out
     * of the linked image rather than this comment.
     */
    if (g_storage_mode_complete != 0u)
        st_clock_set();
#endif
}
