# 769: the SDC1 pads are a dedicated-pad register the ladder has never read — 768 §5's other half, and the vendor applies it *only* from the power IRQ

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER.** The live
arm in `out/` is still 768's spent `armed-storage-94de2ede`; rung 23's `armed-storage-3a92aa52` stays a
superseded, unspent park. **Nothing in this document was measured on hardware.** It is the result of
reading the vendor tree, and every claim below carries its `file:line` so it can be checked the same way.

## 1. What this document is for

768 §5 left the frontier at *"the card does not answer, on a bus the controller is demonstrably driving"*
and named the two things between the controller and the pins: **the function mux** and **the clock**.
This document retires one of them by construction, retires a second candidate that looked strong (the
1.8 V signalling bit), retires a third (the mechanism attribution inside 768 §1), finds **a contradiction
inside 768 §3 that the arm's own cells produce**, and lands on **a named, never-attempted vendor act with
a pre-registered expected value** — the register `TLMM + 0x2044`, the SDC1 pads.

## 2. The function mux does not exist for SDC1 — it is a dedicated pad

**This retires the first half of 768 §5's candidate space, and it retires it without a press.**

MSM8974 routes SDC1 and SDC2 through **dedicated pads**, not GPIOs. The distinction is structural and it
is written into the driver:

- `sdhci-msm.c:267-270` — *"`= 1 if controller pins are using gpios` / `= 0 if controller has dedicated
  MSM pads"*, selected at `sdhci-msm.c:1266` by `of_gpio_count(np)`: `sdhc_1` has **no `gpios` property**
  (`msm8974.dtsi:500-518`), so the pad branch is taken.
- `sdhci_msm_setup_pad` (`sdhci-msm.c:964-988`) is the only runtime write, and it calls exactly two
  helpers: `msm_tlmm_set_hdrive` (`:972/975`) and `msm_tlmm_set_pull` (`:981/984`).
- Both land in `MSM_TLMM_BASE` (`msm_iomap.h:83`; physical `0xFD510000`, `msm_iomap-8974.h:34-35`) at the
  register `SDC1_HDRV_PULL_CTL = 0x2044` (`gpio-msm-common.c:37-42`, the `CONFIG_GPIO_MSM_V3` branch that
  this SoC selects) — i.e. **physical `0xFD512044`**.
- **Every field of that register is drive strength or pull**: `tlmm_hdrv_cfgs[]` puts SDC1 CLK/CMD/DATA at
  bit offsets 6/3/0 (3 bits each) and `tlmm_pull_cfgs[]` puts SDC1 CLK/CMD/DATA/RCLK at 13/11/9/15 (2 bits
  each) — `gpio-msm-common.c:59-88`. **There is no function-select field to write.**

The function-select writers that do exist in this tree — `__gpio_tlmm_config` (`gpio-msm-v3.c:205`) and
`__msm_gpiomux_write` (`gpiomux-v2.c:24`) — are applied only to **SDC3/SDC4**, which are GPIO-routed
(`board-8974-gpiomux.c:1304-1313`, `:1386-1400`, `.func = GPIOMUX_FUNC_2`). A case-insensitive grep for
`sdc1` in `board-8974-gpiomux.c` and `board-8974.c` returns nothing. There is no `pinctrl-msm` driver and
no `qcom,pins`/`qcom,function` anywhere in this kernel.

**So: the SDC1 pads' FUNCTION is fixed in silicon and was set by whatever ran before Linux.**
`_nidx_cmdlow_seen = 0x187` is therefore not evidence about a mux, and 768 §5's *"the function mux"* is
**closed** — not by a reading, by a register layout that has no field for it.

## 3. And the register is a never-attempted vendor act, applied *only* from the power IRQ

The pads exist as a register the ladder has never written **or read**. And the vendor applies them at a
point the ladder has never been:

```
sdhci-msm.c:2015-2032   /* Handle BUS ON/OFF */
    if (irq_status & CORE_PWRCTL_BUS_ON) {
        ret = sdhci_msm_setup_vreg(msm_host->pdata, true, false);   /* the PMIC rails */
        if (!ret) {
            ret = sdhci_msm_setup_pins(msm_host->pdata, true);      /* <-- the pads, HERE */
            ret |= sdhci_msm_set_vdd_io_vol(msm_host->pdata, VDD_IO_HIGH, 0);
        }
```

**The pads' drive and pull are written from inside `sdhci_msm_pwr_irq`, in the `CORE_PWRCTL_BUS_ON`
branch — the ISR that fires when the driver writes `SDHCI_POWER_ON`.** Nothing else in the probe →
first-command window calls `sdhci_msm_setup_pins`. That matters because rung 7 wrote the power byte and
rung 8 registered the handler, but **the ladder's own handler was built to ack the latch, not to perform
the vendor's BUS_ON work** — so this act has never been attempted in any rung.

