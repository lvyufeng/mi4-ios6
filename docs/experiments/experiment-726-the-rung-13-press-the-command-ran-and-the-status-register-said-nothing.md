# 726: the rung-13 press — the command RAN, the block acknowledged it, and the controller reported NOTHING in its status register

`armed-storage-instant-e8dc64f7` (`STAGE90_XNU_STORAGE_PROBE=12`) was pressed and **returned**: readiness
5/5 exit 0, **one** gate (exit 0, 574 lines), **exactly one** runner
`--expect-arm=armed-storage-instant-e8dc64f7` → **EXIT 0** (returned and captured). One non-persistent
`fastboot boot`; nothing flashed; `33e80afe` off the bus throughout; the device came back on `adb` on its
own, on no power press. The arm's record is
[725](experiment-725-the-rung-13-arm-built-and-parked-the-seam-moves-a-third-time.md) and the
pre-registration is [724](experiment-724-the-rung-13-pre-registration-the-instant-of-the-command-and-the-return-path.md).

```
[03:30:16] readiness exit=0
[03:30:16] === GATE: preflight_boot_check.sh --allow-xnu-entry ===
[03:30:46] GATE EXIT=0  (574 stdout lines)
[03:32:00] RUNNER EXIT=0  (0 returned+captured / 1 host-side / 2 did not return / 3 a return adb missed)
[03:32:00] done. gate=0 runner=0
```

**No firer is armed**, so 660's closure has released itself. The runner does not archive its capture, so it
was archived by hand: `out/stage90/captures/rung13-instant-20260926-033016-last_kmsg.txt`, 625,475 B,
sha256 `3aacabdc…`.

## 1. The command was TAKEN, STARTED and FINISHED — and this is new

Rung 12 could say only that `COMMAND 0x0E` was written and that `INT_STATUS` stayed zero. 724 §2 named five
hypotheses and said the arm could not tell three of them apart. **It can now, and all five are refuted.**

| cell | value | reading |
|---|---|---|
| `_cmd0_sent` | 1 | the command path ran and the command was issued |
| `_cmd0_word_read` | **`0x0000`** | **the block holds the driver's own CMD0 word** — the store landed where the arm believes, and the register reads back. *No longer an inference: the value was read out of the block.* |
| `_cmd0_inhibit_before` | 0 | the controller was idle before the command |
| `_cmd0_inhibit_after` | **1** | **`SDHCI_CMD_INHIBIT` ROSE on the read immediately after the store** — the block's own acknowledgement that it took the command |
| `_cmd0_inhibit_seen` | **`0x21b` = 539** | **the bit was set in 539 of the poll's first 1024 samples** — the command was in progress on the CMD line |
| `_cmd0_inhibit_last` | `0x01f80000` | bit 0 **clear** at sample 1024: **the inhibit released, so the command finished** (~539 samples ≈ 126 µs) |
| `_cmd0_status_any` | **`0x00000000`** | **not one bit of ANY kind, ever** — over 5,088,256 polls and 23,040,340 ticks (1.200 s) |
| `_cmd0_status_after`, `_cmd0_complete`, `_cmd0_err` | 0, 0, 0 | no `RESPONSE`, no `TIMEOUT`, no CRC, END, INDEX or AUTO_CMD_ERR |
| `_cmd0_resp_read`, `_cmd0_resp` | **1**, 0 | `RESPONSE 0x10` **was read** and holds zero — a reading now, where rung 11's `_cmd0_resp = 0` was the absence of a read |
| `_cmd0_timeout` | 1 | our own 1.2 s bound expired |
| `_cmd_gated` | 1 | CMD1 was refused; **zero `_cmd1_*` keys** |

**So the five hypotheses of 724 §2:**

1. *"The command was never issued"* — **refuted** by `_word_read = 0x0000` (the block holds the word) and
   `_inhibit_after = 1` (the block started it).
2. *"The command ran and the arm's poll read the wrong bit"* — **refuted by construction**: `status_any`
   is the **first non-zero value of the whole register**, so a block that latched *any* bit would have
   been caught no matter which.
3. *"The command is in flight and stuck"* — **refuted** by `_inhibit_last`'s bit 0 being clear: the
   inhibit released and the command did not stay in progress.
4. *"The bus is not powered at the instant"* — **refuted** by the census below.
5. *"The block's clock root is not running"* — **refuted** by the census below.

