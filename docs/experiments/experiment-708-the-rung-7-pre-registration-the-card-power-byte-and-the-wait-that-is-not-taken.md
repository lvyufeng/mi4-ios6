# 708: the rung-7 pre-registration — the card-power byte, the `REQ_BUS_ON` wait that is not taken, and the quirk that takes one of 706's two hazards off the path

706 pre-registered the driver's first clock set and called the power act **"both hazards in three
lines"**: the `0` written to `POWER_CONTROL 0x29` (the bus-off request 531 §8 names) and
`sdhci_msm_check_power_status`' unbounded `wait_for_completion`. Reading the same path for rung 7
finds that **the first of those two is not on this host's path at all** — `SDHCI_QUIRK_SINGLE_POWER_WRITE`
is set (`sdhci-msm.c:2897`) — and that `mmc_power_up` is **two `mmc_set_ios` passes**, of which rung 6
was the *second*. So rung 7 is both smaller than the pre-registration implied and differently placed:
**one byte store and one wait, on the pass the driver itself uses for power.**

## 1. The path, read out of the vendor's own source — and two corrections to 706

### 1.1 `mmc_power_up` is two passes, and power comes FIRST

`mmc_power_up` (`core.c:1904`) sets `ios.vdd`, `ios.power_mode = MMC_POWER_UP` and calls `mmc_set_ios`
(`:1916-1924`) **with `ios.clock` still 0**; then delays 10 ms; then sets `ios.clock = host->f_init` and
`ios.power_mode = MMC_POWER_ON` and calls `mmc_set_ios` again (`:1934-1937`). In `sdhci_do_set_ios`:

| pass | `ios.clock` | `ios.power_mode` | what runs |
| --- | --- | --- | --- |
| A (`:1924`) | **0** | `MMC_POWER_UP` = 1 | `if (ios->clock)` (`sdhci.c:1637`) **false** → no `sdhci_set_clock`; then `:1658` `if (ios->power_mode & MMC_POWER_UP)` **true** → `enable_controller_clock` + **`sdhci_set_power(host, ios->vdd)`** (`:1663`) |
| B (`:1937`) | `f_init` = 400000 | `MMC_POWER_ON` = 2 | `sdhci_set_clock(host, 400000)` — **rung 6's act**; `:1658`'s test is `2 & 1` = **false**, so the power call does not run |

Two consequences the ladder is now placed against:

