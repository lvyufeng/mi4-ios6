# 706: the rung-6 pre-registration — the driver's first clock set, the rate write that is not on this path, and the boundary that keeps the bus-off register unreachable

704 pre-registered rung 5 as a **read-only census of the clock surface**, and 705 pressed it: twelve words
answered, `_clk_writes = 0`, and the surface's own disagreement is now measured — all four SDCC1 branches
`en=1`/`off=0`/`hw=0` while the apps root's `root_en` **and** `root_status` are both clear. This is the
pre-registration of the **first rung that writes the clock controller**, read out of the vendor's own
source before any code, and its design turned on a finding that makes it much smaller than the ladder's
name for it suggests.

## 1. The path, read out of the vendor's own source

The writer is `sdhci_msm_set_clock` (`sdhci-msm.c:2402-2535`), and it is reached from the standard core,
not directly: `mmc_power_up` (`drivers/mmc/core/core.c:1934`, `:1968`) sets `ios.clock = host->f_init` and
calls `mmc_set_ios`, and `sdhci_do_set_ios` (`sdhci.c:1635-1636`) calls `sdhci_set_clock(host, ios->clock)`
— which calls the vendor hook **first** (`sdhci.c:1211-1214`) and then does its own two writes
(`sdhci.c:1288-1289`, `:1305-1306`). `host->f_init` is `host->f_init = freq` from `mmc_rescan_try_freq(host, host->f_min)`
(`core.c:3077-3079`, called at `:3251`), and `host->f_min` is `mmc->f_min = get_min_clock(host)` =
`sup_clk_table[0]` (`sdhci-msm.c:2233`, wired at `:2660`). The DT's table for this SoC is
`msm8974pro.dtsi:1768` — `qcom,clk-rates = <400000 20000000 25000000 50000000 100000000 192000000
384000000>` — so **the first clock the driver asks for is 400000 Hz**, and on this board the *last* entry
is `max_clk = host->ops->get_max_clock(host)` = 384000000 (`sdhci-msm.c:2240`).

The hook, in order, at `clock = 400000`:

| # | the vendor's line | what it does |
| --- | --- | --- |
| 1 | `:2411` `if (!clock)` | not taken — the arm is the `400000` call, not the power-off call |
| 2 | `:2423` `sdhci_msm_prepare_clocks(host, true)` | the four branch enables, in order: `pclk` → `clk` → `bus_clk` (→ `ERR_OR_NULL` on this board) → `ff_clk` → `sleep_clk`, each `clk_prepare_enable` = a **CBCR read-modify-write of `BIT(0)`** (`clock-local2.c:380-382`) followed by **`branch_clk_halt_check(HALT, BRANCH_ON)`** (`:386-387` → `:333-371`) |
| 3 | `:2427-2441` the `curr_pwrsave` pair | **not taken**: both arms need `clock > 400000` or `curr_pwrsave`, and `_clk_vendor_pwrsave` measures 0 |
| 4 | `:2495-2513` the non-HS400 arm | two more read-modify-writes of `CORE_VENDOR_SPEC 0x10C`: set `CORE_HC_MCLK_SEL_DFLT` (`2 << 8`, `:94`), then clear `CORE_HC_SELECT_IN_EN` (`1 << 18`) and `CORE_HC_SELECT_IN_MASK` (`7 << 19`, `:98-100`) |
| 5 | `:2517-2520` `if (sup_clock != msm_host->clk_rate) clk_set_rate(...)` | **NOT TAKEN — and §2 is why** |

