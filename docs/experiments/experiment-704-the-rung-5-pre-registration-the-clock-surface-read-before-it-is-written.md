# 704: the rung-5 pre-registration — the clock surface, read before anything writes it

703 §7 left the next step named and named *by a reading*: `CLOCK_CONTROL 0x2C` read `0x0003` before and
after rung 4's reset, so `sdhci_set_clock` is still ahead of the driver, and on this SoC that means
`sdhci_msm_set_clock`'s `CORE_VENDOR_SPEC 0x10C` plus `clk_set_rate` on the GCC's SDCC clocks — i.e. a
third device block, and the `0xfc400000` megabyte 692 pressed and measured as **not mapped**.

This record is the design of that step. It is written **before** the code, in the same shape as 696 §4,
698 §4 and 701 §4. **And reading the vendor's clock driver to design it turned up one defect in a reading
this project has relied on since 692** — §2. That is the reason this record is not just a cell list.

**Nothing here is sent and nothing is built.** What follows is what the next rung will be read against.

## 1. The surface, read out of the vendor's own source

`sdhci_msm_set_clock` (`sdhci-msm.c:2402-2535`) is reached from `sdhci_set_clock`
(`sdhci.c:1211-1214`: `if (host->ops->set_clock) { host->ops->set_clock(host, clock); if (host->quirks &
SDHCI_QUIRK_NONSTANDARD_CLOCK) goto ret; }` — and `sdhci_msm_ops` sets neither that quirk nor a
`set_clock` that writes the standard file). The registers it can touch, with the line each is read from:

| # | register | where | read by | written by |
| --- | --- | --- | --- | --- |
| 1 | `CORE_VENDOR_SPEC 0x10C` (`core_mem`) | `sdhci-msm.c:92` | `:2420`, `:2430`, `:2466`, `:2482`, `:2489` — **five read sites** | `:2421`, `:2432`, `:2470`, `:2486`, `:2492` — read-modify-write of `CORE_CLK_PWRSAVE` (`:93`, bit 1) and `CORE_HC_MCLK_SEL_*` (`:94-96`, bits 9:8) |
| 2 | the apps branch, `SDCC1_APPS_CBCR 0x04C4` (GCC) | `clock-8974.c:339` | via `clk_prepare_enable(msm_host->clk)` → `clk_ops_branch` | `__branch_enable_reg` → `CBCR <- \|CBCR_BRANCH_ENABLE_BIT`, then a halt check |
| 3 | the AHB branch, `SDCC1_AHB_CBCR 0x04C8` | `clock-8974.c:340` | via `clk_prepare_enable(msm_host->pclk)` | same, and its `branch_clk` carries **`has_sibling = 1`** (`:2340-2341`) |
| 4 | `SDCC1_CDCCAL_SLEEP_CBCR 0x04E4`, `SDCC1_CDCCAL_FF_CBCR 0x04E8` | `clock-8974.c:341-342` | the two CD-calibration branches (`:2362`, `:2373`) | same |
| 5 | the apps RCG, `SDCC1_APPS_CMD_RCGR 0x04D0` and four words after it | `clock-8974.c:140`, `:1584` | `sdcc1_apps_clk_src` is an `rcg_clk` with `.set_rate = set_rate_mnd` (`:1583-1584`) | `clk_set_rate(msm_host->clk, sup_clock)` (`sdhci-msm.c:2520`) → `set_rate_mnd` → an update poll |
| 6 | `BCR 0x04C0` | `clock-8974.c:244` | `branch_clk_reset` | `BCR <- \|BCR_BLK_ARES_BIT` — the block reset, **not on this path** |

The RCG's five words are `clock-local2.c`'s own offsets — `CMD_RCGR_REG` = base, then `+0x4`, `+0x8`,
`+0xC`, `+0x10` (`:49-53`):

