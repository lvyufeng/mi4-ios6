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
 */
#include <stdint.h>

#include "entry_storage.h"

#ifndef STAGE90_XNU_STORAGE_PROBE
#define STAGE90_XNU_STORAGE_PROBE 0
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
 */
#define ST_CORE_MEM_BASE        0xf9824000u
#define ST_HC_MEM_BASE          0xf9824900u
#define ST_CORE_POWER           0x00u
#define ST_CORE_MCI_DATA_CTRL   0x2Cu
#define ST_CORE_MCI_VERSION     0x050u
#define ST_CORE_HC_MODE         0x78u
#define ST_SDHCI_HCI_VERSION    0x00u
#define ST_SDHCI_CAPABILITIES   0x40u

/* 531 section 8's SDCC1 pair, out of `clock-8974.c:244` and the `+4` the UART pair measured. */
#define ST_GCC_BASE             0xfc400000u
#define ST_SDCC1_BCR            0x04C0u
#define ST_SDCC1_CBCR           0x04C4u
#define ST_SDCC1_CLK_ENABLE     0x1u

static uint32_t st_read32(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static uint32_t g_storage_probed;

void entry_storage_probe(void)
{
    uint32_t slot_before = 0u, desc = 0u, mapped, section, hc_section;
    uint32_t gcc_mapped, gcc_slot_before = 0u, gcc_desc = 0u, gcc_section;
    uint32_t bcr, cbcr, gate;
    uint32_t core_power, mci_data_ctrl, mci_version, hc_mode, hci_version, capabilities;

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
     */
    core_power = st_read32(ST_CORE_MEM_BASE + ST_CORE_POWER);
    mci_data_ctrl = st_read32(ST_CORE_MEM_BASE + ST_CORE_MCI_DATA_CTRL);
    mci_version = st_read32(ST_CORE_MEM_BASE + ST_CORE_MCI_VERSION);
    hc_mode = st_read32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE);
    hci_version = st_read32(ST_HC_MEM_BASE + ST_SDHCI_HCI_VERSION);
    capabilities = st_read32(ST_HC_MEM_BASE + ST_SDHCI_CAPABILITIES);

    ST_LIVE("xnu_live_storage_loads", 6u);
    ST_LIVE("xnu_live_storage_writes", 0u);
    ST_LIVE("xnu_live_storage_core_power", core_power);
    ST_LIVE("xnu_live_storage_mci_data_ctrl", mci_data_ctrl);
    ST_LIVE("xnu_live_storage_mci_version", mci_version);
    ST_LIVE("xnu_live_storage_hc_mode", hc_mode);
    ST_LIVE("xnu_live_storage_hci_version", hci_version);
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
    ST_LIVE("xnu_live_storage_mode_bit", hc_mode & ST_SDCC1_CLK_ENABLE);
}