## 4. The pre-registered expected value, derived from the board's own DT

The values come from the DT, positionally indexed into the SDC1 fields (`gpio.h:161-193` puts
`TLMM_HDRV_SDC1_CLK/CMD/DATA` at 9/10/11 and `TLMM_PULL_SDC1_CLK/CMD/DATA/RCLK` at 9/10/11/12), and
applied by `msm_tlmm_set_field` (`gpio-msm-common.c:481-496`):

```
reg_val &= ~(mask << config->off);
reg_val |= (val & mask) << config->off;      /* hdrive width 3, pull width 2 */
```

The Mi 4's DT family is `msm8974pro*mtp*.dts` (`AndroidKernel.mk:23`); for
`msm8974pro-ac-pm8941-mtp-v5.dts` (`qcom,board-id = <8 0x500>`), `&sdhc_1` at **`:25-29`** declares

```dts
qcom,pad-pull-on = <0x0 0x3 0x3 0x1>;  /* no-pull, pull-up, pull-up, pull-down */
qcom,pad-drv-on  = <0x4 0x4 0x4>;      /* 10mA, 10mA, 10mA */
```

which evaluates field by field to:

| field | reg bits | DT value | contribution |
| --- | --- | --- | --- |
| HDRV SDC1_DATA | [2:0] | `0x4` | `0x004` |
| HDRV SDC1_CMD | [5:3] | `0x4` | `0x020` |
| HDRV SDC1_CLK | [8:6] | `0x4` | `0x100` |
| PULL SDC1_DATA | [10:9] | `0x3` | `0x600` |
| PULL SDC1_CMD | [12:11] | `0x3` | `0x1800` |
| PULL SDC1_CLK | [14:13] | `0x0` | `0x0000` |
| PULL SDC1_RCLK | [16:15] | `0x1` | `0x8000` |
| **`0xFD512044`** | | | **`0x00009F24`** |

**And the field that is the mechanism is `PULL SDC1_CMD = pull-up`.** MMC's CMD line is **open-drain
during identification**: the host and the card both pull it LOW, and it returns HIGH only through the
**pull-up** — which on this SoC is this pad register, not a discrete resistor the ladder can assume. A
board where the pads were left at their reset value reads `0x00000000`: no pull-up on CMD, no pull-up on
DATA, and drive strength at its minimum. That is a mechanism that would let the block drive a command and
never see a well-formed response.

**This is stated as a candidate and not as the cause.** It has a competing reading, named here: the CMD
line reads HIGH on the idle samples of every arm on record (`_nidx_inhibit_last = 0x01f80001`, bit 24
set), which is *also* what a pull-up would produce — but it is equally what a floating line that happens
to sit high looks like, and the cell cannot tell the two apart. **Only the register itself can.**

## 5. Three candidates this document retires, and one contradiction it finds in 768

**5.1 `HOST_CONTROL2 0x3E` bit 3 `VDD_180` = 0 is NOT a defect.** rung 22's `_dll_ctrl2 = 0` looked like
a 3.3 V/1.8 V signalling mismatch (`SDHCI_CTRL_VDD_180 0x0008`, `sdhci.h:170`). It is not:
`sdhci_msm_set_uhs_signaling` **read-modify-writes `HOST_CONTROL2` changing only `UHS_MASK`**
(`sdhci-msm.c:2544-2546`, `:2597`) — it never touches bit 3 — and the **only** writer of `VDD_180` is
`sdhci_do_start_signal_voltage_switch` (`sdhci.c:2015`), which the mmc core calls when *switching
timing*, not at 400 kHz init. **The vendor kernel's own `HOST_CONTROL2` is 0 at the first command too.**

**5.2 768 §1's mechanism attribution is a coincidence read as causation.** 768 §1 says the 665.2 µs is
`TIMEOUT_CONTROL 0x2E`'s `0x00` *"and the register that sets it has now been read"*. But
`SDHCI_INT_TIMEOUT` (`sdhci.h:130`, `0x00010000`) is the **command**-timeout bit while
`SDHCI_INT_DATA_TIMEOUT` (`:134`, `0x00100000`) is the data one — and **`sdhci.c` writes
`TIMEOUT_CONTROL` exactly twice**: the debug dump at `:115` and `:828`, inside
`sdhci_prepare_data`'s `if (data || (cmd->flags & MMC_RSP_BUSY))` at `:826`. **Not one of this ladder's
commands carries either condition**, so the register is at its **reset** value, set by nobody. The
arithmetic holds and is a real reading (`2^13 / 12.288 MHz = 666.7 µs` vs `665.2 µs` measured, 0.2 %
apart — and it is a *positive* measurement that TMCLK is running), but **"the field was unset" and "the
field was set to its longest" are indistinguishable when the field is `0`**, which is exactly what §1's
own `2^(13+v)` formula says. The word carries a reading; the attribution does not.

