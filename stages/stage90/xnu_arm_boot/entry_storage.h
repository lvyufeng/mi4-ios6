/*
 * 692: the storage probe's one entry point. 692 then pressed it, and the press made the probe's first
 * act a second mapping: the GATE's megabyte (`0xFC4`, the GCC block, which also holds the reset path's
 * PS_HOLD store) is in no table this image builds, so the arm installs it before it reads anything and
 * the storage block's install follows.
 *
 * The probe is `entry_storage.c` and the reason it is a file of its own rather than four more lines in
 * `entry_gic.c` is 483's own split: an object is where one definition of "how this image talks to a
 * device" lives. The GIC probe is that object for the distributor; this is that object for the eMMC
 * controller and for the GCC gate it hangs off, and the three share only the mapper
 * (`entry_mmio_section`) and the live channel.
 *
 * It is compiled in every build and its records are not, which is the same split `entry_timebase.c`
 * and `entry_gic.c` make and for the same reason: the measurement is the step and the records are the
 * instrument. A build with `STAGE90_XNU_STORAGE_PROBE=0` links a probe whose body does nothing at all.
 *
 * **696: the switch is a rung, and the entry point is one call either way.** `..._PROBE=1` is the
 * read-only probe above; `=2` is that probe **and** the vendor's mode sequence
 * (`sdhci-msm.c:2841-2868`: four stores, one bounded poll, a readback after each store), which makes
 * this object the first in the project that writes to a device block rather than reading one. The
 * caller does not change with the rung - `entry_storage_probe` is still one call from
 * `__wrap_platform_cache_idle_exit`, still before 690's clock block, still once per boot - because a
 * rung is a property of the arm's own body and not of where in the boot it happens. What does change
 * is the one thing the caller must not have to know: with the rung at 2, the call can write.
 *
 * **698: `=3` is 2 plus a read-only census of the standard register file** (`PRESENT_STATE`,
 * `HOST_CONTROL`, `POWER_CONTROL`, `CLOCK_CONTROL`, `SOFTWARE_RESET`, `SLOT_INT_STATUS`, the two
 * capability words, `MAX_CURRENT`, and `core_mem`'s power-IRQ mask/control pair), each read at the
 * width the vendor's own accessors use. It is the *before-value* arm for the driver's own
 * `sdhci_reset(SDHCI_RESET_ALL)`: the reset touches four of those registers, and a before-value can
 * only be taken before it. The rung adds no store, and the store census in `build_entry.sh` is what
 * says so rather than this comment - so the ladder is not "how much does this arm do" but "how far up
 * the line it stands", and a rung that adds reads after one that added writes is the shape it allows.
 *
 * **704: `=5` is 4 plus the CLOCK SURFACE, read and never written.** The GCC's four SDCC1 branch words
 * (`SDCC1_BCR`, the apps/AHB/CDCCAL CBCRs), the apps root clock generator's five words
 * (`CMD_RCGR`/`CFG_RCGR`/`M`/`N`/`D`), and `CORE_VENDOR_SPEC 0x10C` - the fifth `core_mem` offset and the
 * one register on this path that is not a clock branch. It is the before-value arm for
 * `sdhci_msm_set_clock`, whose writes land on exactly those registers, and it stores **nothing anywhere**
 * so that "this rung does not write the clock" is a property of the artifact and not of this comment.
 * The rung also decomposes the gate the whole line has been guarded by since 692 into the **two** bits
 * the clock framework itself distinguishes: `CBCR_BRANCH_ENABLE_BIT` (the request, which is all the gate
 * read) and `CBCR_BRANCH_OFF_BIT` (the halt state, which is what "running" means and which no run in
 * this project had ever read). See `docs/experiments/experiment-704-...`, section 2.
 *
 * **706: `=6` is 5 plus THE DRIVER'S FIRST CLOCK SET, and it is the first rung that writes the clock
 * controller.** `sdhci_msm_set_clock` at `clock = 400000` (the DT's `qcom,clk-rates` first entry, reached
 * through `mmc_rescan_try_freq(host, host->f_min)`) is six stores: four `CBCR` read-modify-writes of
 * `BIT(0)` - the four branches `sdhci_msm_prepare_clocks` enables, each followed by the framework's own
 * bounded halt check - and two `CORE_VENDOR_SPEC 0x10C` read-modify-writes (`MCLK_SEL <- DFLT`,
 * `HC_SELECT_IN` cleared), beside the standard's two `CLOCK_CONTROL 0x2C` halfwords with the 20 ms
 * stability poll between them. **It writes no rate at all**: on the first call `sup_clock =
 * get_sup_clk_rate(400000) = 400000` equals `msm_host->clk_rate`, which the probe initialised to
 * `get_min_clock(host)`, so `clk_set_rate` - the only writer of the RCG - is skipped, and the RCG's five
 * words stay rung 5's before-values. It writes no `BCR 0x04C0` (block reset) and no RCG word, and
 * `POWER_CONTROL 0x29` stays unreachable: the build's clauses say so, because the driver's own power
 * path writes **0** there (a bus-off request) and then waits unbounded in
 * `sdhci_msm_check_power_status`. See `docs/experiments/experiment-706-...`, sections 1 and 2.
 */
#ifndef STAGE90_ENTRY_STORAGE_H
#define STAGE90_ENTRY_STORAGE_H

#include <stdint.h>

/*
 * One call, once, and it never touches a register the caller has to have prepared. The probe takes its
 * own mapping, reads its own gate and publishes its own writes count, so its call site needs to know
 * nothing about the storage line - which is what lets that site be the idle exit's own wrapper.
 */
void entry_storage_probe(void);

#endif /* STAGE90_ENTRY_STORAGE_H */