**What is left is a sixth hypothesis that the previous five did not contain: this controller runs the
command to completion and does NOT set its interrupt-status register.** The command reached the CMD line,
the block held the inhibit while it was in flight, released the inhibit when it finished, and
`INT_STATUS 0x30` never read anything but zero — not the completion, not the block's own timeout, not one
error bit — on 5,088,256 consecutive reads taken *across* the window in which all of that happened.

## 2. The census, and it refutes the two remaining alternatives with values from the command's own moment

| cell | value | reading |
|---|---|---|
| `_cmd2_calls`, `_cmd2_ok`, `_cmd2_refused_power` | 1, **1**, 0 | the census ran and its one refusal was not taken |
| `_cmd2_power_control` | **`0x0b`** | `POWER_CONTROL 0x29` = `SDHCI_POWER_180 \| SDHCI_POWER_ON` |
| `_cmd2_power_bus` | **1** | **the bus-power bit is SET at the instant of the command** — hypothesis 4 dead, and 723 §5's correction confirmed a second time |
| `_cmd2_clock_control` | `0xe047` | `INT_EN` + `INT_STABLE` + **`CARD_EN`** all set |
| `_cmd2_cc_int_en`, `_cmd2_cc_stable`, `_cmd2_cc_card_en` | 1, 1, 1 | **the SD clock is enabled, stable and driving the card** |
| `_cmd2_rcg_cmd`, `_cmd2_rcg_root_en`, `_cmd2_rcg_root_status`, `_cmd2_rcg_update` | 0, 0, **0**, 0 | `CMD_RCGR` is zero: **bit 31 clear means the root is ENABLED** (`clock-local2.c:61`, `:290-299`), and nothing is updating |
| `_cmd2_rcg_cfg`, `_cmd2_rcg_src`, `_cmd2_rcg_div`, `_cmd2_rcg_mnd_mode` | `0x507`, **5**, **7**, 0 | `SRC_SEL = 5`, `DIV = 7` — with the vendor's own pre-divider `(7 + 1) >> 1 = 4` that is **200 MHz**, exactly 724 §1.1(b)'s arithmetic, read off the hardware at the command's own moment |
| `_cmd2_apps_cbcr`, `_cmd2_ahb_cbcr`, `_cmd2_bcr`, `_cmd2_bcr_ares` | `0x4ff1`, `0x2000cff1`, 0, 0 | both branches enabled, no block reset |
| `_cmd2_int_enable`, `_cmd2_sig_enable` | 0, 0 | **the run-protecting gate holds at this instant too** |
| `_cmd2_present_state`, `_cmd2_ps_inhibit` | `0x01f80000`, 0 | the block is idle |

Hypothesis 5 is refuted **at the moment that matters**, not at rung 5's moment: the clock root is enabled,
the branch is on, the card clock is enabled and stable, and the tree is at 200 MHz. 724 §1.1(b)'s two
record defects are now both **measured on hardware** rather than reasoned: `ST_SET_MAX_CLK = 384000000`
names a table this device does not use, and `_clk_set_vendor_after = 0` is a store at a window the driver
never addresses — neither of which is a cause, both of which are corrections to the record.

**And one more thing is measured here for free.** `_cmd2_power_control = 0x0b` **after** the whole command
path and `_pwr_after = 0x0b` at the end of the run: the byte rung 7 wrote is still there at the end of the
boot, so 723 §3's "the ladder never wrote it" is refuted by four independent readings in one log.

## 3. What this points at, and the arm's own gate is now a suspect

**The single most useful fact in this log is that `_cmd2_int_enable` and `_cmd2_sig_enable` are zero — and
that this rung has been *requiring* them to be zero since rung 11.** The reason is sound: the block's
`hc_irq` is SPI 123 → intid 155, a line this image hands to nobody, and a delivery would end the run at the
dispatcher (`entry_irq.c:549-556`, measured on the `pwr_irq` line by 709 before rung 8 owned it). But the
SDHCI specification's normal-interrupt path is `INT_STATUS → INT_ENABLE → SIGNAL_ENABLE → the line`, and a
block implemented as `status bit latches only if its enable is set` would produce **exactly this log**: a
command that runs and finishes, and a status register that stays zero.