**5.3 The `RESPONSE` register DID move during CMD3 — 768 §3 says it did not.** 768 §3's table reads
`_nidx_resp` as *"the RESPONSE register was read and never changed — the word is the boot chain's
leftover"*. The same capture's own freshness instrument says otherwise:

| cell | value |
| --- | --- |
| `_nidx_resp_pre` | **`0x00000000`** |
| `_nidx_resp` | **`0x40ff8080`** |
| `_nidx_resp_post` | `0x40ff8080` |
| `_nidx_resp_moved` | **`0x00000001`** |
| `_nidx_raw_pre0/1` (`+12`/`+8`) | `0x0040ff80` / `0x80000000` |
| `_nidx_raw_post0/1` (`+12`/`+8`) | `0x00000000` / `0x00000000` |

All three of `pre`, `c3.resp` and `post` are **the same address** — `ST_SDHCI_RESPONSE 0x10` (`:2003`;
filled at `:2548` for `c3.resp`, `:3583`/`:3696` for `pre`/`post`) — so `0 → 0x40ff8080` is a reading
about one register, not three. **And it is a value *permutation*, not a fresh word**: `0x40ff8080` is
exactly what the vendor's own 136-bit assembly produces from the **pre** words —
`resp[0] = readl(+12) << 8 | readb(+11)` = `0x0040ff80 << 8 | 0x80` (`sdhci.c:1163-1172`) — while `+12`
and `+8` went to zero.

**Two producers, and they are not separated here:** *(a)* the block latched a real response into `+0`, or
*(b)* the block rotated a stale 136-bit field down by one word. **This is m739's shape and it is stated
at that strength.** §7 resolves it — **host-side, with no press** — and the answer is that the movement
is the block's own and cannot be read as the card's.

**5.4 A claim from the census that is WRONG, recorded so it is not carried.** A reading of the vendor
bring-up pass produced *"the vendor writes `0x29 = 0x0E | 0x01 = 0x0F` — VDD 3.3 V"*. It is false, and
`sdhci.c`'s own head refutes it: `sdhci_set_power` switches on `1 << power` where `power = ios->vdd` is a
**bit index** (`sdhci.c:1312-1318`), `ocr_avail = MMC_VDD_165_195` because `CAPABILITIES 0x742dc8b2` has
`CAN_VDD_330` and `CAN_VDD_300` clear and `CAN_VDD_180` set (`sdhci.h:195-197`; `sdhci.c:3434/3447/3460`),
and `fls(ocr_avail) - 1 = 7` → `pwr = SDHCI_POWER_180 = 0x0A` → **`0x0B`**, which is what the ladder
writes. **The wrong claim came from reading a storage assignment (`sdhci.c:1339` `host->pwr = pwr`)
without evaluating the switch above it** — so it is m737's shape again: a claim about a *register* that
is really a claim about a *moment*, and it was nearly carried into a rung.

## 6. The rung-25 design, pre-registered before it is built

**One new read, one new megabyte, one new register, ZERO stores to anything.**

- **Install first, then read.** `entry_mmio_section` for the `0xFD5` megabyte, published as
  `_pad_map` — 693's design, and 692's measurement is why: an uninstalled megabyte faults (`fsr = 0x5`)
  and the install has to come *before* the dereference. `0xFD512044 >> 20 = 0xFD5`, and no other device
  in this image lives there, so the `addr >> 20` interlock is satisfied by construction.
- **One 32-bit read of `0xFD512044`**, published raw as `_pad_raw` beside seven field decodes
  (`_pad_clk_drv/cmd_drv/data_drv`, `_pad_clk_pull/cmd_pull/data_pull/rclk_pull`) so no reader has to
  take the arithmetic on trust.
- **A read of a pad-control register is safe**: `msm_tlmm_set_field` itself opens with
  `__raw_readl(reg)` on exactly this address (`gpio-msm-common.c:487`), it is not W1C and not a FIFO.
- **No store anywhere, and no new window in `hc_mem` or `core_mem`.** The arm's safety reading is
  `_pad_writes = 0`, published as a count that never leaves zero.

**The two-row answer, pre-registered:**