```
CMD_RCGR  0x04D0   BIT(1) root enable, BIT(0) config update, BIT(31) root status   (:61-65)
CFG_RCGR  0x04D4   BM(4,0) divider, BM(10,8) source select, BM(13,12) MND mode    (:68-71)
M         0x04D8   the M in M/N:D                                                 (:51)
N         0x04DC   the N — N != 0 is what selects the dual-edge mode              (:51, :144-145)
D         0x04E0   the D                                                            (:53)
```

**And every access in the table above is 4-aligned**, so 698's alignment census — which already covers
every load and store in `entry_storage_probe` for the rung's whole body — checks all of them for free.

**The path's own write order, for the rung after this one**: read-modify-write `CORE_VENDOR_SPEC` (a field
the vendor also writes, so it is safe by construction), then `clk_prepare_enable` on pclk/clk (a `CBCR`
read-modify-write **plus a halt handshake**), then `clk_set_rate` → `M`/`N`/`D`, `CFG_RCGR`, then set
`CMD_RCGR`'s update bit and **poll it clear** (`rcg_update_config`, `clock-local2.c:90-108`, bound
`UPDATE_CHECK_MAX_LOOPS 500` × `udelay(1)`, `:44`) — a *bounded* poll of exactly the shape rung 4's reset
poll is, and the third instance of that shape on this line.

## 2. What designing this found: the probe's gate is a **half** reading of a two-bit contract

`entry_storage.c` publishes, and the whole storage line is guarded by:

```c
gate = cbcr & ST_SDCC1_CLK_ENABLE;   /* #define ST_SDCC1_CLK_ENABLE 0x1u */
```

531 §8 read that bit out of the vendor and the file has carried it since 692 as *the* branch enable. It
is. **It is not the whole answer**, and the register says so two bits away (`clock-local2.c:62-63`):

```c
#define CBCR_BRANCH_ENABLE_BIT   BIT(0)    /* what BIT(0) means */
#define CBCR_BRANCH_OFF_BIT      BIT(31)   /* and what "the branch is running" means */
```

`BIT(0)` says the enable has been **requested** — it is the bit the driver *writes*
(`__branch_enable_reg`, `:380-382`: `cbcr_val = readl(CBCR_REG(branch)); cbcr_val |=
CBCR_BRANCH_ENABLE_BIT; writel(cbcr_val, CBCR_REG(branch));`). `BIT(31)` says the branch is **not halted**
— and it is the bit the driver *then polls*, to a bound, before it believes the clock is on (`:386-387` →
`branch_clk_halt_check`, `:333-360`, `HALT_CHECK_MAX_LOOPS 500` × `udelay(1)`). The framework's own
handoff test reads the pair the same way: `branch_clk_handoff` (`:490-497`) returns `HANDOFF_DISABLED_CLK`
**on `BIT(31) set` alone**, without looking at `BIT(0)` at all.

**The two bits can disagree**, and there is one very ordinary state in which they do: a branch whose
enable is requested and whose **parent root is not running** is enable-requested *and still halted* — the
clock does not propagate until the RCG above it is on. `rcg_clk_prepare` (`:170-178`) even `WARN`s about
the mirror case on the root's side.

**So the sentence "the gate is open" was a statement about the request and not about the clock.** 694's
measurement, `_cbcr = 0x00004ff1`, *satisfies both halves* — bit 0 set, bit 31 clear — so the arm's
conclusion stands unchanged. But that is a fact about a word the run never decomposed, and the run that
has spent fourteen presses on this line never read bit 31. **This is [[mi4-one-value-two-definitions]] in
the shape the memory's own table calls the most-repeated failure here: one quantity — "is this branch
running?" — with two readings, and the code silently using the one that can be true while the clock is
still stopped.**

It is also why rung 5 is a *reading* rung and not a writing one: the fault this defect would cause is not
a fault at all. A register read through a branch whose parent root is off is the bus wait nothing ends —
the failure 692 §… named as the one this project cannot read a log out of. The gate has been the
interlock against that failure since 692, and **the interlock has been reading half of itself.**

## 3. Pre-registered: what rung 5 is