**That is now the leading hypothesis, and it is testable without putting the line in play — if the two
enables are separable.** The discriminator is a rung that writes **one** bit of `INT_ENABLE 0x34` (the
`RESPONSE` bit, `0x00000001`) with **`SIGNAL_ENABLE 0x38` left at zero**, reads `INT_STATUS`, and restores
`INT_ENABLE` to zero immediately. If the block ANDs the two enables to raise the line (which is what the
vendor's own `sdhci_enable_irq`/`sdhci_disable_irq` pair and `sdhci.c:295`'s mask compose), the line never
rises, the status bit latches, and the run gets the answer **and** the log. If it does not AND them, the
run ends at the dispatcher as `_irq_other_count = 1` with `_irq_other_iar = 155` — **an ending this image
already reads and survives** (709's press ended exactly that way on intid 170 and the device came back), so
the failure mode is a **diagnosis**, not a lost device.

**Two alternatives are cheaper and should be read first, and both are reads:**
1. **`SLOT_INT_STATUS 0xFC`**, the standard file's *other* status register, which the vendor reads
   (`sdhci.h`, `sdhci-msm.c`) and which this ladder has **never read at all** — a second place the same
   event could be recorded.
2. **`HOST_CONTROL2 0x3C`'s `PRESET_VALUE_ENABLE` and the vendor's `CORE_MCI_DATA_CTRL 0x2C`** — but more
   to the point, **the vendor's own `sdhci_msm_pwr_irq`** (`sdhci-msm.c:1990-2099`) exists because this SoC
   routes **a second, vendor-specific status path** through `core_mem`; the ladder has already seen that
   path answer (`_pwr_irq_status32 = 0x02`, `_wait_ctl_after = 0x01`). A rung that reads `CORE_PWRCTL_STATUS`
   and `SLOT_INT_STATUS` in the same instant would say whether the block reports elsewhere.

**So the next rung has three parts and none of them is a new command**: read the registers the completion
could be hiding in (`SLOT_INT_STATUS 0xFC`, `CORE_PWRCTL_STATUS 0xDC`, `COMMAND 0x0E`, `PRESENT_STATE`),
**write one bit of `INT_ENABLE` with `SIGNAL_ENABLE` at zero and read `INT_STATUS` immediately**, restore
`INT_ENABLE` to zero, and publish the restore's readback so "the gate is back where it was" is a cell and
not a claim. It spends no command, changes no command register, and its worst outcome is a run that ends on
intid 155 with the answer already in the log.

## 4. The inherited rungs, unchanged, and the ending

`_pwr_wait_timeout = 0`, `_pwr_wait_ctl_after = 0x01`, `_pwr_wait_cpsr = 0x80000013`,
`_pwr_wait_cpsr_after = 0x20000093`, `_mode_bit_after = 1`, `_clk_set_cc_after = 0xe045`,
`_pwr_irq_calls_probe_end = 1`, `_pwr_after = 0x0b` — every one identical to the pressed rung-12 arm, so
the rung is contained and the run is comparable cell for cell.

The ending did not move: `_seam_post_end_ticks = 0x06ddd000` (6000 ms), `_post_end_calls = 7`,
`_post_cntfrq = 0x0124f800`, the last `_post_elapsed = 0x07cd6f02`, `_seam_sctlr = 0x30c57879` (C clear),
`_sleh_storm = 9`, `far_frame = 0x0fa0065c`, and the OS's floors intact (pid 1's `open`/`read`/`getpid`/
`exit`/`wait` and two ASTs).

## 5. The goal

The device was never bricked and never hard-hung: one non-persistent `fastboot boot`, nothing flashed, the
run came back on its own. The stage-by-stage rule holds — built, parked, recorded, gated, pressed, and this
document is the press's own record.

**But 「把基础驱动跑起来」 is still unmet and this press does not meet it.** What it does meet is the
question it was built for: **the five hypotheses are all refuted, and the frontier has moved from "does
the controller see a command" to "where does this controller report a completion".** That is a strictly
smaller question than the one rung 12 left, and it is the first time this line can say — with a readback of
the command register and the inhibit bit's own rise and fall — that **a command left the controller and
came back.** **XNU is not 正常加载, the OS has not been entered, and 「如果os已经能进去了的话」 is not
triggered — so TWRP-to-storage stays withheld.**