* **The driver powers the bus with the card clock OFF** and only then starts the clock — which is the
  standard's own rule, and which the vendor's comment states (`sdhci.c:1652-1657`: *"we may end up
  enabling card clock before giving power to the card"*).
* **`power_mode & MMC_POWER_UP` is a bit test on an enum** (`MMC_POWER_OFF` 0, `UP` 1, `ON` 2 —
  `host.h:43-45`), so it is true for `UP` and for nothing else. The vendor's power call therefore
  happens on **exactly the pass whose clock is still off** — the gate is not a mistake, it is the
  mechanism.

**Rung 6 was pass B's act; rung 7 is pass A's.** The ladder appends rather than reorders (§3), so the
arm will run them in the order B-then-A while the driver runs A-then-B; the end state is the same
(`CLOCK_CONTROL 0xE045`, `POWER_CONTROL` powered) and §3 publishes the cell that shows the difference.

### 1.2 `SDHCI_QUIRK_SINGLE_POWER_WRITE` is SET, so the bus-off write is not on this path

`sdhci_set_power` (`sdhci.c:1314`), with `SDHCI_QUIRK_SINGLE_POWER_WRITE` in `host->quirks`:

| line | the code | taken? |
| --- | --- | --- |
| `:1336` | `if (host->pwr == pwr) return -1;` | **no** — `host->pwr` is 0 (kzalloc) and `pwr` is nonzero |
| `:1341-1346` | `if (pwr == 0) { writeb(0, POWER_CONTROL); check_power_status(REQ_BUS_OFF); }` | **no** — `pwr != 0` |
| `:1352-1356` | `if (!(quirks & SDHCI_QUIRK_SINGLE_POWER_WRITE)) { writeb(0, …); check_power_status(REQ_BUS_OFF); }` | **NOT TAKEN — the quirk is set** |
| `:1362-1366` | `if (quirks & SDHCI_QUIRK_NO_SIMULT_VDD_AND_POWER) { writeb(pwr, …); check_power_status(REQ_BUS_ON); }` | ignored, and the quirk is not set either |
| `:1368` | `pwr \|= SDHCI_POWER_ON;` | **yes** |
| `:1370` | `sdhci_writeb(host, pwr, SDHCI_POWER_CONTROL);` | **yes — THE store** |
| `:1371-1372` | `if (host->ops->check_power_status) check_power_status(host, REQ_BUS_ON);` | **yes — the wait**, and the ops table sets it (`sdhci-msm.c:2655`) |

**So the power act is ONE store and ONE wait, and the wait is `REQ_BUS_ON` — not `REQ_BUS_OFF`.** 706 §4
and 707 §7 both said the zero write / `REQ_BUS_OFF` pair is on this path subject to the quirk; the quirk
is set on this host, so it is not. **Both documents are corrected in place** (706 §4 and 707 §7 carry a
dated correction note pointing here). This is the same defect class the project keeps meeting —
`sdhci.c:1352`'s own condition is the difference between two readings of "the power act", and the
pre-registration had propagated the wider one.

### 1.3 The wait, and why it cannot be taken — the rung's central boundary

`sdhci_msm_check_power_status` (`sdhci-msm.c:2184`):

```c
if ((req_type & msm_host->curr_pwr_state) || (req_type & msm_host->curr_io_level))
        done = true;
...
if (done) init_completion(&msm_host->pwr_irq_completion);
else      wait_for_completion(&msm_host->pwr_irq_completion);
```

`curr_pwr_state` and `curr_io_level` (`:325-326`) are written **only** by `sdhci_msm_pwr_irq`
(`:2092-2094`), the threaded IRQ handler — and every run of this project has measured the vendor's power
surface as silent: `_reg_pwrctl_mask = 0x0000000f` (the driver's own `INT_MASK` written to
`CORE_PWRCTL_MASK 0xE0` at `:2946`), `_reg_pwrctl_status_after = 0`, `_mode_pwrctl_status = 0`,
`_reg_pwrctl_ctl = 0`. With both cached fields 0, `REQ_BUS_ON` (BIT(1)) is not in either, `done` is
false, and the call **waits on a completion whose only completer in this image is an IRQ that is never
delivered** — the boot would end there, silently, with no panic and no return.

**That is the boundary rung 7 draws, and it is a finding rather than an omission: the driver's power-up
cannot be performed the driver's way in this image.** The rung therefore does the **register act** and
takes the **reading the handler would have taken** — `CORE_PWRCTL_STATUS 0xDC` — instead of the wait.
The status is *read and left latched* (the handler's own acknowledge, a write to
`CORE_PWRCTL_CLEAR 0xE4` and `CORE_PWRCTL_CTL 0xE8`, is **not** taken; 697 already left
`_mode_pwrctl_status` latched and this is the same treatment).

### 1.4 The byte, and its one published input

`sdhci_set_power`'s voltage map (`:1317-1334`) is a `switch (1 << power)` over four groups, with
`BUG()` as the default arm — the driver's own failure mode for a vdd it does not know:

| `1 << vdd` | bits | `pwr` |
| --- | --- | --- |
| `MMC_VDD_165_195` | `0x80` (bit 7) | `SDHCI_POWER_180` = **`0x0A`** |
| `MMC_VDD_29_30` / `MMC_VDD_30_31` | bits 23/24 | `SDHCI_POWER_300` = `0x0C` |
| `MMC_VDD_32_33` / `MMC_VDD_33_34` | bits 20/21 | `SDHCI_POWER_330` = `0x0E` |
| anything else | — | `BUG()` |

and the input is **one register this project has already read**: `host->ocr_avail` is derived in
`sdhci_add_host` from `caps[0] = sdhci_readl(host, SDHCI_CAPABILITIES)` (`sdhci.c:3185-3186`, `:3421-3481`)
— `SDHCI_CAN_VDD_330` `0x01000000`, `SDHCI_CAN_VDD_300` `0x02000000`, `SDHCI_CAN_VDD_180` `0x04000000`
(`sdhci.h:195-197`) — and `mmc_power_up` takes `bit = ffs(host->ocr) - 1` if `host->ocr` else
`fls(host->ocr_avail) - 1` (`core.c:1912-1914`).

**705's census read that register: `xnu_live_storage_mode_capabilities = 0x742dc8b2`.** Decoded against
the driver's own three bits: **VDD_330 clear, VDD_300 clear, VDD_180 set** → `ocr_avail =
MMC_VDD_165_195` (`0x80`) → `fls(0x80) - 1` = **7** → `1 << 7` is the first row → `pwr = 0x0A` →
**the byte the driver would write is `0x0A | SDHCI_POWER_ON` = `0x0B`** — a **1.8 V request**, not the
`0x0F` a 3.3 V-reporting host produces. `host->ocr`'s branch agrees: `mmc_power_off` sets it to
`1 << (fls(ocr_avail) - 1)` = `0x80`, and `ffs(0x80) - 1` is also 7.

**The value is the driver's own, derived from a reading this project published two presses ago, and the
log will carry both ends of the derivation.** A board whose SD slot is fed 2.95 V reporting 1.8 V-only in
`CAPABILITIES` is not this rung's question — the driver does not cross-check the DT (`sdhci-msm.c` has no
`ocr_avail` assignment at all, and the vendor gets its voltages from regulators) — but it is worth
stating in the record that **the byte the standard's own path produces here is 1.8 V**, because the
alternative reading (`0x0F`) is what a reader would expect.

## 2. Pre-registered: what rung 7 is

**Rung 7 = rungs 1–6 unchanged, plus the driver's first power byte** — one 8-bit store to
`POWER_CONTROL 0x29` in `hc_mem` — at the tail of `entry_storage_probe`, after rung 6's clock set,
guarded by the same `g_storage_mode_complete` interlock every rung above 2 uses.

| key | what a reading means, and what the alternative would be |
| --- | --- |
| `_pwr_calls` / `_pwr_done` | the act ran and completed |
| `_pwr_cap` | the derivation's input, re-read in this rung: expected `0x742dc8b2` (705's `_mode_capabilities`, and a re-reading rather than a constant) |
| `_pwr_vdd` | the bit the driver's own rule gives: `fls(ocr_avail) - 1` = **`7`** |
| `_pwr_voltage_bits` | the map's answer: **`0x0A`**. `0x0E` here means the caps' 3.3 V bit was set and this record's reading of it was wrong |
| **`_pwr_wrote`** | **the byte: expected `0x0B`** = `SDHCI_POWER_180 \| SDHCI_POWER_ON`. `0x0F` is the 3.3 V alternative, `0x01` would mean the voltage bits were dropped |
| `_pwr_refused` | **`0` for the act to have happened.** `1` = the caps' three voltage bits produced no map entry, which is the driver's own `BUG()` arm — the arm refuses rather than writing a byte the driver would never write, and the refusal is published |
| `_pwr_before` / `_pwr_after` | `POWER_CONTROL` read 8-bit before and after: expected `0x00` → **`0x0B`**. **`_after` is the readback that says the store took** — the cell 706's `CORE_VENDOR_SPEC` store failed, so a power store whose readback disagrees with the word written is a finding about this register too |
| `_pwr_status_before` / `_pwr_status_after` | `CORE_PWRCTL_STATUS 0xDC` read 32-bit, before and after: expected `0x00000000` → **the controller's own answer**, `CORE_PWRCTL_BUS_ON` (BIT(1)) if it asks for the bus, `BUS_SUCCESS`-shaped bits only after an acknowledge. **This is the reading the handler that cannot run would have taken**, published as the wait's stand-in |
| `_pwr_mask` / `_pwr_ctl` | re-read here: `0x0000000f` (four power events armed — so a status bit is *evidence* and not a masked-off formality) and `0x00000000` |
| `_pwr_ps_before` / `_pwr_ps_after` | `PRESENT_STATE 0x24`: expected `0x01f80000`, whose `CARD_PRESENT` bit (`0x00010000`, `sdhci.h:64`) is **clear** — the controller's own statement that no card is in the slot, made before and after the byte |
| `_pwr_cc_before` | `CLOCK_CONTROL 0x2C` at the moment of the power write: expected **`0xE045`** (rung 6's card-enable readback). **This is the cell that states the route**: the driver's pass A sees `0x0000` here, because it powers with the clock off |
| `_pwr_cc_after` | `CLOCK_CONTROL` re-read after the power byte: the cell that says whether powering does anything to it. `0xE045` unchanged is the expected reading; anything else is a coupling this ladder had not seen |

## 3. The route differs from the driver's, and that is published rather than hidden

The ladder is **additive**: every rung appends to the probe so that the cells earlier rungs already
answered keep reading the same values, which is what makes a rung's inherited block a comparison at all.
Rung 7 therefore runs **after** rung 6's clock set, while the driver runs power *before* clock. The
consequences, named rather than left to a reader:

* **The end state is the driver's**: after pass A and pass B, the driver has `POWER_CONTROL = 0x0B` and
  `CLOCK_CONTROL = 0xE045`, which is exactly what rungs 6 and 7 together leave.
* **The route is not**, and `_pwr_cc_before = 0xE045` is where the log says so — a value the driver's
  pass A can never have. A rung that wanted the driver's route would have to move the clock set *after*
  the power write, which would also move every cell 706's press answered; that is a re-baseline and not
  a rung.
* **Nothing in pass A is lost by appending it**: `enable_controller_clock` (`sdhci-msm.c:2271`) enables
  `pclk` and `host->clk` through the same four CBCR bits rung 6's `sdhci_msm_prepare_clocks` enables,
  and its `atomic_read(&msm_host->controller_clock)` guard is a driver-side cache this fixture does not
  have — the *register* state it would produce is the state 705 and 706 measured (`en=1`, `off=0`).

## 4. What rung 7 will **not** do

* **It does not call the vendor's `check_power_status`.** §1.3: with `curr_pwr_state = curr_io_level = 0`
  it would `wait_for_completion` on a completion only the threaded IRQ completes, and this image
  delivers no IRQ. The register act plus a `CORE_PWRCTL_STATUS` read is what the rung takes instead.
* **It does not acknowledge the power IRQ.** No write to `CORE_PWRCTL_CLEAR 0xE4` and none to
  `CORE_PWRCTL_CTL 0xE8`; the status is read and left latched, exactly as 697 left
  `_mode_pwrctl_status`. No write to `CORE_PWRCTL_MASK 0xE0` either — the mask stays `0x0F`.
* **It writes `0x0B` and never `0x00`.** The bus-off request is *not* on this path (§1.2) and is not
  being added; `POWER_CONTROL` moves from a register the census read to a register this rung writes,
  and the only value it ever writes is the derived byte behind `_pwr_refused`.
* **No regulator, no rail.** Nothing in this image touches a supply; the byte is a request to the
  controller. The physical rail is whatever the bootloader left, which is why `_pwr_status_after` is a
  reading about the controller and not about a card.
* **No level shifter, no `CORE_VENDOR_SPEC` MCLK store** (706's readback that did not hold is still owed
  and is not this rung's subject), and no `_pwr_io_*` VDD-IO act (`sdhci_msm_set_vdd_io_vol` is a
  regulator sequence, not a register write).
* **No command, no sector, no partition table, no mount**, and no driver beyond the fixture. **TWRP-to-storage
  stays withheld**: the OS is not observed entering and staying.

## 5. The build clauses rung 7 needs

1. **the `hc_mem` window becomes `47 44 44 41`** with mnemonics `strb strh strh strb` — the reset, the
   two `CLOCK_CONTROL` halfwords, then the power byte at `41 = 0x29`. **The width clause stays as it is**
   (`0x29` is odd, so a `str`/`strh` at that offset cannot satisfy the pairing rule and refuses), and the
   clause's message must name which offset is which rather than counting.
2. **`POWER_CONTROL 0x29` moves from an asserted-ABSENT offset to an asserted-PRESENT one** — and that
   absence has been the clause that kept §4's boundary a property of the linked image since 701
   (`build_entry.sh:29877-29880`). The rung-6 sentence *"A rung above 6 does not exist yet, so the GCC
   set is asserted exactly and not by bound: a rung-7 edit has to change this sentence too, which is the
   point"* is what this clause change answers, and the sentence is rewritten with the new rung.
3. **the refusal is the point of the change and not a casualty of it**: the clause must still refuse a
   *second* store to `0x29`, any store to `0x29` below rung 7, and any store to a fourth `hc_mem` offset.
   A clause that goes from "one byte at 0x2F" to "four stores in this window" without saying which one is
   which has traded a checked absence for a count — `mi4-one-value-two-definitions` m718.

## 6. Owed by rung 7, and what it does not answer

* **The wait's completer.** `sdhci_msm_pwr_irq` is the only writer of `curr_pwr_state`/`curr_io_level`, so
  the driver's power-up needs the threaded IRQ — i.e. it needs the kernel running its own driver with
  interrupt delivery, which is a much later step than a register act. Rung 7 measures the act and the
  controller's answer; it does not make the driver's wait completable.
* **`CORE_VENDOR_SPEC 0x10C`'s MCLK select still does not hold** (706's `_vendor_after = 0`): a rung that
  writes one field and reads it back at two addresses is still the cheapest next step after this one.
* **The RCG rate write** (the speed-change rung) with `_clk_rcg_update = 0` / `_clk_rcg_cmd = 0` as its
  before-values, and the bounded update poll (`UPDATE_CHECK_MAX_LOOPS 500`).
* **`CLOCK_CONTROL 0xE045`'s power-on coupling**: whether `_pwr_cc_after` moves when the bus is powered
  is the first reading this project will have of the clock and power registers interacting.
* Carried unchanged: the width clause of the store census still never observed firing on a **widened
  device store**; what actually returns a run (8/17/24/27/24/27/25/28 s); the ending's first store still
  faulting; the 691 §5 `entry_note_wfi` readback; `entry_reset.h`'s false IMEM claim; 676 §6 / 677 §6;
  the 684-owed runner clause; `tools/xnu_dt_requirements.py` and the `"master"` value; the two
  peer-lane tripwire repairs; BIT(29) of `_clk_ahb_cbcr` published and unnamed; the seam address pinned in
  two files.
* **Peer lane, by message and never by edit**: the gate's narration of `STAGE90_XNU_STORAGE_PROBE` is now
  **six rungs short** (2–6 are not described in it), and this rung will make it seven.

The pre-registration rung 6 was, and the press that filled it:
`docs/experiments/experiment-706-the-rung-6-pre-registration-the-first-clock-set-and-the-rate-write-that-is-not-on-this-path.md`
and `docs/experiments/experiment-707-the-clock-set-pressed-eight-stores-no-rate-and-the-readback-that-did-not-hold.md`.