**Rung 5 = rungs 1–4 unchanged, plus a read-only census of the clock surface, at the tail of
`entry_storage_probe`, after the reset.** No store, anywhere, to any window. It is the exact analogue of
698 being a rung of reads before 701's rung of writes: these are the before-values `sdhci_set_clock`
needs, and a before-value can only be taken before.

Placed after the reset because the reset is rung 4's tail and re-reading `CLOCK_CONTROL` after everything
is what makes "this census moved nothing" a measurement rather than a claim. Guarded by the same
`g_storage_mode_complete` interlock every rung above 2 uses: a block that did not answer a version word is
a block whose clock tree must not be read either — the interlock is the same one, one rung up.

| key | what a reading means, and what the alternative would be |
| --- | --- |
| `_clk_calls` | the census ran |
| `_clk_loads` | **counted bottom-up at each read and published once**, like `_loads` and `_reg_loads` — so the record's "ten words" and the log's number are two derivations of one fact |
| `_clk_writes` | **`0` on every path.** The rung's whole claim, published rather than inferred from a comment |
| **`_clk_apps_en` / `_clk_apps_off` / `_clk_apps_hw`** | `BIT(0)` / `BIT(31)` / `BIT(1)` of `_cbcr` — **the §2 defect's cells.** `en=1, off=0` is a branch that is requested **and running**; `en=1, off=1` is the state the old single-bit reading could not tell from "on", and it is the state in which the register file would answer *or hang*. `_clk_apps_hw` is `CBCR_HW_CTL_BIT`: set means the branch is under hardware gating, and the framework skips its own halt check there (`:346-347`) |
| `_clk_bcr`, `_clk_bcr_ares` | the block reset word and `BCR_BLK_ARES_BIT` (`clock-local2.c:66`). 694 published `_bcr = 0` without saying what a bit of it means; `ares=0` says the block is **not held in reset**, which is the cell that makes the branch readings above mean anything |
| `_clk_cbcr_ahb` + `_clk_ahb_{en,off,hw}` | **`SDCC1_AHB_CBCR 0x04C8`, a register the probe has never read.** It is the `pclk` `sdhci_msm_prepare_clocks` enables explicitly, and its `branch_clk` carries `has_sibling = 1` — so `clk_set_rate` on it is `-EPERM` by the framework's own rule (`:444-446`, `:460-462`) and enabling it is a different act from enabling the apps branch. **This is the genuinely open question of rung 5**: the register file answers today, which *suggests* this branch is on, and no run has ever read the register. A 0 here beside an answering register file would be a finding about which clock actually gates the controller |
| `_clk_cbcr_cdccal_sleep` / `_clk_cbcr_cdccal_ff` + their three bits | the two CD-calibration branches. Their parents are `cxo_clk_src` and not the apps root (`clock-8974.c:2362-2373`), so they can be in a state the other two are not — and an off calibration clock is consistent with a controller answering a version word while not being calibrated for a fast mode |
| `_clk_rcg_cmd`, `_clk_rcg_root_en`, `_clk_rcg_update`, `_clk_rcg_root_status` | the apps RCG's command word. `update=0` **before** anything writes it is the premise `rcg_update_config`'s poll needs (nothing is mid-update); `root_en=0` would mean the root itself is off, in which case every branch under it reads enable-requested and halted, and the §2 pair would separate them. Root status at `BIT(31)` |
| `_clk_rcg_cfg`, `_clk_rcg_src`, `_clk_rcg_div`, `_clk_rcg_mnd_mode` | **which parent the apps clock is on and what it is divided by** — the words `set_rate_mnd` reads and writes. `mnd_mode` (bits 13:12) reads the **dual-edge** value (`0x2`) only when `N != 0` (`:144-145`), so it is a cross-check on `_clk_rcg_n` and not an independent fact |
| `_clk_rcg_m`, `_clk_rcg_n`, `_clk_rcg_d` | the MND divider itself |
| `_clk_vendor_spec`, `_clk_vendor_pwrsave`, `_clk_vendor_mclk_sel` | **`CORE_VENDOR_SPEC 0x10C`, the fifth `core_mem` offset and the one register on this path that is not a clock branch.** `pwrsave` (bit 1) and `mclk_sel` (bits 9:8) are the two fields `sdhci_msm_set_clock` reads before it decides, and the MCLK select is what it writes unconditionally on the non-HS400 path |
| `_clk_clock_control_after` | `CLOCK_CONTROL 0x2C` re-read at the end of the census. `0x0003` again — bit 2 still clear — is the before-value for the write rung and the reading that says this census moved nothing |