Then `sdhci_set_clock`'s own arithmetic, with the two inputs 705 already measured: `host->version` = `2`
(699's `_reg_host_version = 0x00001102`, spec field `0x02` = `SDHCI_SPEC_300`, `sdhci.h:248`) and
`host->clk_mul = (caps1 & SDHCI_CLOCK_MUL_MASK) >> 16` (`sdhci.c:3291`, `sdhci.h:212-213`) = `(0x00008007
& 0x00FF0000) >> 16` = **0**, so the Programmable-Clock branch (`sdhci.c:1230-1253`) is **not** taken and
`SDHCI_HOST_CONTROL2 0x3E` need not be read at all. The divisor loop is the spec-3.00 one
(`sdhci.c:1254-1267`): `div = 2; div < 2046; div += 2; if (max_clk / div <= clock) break` → `384000000/960 =
400000` → **`div = 960`**, `real_div = 960`, `div >>= 1` → 480. Packed per `sdhci.c:1285-1287` and
`sdhci.h:101-106`: `((480 & 0xFF) << 8) | (((480 & 0x300) >> 8) << 6) | SDHCI_CLOCK_INT_EN` = **`0xE041`**,
and after the stability poll adds `SDHCI_CLOCK_CARD_EN` (`sdhci.h:107`) → **`0xE045`**. The two writes are
`sdhci_writew` (`sdhci.c:1289`, `:1306`) — halfwords at `0x2C` — and the poll between them is
`while (!(readw(CLOCK_CONTROL) & SDHCI_CLOCK_INT_STABLE))` bounded by `timeout = 20` × `mdelay(1)`
(`sdhci.c:1291-1304`), i.e. **≤ 20 ms**.

**So rung 6 is six device writes and five bounded polls**: four CBCR read-modify-writes on the GCC (each
followed by a halt poll), two `CORE_VENDOR_SPEC` read-modify-writes on `core_mem` — **the fifth and sixth
offset of a window this project's census had bounded to four since 698** — and the two standard
`CLOCK_CONTROL` halfwords on `hc_mem`.

## 2. What designing this found: the first clock set writes **no rate at all**

`sdhci_msm_set_clock`'s fifth step is the one the whole line exists for — and it does **not** run on the
first call. `sdhci_msm_get_sup_clk_rate(host, 400000)` (`:2245-2268`) walks `sup_clk_table` and returns
`400000` (the first entry equals the request), while `msm_host->clk_rate` was initialised at probe time to
`sdhci_msm_get_min_clock(host)` = `sup_clk_table[0]` = `400000` (`:2795`). `sup_clock != msm_host->clk_rate`
is therefore **false**, and `clk_set_rate` — the only thing on this path that writes the RCG — is skipped.

**One quantity, two readings, and they disagree by 480× on the first call**: the driver's own bookkeeping
says the controller's clock is 400 kHz, and 705 measured the register file saying `src = 5` (`gpll4`),
`div = 4`, `m = n = 0` — the `F(192000000, gpll4, 4, 0, 0)` row of `ftbl_gcc_sdcc1_apps_clk_ac`
(`clock-8974.c:1589`, the table `msm8974_pro_clock_override` installs for this SoC at `:5834`), i.e. **192
MHz**. The bootloader's rate and the driver's belief are two readings of one value and nothing reconciles
them until the first *speed change*, when `sup_clock` becomes e.g. 200000000 and the RCG write finally
happens. That is this file's defect class arriving inside the vendor's own driver, and it is the reason
rung 6 can be a small step:

* **rung 6 writes no rate.** No `M`/`N`/`D`, no `CFG_RCGR`, no `CMD_RCGR` update bit — the write rung's
  premise (`_clk_rcg_update = 0`, `_clk_rcg_cmd = 0`) stays a premise, and the RCG's before-values stay
  before-values. The RCG write belongs to the **speed-change rung**, which is a later step and whose
  deadline is a bounded poll of the update bit (`UPDATE_CHECK_MAX_LOOPS 500`, `clock-local2.c:44`, `:90-108`).
* **and the halt polls are already satisfied**, for a reason 705 handed over: `branch_clk_halt_check`'s
  on-test accepts `BRANCH_ON_VAL` (`0x0`) **or** `BRANCH_NOC_FSM_ON_VAL` (`0x2`, `clock-local2.c:326-328`,
  `:357-358`) in bits `31:28`, and 705's `_clk_ahb_cbcr = 0x2000cff1` reads exactly `0x2` there while the
  other three read `0x0`. So the four polls should each pass on their **first** read, and a poll count
  above one is the news rather than the expectation.

## 3. Pre-registered: what rung 6 is

**Rung 6 = rungs 1–5 unchanged, plus the driver's first clock set** — the six writes and five polls of §1
— at the tail of `entry_storage_probe`, after rung 5's census, guarded by the same
`g_storage_mode_complete` interlock every rung above 2 uses.

| key | what a reading means, and what the alternative would be |
| --- | --- |
| `_clk_set_calls` | the sequence ran |
| `_clk_set_writes` | **counted bottom-up at each store.** Expected **six**: four CBCR read-modify-writes and two `CORE_VENDOR_SPEC` ones. A count above six means a store reached a path this record does not describe |
| `_clk_set_gcc_writes` | the subset in the GCC megabyte: expected **four**. Published separately because "the four clock-controller writes are the branch enables" is the rung's central safety claim |
| **`_clk_set_rate_writes`** | **expected `0`** — §2's finding, published as a number rather than inferred from a comment. A non-zero value means the driver's bookkeeping did not start at `get_min_clock` and the RCG write *is* on this path, which would refute §2 |
| `_clk_set_<b>_before` / `_after` | per branch (`ahb`, `apps`, `ff`, `sleep`), the word before and after its read-modify-write. `before == after` is **expected and is the point**: the enable bit is already set, so this rung's GCC writes change no bit — and a differing pair is a finding about the handed-over state |
| `_clk_set_<b>_polls` / `_ticks` | how many reads the halt check took and how long they took. **Expected `0x1` (and 0 ticks) on all four** (§2), against the driver's bound of `HALT_CHECK_MAX_LOOPS 500` at 1 µs each |
| `_clk_set_<b>_halted` | the terminal state the poll tested: `0` = the branch is on / NoC-FSM-on. Published rather than inferred from the count |
| `_clk_set_vendor_before` | `CORE_VENDOR_SPEC 0x10C` before the two writes. 705 measured `0x00000000` |
| `_clk_set_vendor_w1` / `_w2` | the two words *written*: `(before & ~0x300) \| 0x200` = **`0x00000200`**, then `w1 & ~(1 << 18) & ~(7 << 19)` = **`0x00000200`** — the second being a read-modify-write of a field already clear, which is what makes the pair a *sequence* rather than one write |
| `_clk_set_vendor_after` | read back: expected `0x00000200`, i.e. **MCLK select = 2 (`DFLT`)**, the value the vendor's non-HS400 path always wants. `0` here would mean the field did not take |
| **`_clk_set_max_clk`** | **`384000000`** — the divisor arithmetic's one input that is not a register (§1: the DT's last `qcom,clk-rates` entry). Published so the written word's derivation is checkable in the log |
| `_clk_set_div` / `_clk_set_real_div` | **`0x3C0` (960)** / `960`. If this reads `500`, the board's table is the standard one (`msm8974.dtsi:339`) and the next cell's alternative is the true one — i.e. **the cell measures the DT** |
| **`_clk_set_cc_before`** | `CLOCK_CONTROL 0x2C` before: expected `0x0003` (705's own last reading, and 699's before that) |
| **`_clk_set_cc_int`** | the word written first: **expected `0xE041`** = divisor 960 packed, `SDHCI_CLOCK_INT_EN` set, **`SDHCI_CLOCK_CARD_EN` clear**. `0xFA41` is the alternative of the 200-MHz table |
| `_clk_set_cc_stable` / `_polls` / `_ticks` | the standard's own 150 ms/20 ms contract, bounded here at the driver's `timeout = 20` ms and published as its own number: `_stable = 1` means `INT_STABLE` asserted |
| **`_clk_set_cc_card`** | the word written second: **expected `0xE045`** — `INT_EN \| CARD_EN`. **This is the first time this image sets the SD clock enable bit**, and the last reading of the rung |
| `_clk_set_cc_after` | `CLOCK_CONTROL` read back: expected `0xE045` with bit 1 asserted |
| **`_clk_set_divisor_alt`** | published so the two candidate DT tables are told apart in the log rather than in this document: `1` means the word written is the **200-MHz-table** alternative (`0xFA41`/`0xFA45`) and the record's assumed `max_clk` was wrong |
| `_clk_set_done` | the sequence completed |