| `_pad_raw` | reads as | the next act |
| --- | --- | --- |
| **`0x00009F24`** (all seven fields as the DT declares) | **the pads are configured** and 768 §5's other half is retired | the subject moves to the PMIC rails (`sdhci-msm.c:2019` `sdhci_msm_setup_vreg`, a bus the ladder cannot reach from `hc_mem`) and the arm has bought that at the cost of one read |
| **`0x00000000`**, or any field at odds with §4 | **the vendor act was never applied on this path** — no pull-up on CMD, no pull-up on DATA | the next rung writes the seven fields, and the write is a single read-modify-write of one register in a megabyte whose section is already installed |

**One caveat, stated because the cell cannot carry it:** the read is taken at the arm's own moment, which
is *before* the command path. If it reads the DT's values there, that is a reading about the pads and
**not** a proof that they were applied by this image — the boot chain may have left exactly the same
values. The row is therefore about **the pads' state**, and the second row is the one that carries an
act.

**And the arm's second question is already answered, at no cost — see §7.** 768 §3's contradiction is
settled **host-side** by rung 20's archived capture: a command with `MMC_RSP_NONE` moves the `RESPONSE`
register too, so **`RESPONSE` is retired as a cell about the card** and rung 25 does not have to carry
it. What the rung carries is the pad read and nothing else.

## 7. The host-side check this document owed, and it was RUN — it settles §5.3

Read `out/stage90/captures/rung20-nrsp-20260927-012235-last_kmsg.txt` (archived, 630,462 B) — the rung-20
arm, whose command is CMD3 with **`_nrsp_flags = 0x00000000`**, i.e. `MMC_RSP_NONE` (`entry_storage.c:2104`),
a command that **begs for no response at all**:

| cell | value |
| --- | --- |
| `_nrsp_flags` | **`0x00000000`** — `MMC_RSP_NONE` |
| `_nrsp_resp_pre` | **`0x40ff8080`** |
| `_nrsp_resp` | `0x40ff8080` |
| `_nrsp_resp_post` | **`0x00000000`** |
| `_nrsp_resp_moved` | **`0x00000001`** |

**A command that demanded nothing moved the `RESPONSE` register.** So the movement is **a property of the
block's own field and not evidence of a response**, and 768 §3's *"the RESPONSE register was read and
never changed"* is refuted in **both** directions at once: the register *does* change, and the change
*isn't* the card.

**What the three arms together say, which no one of them says alone:**

| arm | command | `RESPONSE + 0` before | after |
| --- | --- | --- | --- |
| rung 19 (742) | CMD3, `R1` | `0x40ff8080` | **`0x00000000`** |
| rung 20 (746) | CMD3, `NONE` | `0x40ff8080` | **`0x00000000`** |
| rung 24 (768) | CMD3, `R1`, **after CMD0/1/2** | `0x00000000` | **`0x40ff8080`** |

**The direction of the change depends on what the field held before the command, not on what the command
asked for.** So `RESPONSE` is **not readable as evidence about the card in either direction** on this
ladder — a non-zero post-value is not a response (rung 20 proves it), and a zeroed post-value is not the
absence of one. **`RESPONSE` is retired as a cell about the card**, and the mechanism — a stale 136-bit
field the block rewrites per command — is named as a property of the block with the specific rotation
left **open**: `0x0040ff80` at `+12` becoming `0x40ff8080` at `+0` is an 8-bit movement while `+12` and
`+8` went to zero, and that is **not** a clean 32-bit word shift, so no rotation rule is claimed here.
**What the check buys is a negative, and it is the useful kind**: the CMD3 response cells cannot decide
the card's silence, so §6's rung-25 read is the frontier and not a detour.

## 8. What this document does not say

- **It does not say the pads are the cause.** §4 names a mechanism and §6 pre-registers the reading that
  settles it. A pad register that already holds `0x9F24` retires the candidate.
- **It does not re-open 764 §1 or 768 §4.** The DLL path stays closed and the safety contract is unmoved.
- **It does not claim the card is dead.** The boot chain read this eMMC to get to `fastboot`, and
  `qcom,vdd-always-on` / `qcom,vdd-io-always-on` are declared for `&sdhc_1`
  (`msm8974-mtp.dtsi:385-395`, `vdd-supply = <&pm8941_l20>`, `vdd-io-supply = <&pm8941_s3>`).
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount, no driver.
- **It does not spend a press and it does not authorize one.** Building an arm is host-side; **spending
  it is the operator's decision.**

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist, and 「让os可以正常启动并且挂载存储」
is not reached, so **TWRP-to-storage stays withheld.**
