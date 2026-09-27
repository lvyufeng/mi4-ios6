# 764: the rung-22 press — the DLL was already reset and powered down, and both of 761 §5's candidates are closed

**ONE PRESS SPENT, WITH THE OPERATOR'S AUTHORIZATION, 2026-09-27 08:03:28–08:04:41 UTC (73 s), EXIT 0,
returned and captured.** One `fastboot boot` only, never flash; neighbour `33e80afe` absent from **both**
`adb devices` and `fastboot devices` at fire time; **exactly one** gate (`--allow-xnu-entry`, exit 0, 590
lines) and **exactly one** runner (`--allow-xnu-entry --expect-arm=armed-storage-c3007c37`), both with the
flags readiness printed, readiness 5 of 5 beforehand. Capture archived by hand (the runner does not
archive): `out/stage90/captures/rung22-dllcensus-20260927-080441-last_kmsg.txt`, **631,439 B**, sha256
`5ee96f58ee9b5082b399fc77f7113b61c2b79ccb64eb5b46860377eeaf882b1d`, with its `-gate.log`, `-run.log` and
`-press.log` companions. **EVERY DEVICE TOUCH COSTS ONE PRESS, AND THIS ONE IS SPENT.**

## 1. The answer, and it is the first branch of the three-way

The arm's own cells, out of the capture:

| key | value | reads as |
| --- | --- | --- |
| `xnu_live_storage_dll_config` | `0x60006400` | the whole word |
| `xnu_live_storage_dll_rst` | **`0x00000001`** | `CORE_DLL_RST` (bit 30) **IS SET** |
| `xnu_live_storage_dll_pdn` | **`0x00000001`** | `CORE_DLL_PDN` (bit 29) **IS SET** |
| `xnu_live_storage_dll_en` | `0x00000000` | `DLL_EN` (bit 16) clear |
| `xnu_live_storage_dll_ckout` | `0x00000000` | `CK_OUT_EN` (bit 18) clear |
| `xnu_live_storage_dll_status` | `0x00000000` | the whole word |
| `xnu_live_storage_dll_lock` | `0x00000000` | `CORE_DLL_LOCK` (bit 7) clear |
| `xnu_live_storage_dll_ctrl2` | `0x00000000` | the whole halfword |
| `xnu_live_storage_dll_ctrl2_uhs` | **`0x00000000`** | `HOST_CONTROL2`'s `UHS` field is **ZERO** |
| `xnu_live_storage_dll_loads` | `1`, `2`, `3` published | the body ran to its end |
| `xnu_live_storage_dll_done` | `0x00000003` | **a complete run of all three reads** |

`0x60006400` decomposes exactly as the pre-registration predicted it might: **bit 30 set, bit 29 set**, and
the whole tail below bit 16 is `0x6400` — with `DLL_EN`, `CDR_EN` and `CK_OUT_EN` all clear. **So the DLL is
ALREADY reset AND already powered down before this image's first instruction**, and the block is not, and
was never, sampling on a live DLL.

**This is the first branch of 756 §6's pre-registered three-way, and it is the branch that ends the path:**

> `_dll_rst = 1` with `_dll_pdn = 1` kills the vendor-write hypothesis outright and closes the search space
> 761 §5 enumerated at two, and **NOTHING further is owed on this path**.

**And the second half closed in the same press.** `_dll_ctrl2_uhs = 0` is 756 §4's second question answered:
rung 4's `SDHCI_RESET_ALL` **did** reach `HOST_CONTROL2`, the spec argument holds, and the vendor's `ctrl_2`
write at `sdhci-msm.c:2597` is **not** live as a missing act.

## 2. What that refutes, stated exactly

756 §3's hypothesis was: *the vendor's bring-up sets `CORE_DLL_RST` and `CORE_DLL_PDN` at 400 kHz and the
ladder never does, so the block may be running with an undischarged DLL* — a candidate explanation for a
stall confined to *receiving* (CMD0 and the no-response CMD3 complete in 0.255–0.30 ms while every
response-demanding word is taken and never completes).

**The register state the vendor's own `sdhci_msm_set_uhs_signaling` would have produced is the register
state that is there.** Whatever put it there — the bootloader, a prior stage, or the reset default — the
ladder is not leaving an undischarged DLL running, and the two writes 761 §5 closed the candidate space at
are now both closed at zero: **there is no "something the vendor disabled and the ladder left on" left to
find on this path.** `_dll_lock = 0` beside `_dll_en = 0` is consistent rather than informative — a DLL
that is reset and powered down cannot lock — and it is published as a register beside its named bit for
exactly that reason.

**This is a negative result and it is the valuable kind**: it cost three reads, it removes a hypothesis
completely rather than narrowing it, and **it says the next press must not be spent on this path.**

## 3. And the rung-21 command reproduced its own stall, cell for cell

The arm was designed as purely additive — rung 21's call site is guarded `>= 20`, so a value-21 image sends
`0x030A` as well — and the second sample is identical to 758's press on every stopping cell:

| cell | this press | 758's press |
| --- | --- | --- |
| `_nidx_word` = `_nidx_word_read` | `0x0000030a` | `0x0000030a` |
| `_nidx_inhibit_seen` | `0x00000400` (all 1024 samples) | `0x00000400` |
| `_nidx_inhibit_after` / `_nidx_inhibit_last` | `1` / `0x01f80001` | `1` / `0x01f80001` |
| `_nidx_complete` | `0` | `0` |
| `_nidx_status_any` over `_nidx_any_polls` | `0` over `0` | `0` over `0` |
| `_nidx_timeout` | `1` | `1` |
| `_nidx_ticks` | `0x015f95a7` (1.20002 s) | `0x015f91be` (1.20002 s) |
| `_nidx_ena_held` / `_nidx_readback` / `_nidx_sig_enable` | `0x8001` / `0x8000` / `0` | the same |
| `_nidx_polls` | `0x004db800` = 5,093,376 | `0x004da000` = 5,087,232 |

**Two presses, two arms, two days, one stall, and the cells agree to the last one.** Every hex above
is read out of the two captures and every decimal is converted from the hex in this step, because a
decimal carried over from an earlier document is a citation and not a reading — this step's first
draft wrote `5,090,304` for `0x004db800` from memory, and the hex says 5,093,376.
 The additive design
did what it was for: the census and the stall are in one log, and neither had to be paid for separately.

**The safety contract held cell by cell.** `_gate = 1`, `_gate_read = 1`, `_gated_out = 0`, `_loads = 6`,
`_writes` published `0`–`4` (rung 21's own set, unchanged — the census added **no store at all**, which the
build clause asserted before the press), `SIGNAL_ENABLE 0x38` never written, nothing flashed. The ending is
unmoved: `_post_end_calls = 0x6`, `_seam_sctlr = 0x30c57879`, `_seam_post_end_ticks = 0x06ddd000`,
`_sleh_storm = 9`.

## 4. What is left, stated exactly — and it has narrowed to one question

Every register the ladder has measured is now consistent with a **correctly configured controller**:

| the block's own state | its cell | value |
| --- | --- | --- |
| in SDHCI mode | `_mode_bit_after` | `1` |
| powered, 3.3 V | `_pwr_after` | `0x0b` |
| clock on, stable, enabled | `_clk_set_cc_after` | `0xe045` (and `0xe047` with `CARD_EN`) |
| DLL reset, powered down, not locked | §1 | `RST\|PDN`, `LOCK = 0` |
| `HOST_CONTROL2` clean | §1 | `0x0000` |
| card-detect quirk, not an empty slot | `_reg_card_present` | `0` with the DT's 8-bit eMMC node |
| a no-response command | `0x0000`, `0x0300` | **completes in ~0.255 ms** |
| a response-demanding command | `0x031A`, `0x030A` | **taken, started, never completes** |

**The anomaly is no longer "something is configured wrong". It is that a controller in a correct state,
given a response-demanding command, holds `CMD_INHIBIT` for a full 1.2 s and sets NO BIT OF `INT_STATUS` AT
ALL — not the completion, and not the driver's own `TIMEOUT`.** A block whose response never arrives should
raise `SDHCI_INT_TIMEOUT`; `_status_any = 0` over 5,093,376 polls says it does not. **That absence is the
question, and it is about receiving specifically.**

Three candidate answers remain, and none has been tested:

1. **The card is not powered.** The eMMC's VCC/VCCQ come from the PMIC, and **no rung in this ladder has
   ever touched the PMIC**. A card with no VCC answers nothing — and a controller waiting on a CMD line
   that is never driven might well hold `CMD_INHIBIT` without arming its own timeout. This is the only
   candidate that explains *both* halves: no response, and no timeout.
2. **The response timeout is not armed on this IP for these `RESP_TYPE` values**, which would make the
   absence of `TIMEOUT` a fact about the controller's programming rather than about the card.
3. **The block is not actually issuing onto the bus** — i.e. `_inhibit_after = 1` is the block holding the
   command in its own queue for a reason the ladder has not read. 726 §1's old reading is the closest this
   project has come, and it was refuted by 730.

**And candidate 1 is the one the ladder has never had a cell for, because the PMIC is a different block.**
Naming it does not make it true, and no rung is designed from this document.

## 5. What this document does not say

- **It does not say the storage driver is closer.** It says one hypothesis is dead. No storage, no
  filesystem, no mount, and the SDHCI storage driver still does not exist.
- **It does not name a mechanism for the stall.** §4 states the remaining candidate space and marks all
  three untested.
- **It does not authorise anything.** `out/` still holds `armed-storage-c3007c37` — **now PRESSED** — and
  its park is intact (11 members, verified). No firer is armed, no press is owed, and the press path is the
  two commands readiness prints, run by hand once each.
- **It does not re-open 756 or 761.** It is the press those two documents pre-registered.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. What it buys is one thing: **the
  next reader does not spend a press on the DLL.** The cost of this press was one press and the arm's
  rebuildability (760 §5, 763).

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block starts a
response-demanding command and never completes one — and 「让os可以正常启动并且挂载存储」 is not reached, so
**TWRP-to-storage stays withheld.**