**What the arm's own hazard is, named rather than discovered**: the four CBCR writes are read-modify-writes
of a bit that is already set, so they cannot disable a branch; the two vendor writes touch only `MCLK_SEL`
and the `HC_SELECT_IN` pair, neither of which can gate a clock; and **the GCC's `BCR 0x04C0`** (block
reset, `BCR_BLK_ARES_BIT`, `clock-local2.c:66`) **and the whole RCG** (`0x04D0`–`0x04E0`) are **refused by
the build** rather than avoided by review. The one write that changes the medium's state is
`CLOCK_CONTROL ← 0xE045`, and it changes it in the direction the driver wants: the SD clock goes from
*off* to *on* at 200 kHz, with the card unpowered and no command in flight.

## 4. What rung 6 will **not** do, and the boundary it draws

* **No rate.** §2: the RCG is not written at any value. `_clk_set_rate_writes = 0` is published.
* **No block reset, no enable-disable.** `BCR 0x04C0` is not read-into and not written; no CBCR bit is
  *cleared*.
* **No card power — and this is a boundary, not an omission, and the driver's own code is the reason.**
  The same `sdhci_do_set_ios` call that reaches `sdhci_set_clock` also reaches `sdhci_set_power(host,
  ios->vdd)` (`sdhci.c:1658-1665`), and `sdhci_set_power` does **three** things this project has kept away
  from that register: unless `SDHCI_QUIRK_SINGLE_POWER_WRITE` is set it writes **`0` to `POWER_CONTROL
  0x29`** — *the bus-off request 531 section 8 names* — at `sdhci.c:1352-1353`; it then calls
  `check_power_status(REQ_BUS_OFF)` (`:1354-1355`) and, after setting `SDHCI_POWER_ON`, again with
  `REQ_BUS_ON` (`:1370-1371`); and `check_power_status` on this SoC is `sdhci_msm_check_power_status`
  (`sdhci-msm.c:2179-2209`), which **ends in an unbounded
  `wait_for_completion(&msm_host->pwr_irq_completion)`** — the one construct on this whole path that
  nothing in this image could ever end. So the power act is **both** hazards in three lines, it is the
  next rung's subject and not this one's, So the power-up act is the **next rung's** subject and not this one's, and
  rung 6 is the first step of the sequence that can be taken with `POWER_CONTROL` still **unreachable in
  the linked image** — the clause 701 built for the reset rung stays exactly as it is (`:29986-30017`).
  Enabling the SD clock before the card is powered is the driver's own order, and the vendor's own comment
  says so (`sdhci.c:1651-1657`): *"we may end up enabling card clock before giving power to the card"*.