## 4. What rung 5 will **not** do, and the two claims it must not be read as making

* **No store.** Not to the GCC, not to `core_mem`, not to `hc_mem`. The rung's write count is published and
  it is 0 on every path.
* **No rate.** The census publishes the MND and source-select *fields*; it does **not** publish a
  frequency. Converting them to Hz needs the parent's rate (`gpll0`, `gpll4`, `cxo` are the three parents
  in `ftbl_gcc_sdcc1_4_apps_clk`, `clock-8974.c:1564-1574`), which is a table this image does not carry —
  and computing one from a config decode beside the framework's own cached `msm_host->clk_rate` would be
  the same one-value-two-definitions defect §2 is about, one level up. The fields are published and the
  arithmetic is named as owed.
* **No enable.** The census reads the AHB branch and does not turn it on. If `_clk_ahb_en = 0` the finding
  is published, not acted on: enabling it is a `CBCR` write plus a halt handshake on a block whose parent
  root may itself be off, which is a step with its own pre-registration.

## 5. The build clause rung 5 needs, and why the property is currently incidental

`build_entry.sh`'s store classifier knows exactly two device addresses — `core_mem` (`0xf9824000`) and
`hc_mem` (`0xf9824900`) — and classifies anything else a device store as `DEVBAD`, refusing the build. So
**a store to the GCC megabyte is refused today, by a clause whose stated subject is "not one of the two
windows this controller declares"** rather than by a clause about the clock.

That is the shape [[mi4-a-claim-in-a-comment-is-not-a-check]] calls out: the safety is real and it is a
side effect of the classifier's scope, and rung 5 is the rung that makes a third window load-bearing. So
rung 5 adds the window to the classifier as its own class and asserts its store set is **empty at every
rung**, which turns an incidental refusal into a named one — and makes the clause's own message true
again.

The `#error` bound moves to `> 5`, the `case` gains `5`, and `tools/verify_press_ready.sh` gains the rung-5
arm paragraph and cell list.

## 6. Owed by rung 5, and what the write rung will be read against

* **The write rung (6)**: `CORE_VENDOR_SPEC 0x10C`'s MCLK select is a read-modify-write of a field the
  vendor also writes — the safest of the writes; `clk_prepare_enable` on the AHB branch is a `CBCR`
  read-modify-write **plus a halt handshake** on a branch with `has_sibling = 1`; and `clk_set_rate` on the
  apps RCG is `M`/`N`/`D`, then `CFG_RCGR`, then `CMD_RCGR`'s update bit **and a bounded poll of it** —
  `UPDATE_CHECK_MAX_LOOPS 500`, `clock-local2.c:44`, `:90-108`. Rung 5's `_clk_rcg_update = 0` and
  `_clk_rcg_cmd` are that poll's before-values, and rung 5 is what lets rung 6 be a bounded write instead
  of an experiment.
* **The rate arithmetic** (§4), named and not done.
* **Carried, unchanged**: the width clause of the store census has still not been observed to fire; what
  actually returns a run (8/17/24/27/24/27 s); the ending's first store still faulting into a panic
  (`RESTART_REASON 0x0fa0065c`); the gate's narration for `STAGE90_XNU_STORAGE_PROBE` being three rungs
  short; and **TWRP-to-storage stays withheld** — the user's condition is that the OS can be entered and
  stays, and the OS is not observed doing that.