* **No command, no sector, no partition table, no mount**, and no driver beyond the fixture. The goal's
  floor (pid 1, the driver's `open` answering 0, the fixture's `0xfeedface`) is unchanged.
* **TWRP-to-storage stays withheld**: the OS is not observed entering and **staying**.

## 5. The build clauses rung 6 needs

Three of them, and each changes a clause that is *currently* an assertion of emptiness:

1. **the GCC window's store set.** 704's clause asserts it is empty **at every rung**. From rung 6 it
   becomes: empty below rung 6, and at rung 6 exactly **`0x4C8, 0x4C4, 0x4E8, 0x4E4` in that order as
   32-bit stores** — the four branches in `sdhci_msm_prepare_clocks`' order — with an explicit refusal of
   `0x4C0` (block reset), of `0x4D0`–`0x4E0` (the RCG), of any wider/narrower width, and of any fifth
   offset. **The offsets are the property; the values are the run's** (`_clk_set_<b>_wrote` and the
   after-reads).
2. **the `core_mem` window.** `120 0 120 120` becomes `120 0 120 120 268 268` at rung 6 — the two
   `CORE_VENDOR_SPEC` read-modify-writes, with `268 = 0x10C`. This is the **fifth and sixth** offset of a
   window the census has bounded to four since 698, and the clause's message must say which offset is
   which rather than counting.
3. **the `hc_mem` window.** One `strb` at `47` becomes `47 44 44` with mnemonics `strb strh strh` — the
   reset, then the two `sdhci_writew` halfwords at `CLOCK_CONTROL 0x2C`. **`POWER_CONTROL 0x29` (41) must
   still be absent**, and that is the assertion that keeps §4's boundary a property of the linked image.
   The width census needs no change (`44 % 2 == 0` passes, and `strh`'s width is derived from its own
   mnemonic) — but a `str` at `44` would **also** pass it, which is why the mnemonic is checked here.
   (This file first wrote the pair as `sth sth` and the clause as the same: `sth` is the *Thumb* spelling
   of the halfword store and `objdump` prints `strh` for this image's ARM encoding, so the clause's first
   reading of a non-empty `hc_mem` set was `[strb strh strh ]` against a record of `strb sth sth` — a
   comparison that could never have passed, and the build refused it. Both spellings are `strh` now.)

## 6. Owed by rung 6, and what the speed-change rung will be read against

* **the RCG write**, and it is now a *bounded* act with measured before-values: `M`/`N`/`D`, then
  `CFG_RCGR`, then `CMD_RCGR`'s update bit and the poll of that same bit (`clock-local2.c:90-108`, bound
  `UPDATE_CHECK_MAX_LOOPS 500`, `:44`). 705's `_clk_rcg_update = 0` and `_clk_rcg_cmd = 0` are its
  before-values; rung 6 leaves them before-values.
* **the card-power rung**, which is where `POWER_CONTROL` becomes reachable and where
  `sdhci_msm_check_power_status`'s unbounded wait has to be *not* taken — `_reg_pwrctl_mask = 0x0f` and
  `_reg_pwrctl_status = 0` are the readings that say what such a wait would be waiting on.
* **the two readings of §2**: whether a later rung's `clk_set_rate` is what reconciles the driver's
  `clk_rate` with the hardware, and whether this SoC's first speed change is where the RCG's clear
  `root_en` (§2 of 704) stops meaning "the root is off".
* **what returns a run** (8/17/24/27/24/27/25 s), the ending's first store still faulting
  (`RESTART_REASON 0x0fa0065c`), the width clause never observed firing on a widened device store, and
  the gate's narration of `STAGE90_XNU_STORAGE_PROBE` now being **five rungs short** (peer lane, by
  message and never by edit).
