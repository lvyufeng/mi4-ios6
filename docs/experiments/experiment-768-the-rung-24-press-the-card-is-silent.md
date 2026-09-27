# 768: the rung-24 press — the card is silent and the controller knows it; rungs 13–22's reading is refuted in one bit

**ONE PRESS SPENT, WITH THE OPERATOR'S AUTHORIZATION, 2026-09-27 09:05:22–09:06:36 UTC (74 s), EXIT 0,
returned and captured.** One `fastboot boot` only, never flash; neighbour `33e80afe` absent from **both**
`adb devices` and `fastboot devices` at fire time (`4a2fe00b` present as `device`); **exactly one** gate
(`--allow-xnu-entry`, exit 0, **600 lines**) and **exactly one** runner
(`--allow-xnu-entry --expect-arm=armed-storage-94de2ede`), both with the flags readiness printed,
readiness **5 of 5** beforehand. Capture archived by hand (the runner does not archive):
`out/stage90/captures/rung24-cmdline-20260927-090636-last_kmsg.txt`, **631,581 B**, sha256
`053818944ec8009f55e1ebeded90c82b45cdaa24861e2fe6e9d4013d130c506e`, with its `-gate.log` and `-run.log`
companions. **EVERY DEVICE TOUCH COSTS ONE PRESS, AND THIS ONE IS SPENT.**

## 1. The answer, and it is the FIRST ROW of the pre-registered table

766 §2 (and 765 §4) pre-registered four rows. **This press landed row 1, exactly:**

> **`0x00010000` alone** ⇒ **the card did not answer and the controller knew it** — and that one bit refutes
> the whole *this controller runs a command and never sets its status* reading of rungs 13–22.

Out of the capture, the arm's own cells:

| key | value | reads as |
| --- | --- | --- |
| `_nidx_status_any` | **`0x00018000`** | `SDHCI_INT_ERROR` (bit 15, the error summary) **+ `SDHCI_INT_TIMEOUT` (bit 16)** |
| `_nidx_err` | **`0x00010000`** | the same, through the arm's own error word |
| `_nidx_complete` | `0x00000000` | **no `SDHCI_INT_RESPONSE`** — the card never answered |
| `_nidx_timeout` | `0x00000000` | the poll ended on its **break condition**, not on the 1.2 s budget |
| `_nidx_any_polls` | `0x000006fd` = **1789** | the bit arrived at poll 1789 |
| `_nidx_polls` | `0x000006fd` = **1789** | — |
| `_nidx_ticks` | `0x000031e3` = **12,771 ticks** = **665.2 µs** | the whole command, start to verdict |
| `_nidx_word` / `_nidx_word_read` | `0x0000030a` / `0x0000030a` | the block holds CMD3, flags `0x030A` |
| `_nidx_arg_wrote` | `0x00010000` | RCA 1 |
| `_nidx_resp` / `_nidx_resp_read` | `0x40ff8080` / `0x00000001` | **the RESPONSE register was read and never changed** — the word is the boot chain's leftover |
| `_nidx_ena_held` | **`0x000f8001`** | **the five-bit enable held** (`0x8000` is the sticky bit 15) |
| `_nidx_readback` | `0x00008000` | the restore worked |
| `_nidx_sig_enable` | **`0x00000000`** | **never written, as at every rung since 17** |
| `_nidx_clear_after` | `0x00000000` | the W1C clear at the command's start worked |
| `_nidx_inhibit_seen` | `0x00000400` = 1024 | the inhibit held for **all** 1024 samples |
| `_nidx_inhibit_last` | `0x01f80001` | bit 0 **still set** at the window's end |

**`_nidx_complete = 0` with `_nidx_err = SDHCI_INT_TIMEOUT` is the whole finding.** The command was
taken, the block waited, the block armed its own response timeout, the timeout **fired**, and — because
the enable was up — the block **said so**.

**And the timing is the timeout's own signature.** `TIMEOUT_CONTROL 0x2E` read `0x00000000`, which is the
SDHCI field's value 0 → `2^13 = 8192` TMCLK cycles. 8192 / 665.2 µs = **12.31 MHz**, and `12.288 MHz` is
this platform's TMCLK: the prediction is **666.7 µs** against **665.2 µs** measured, 0.2 % apart. **So the
665 µs is not a stall, a budget, or an artefact — it is the controller's own response timeout, and the
register that sets it has now been read.**

## 2. The CMD line moved, and 764 §4's candidate 3 is retired

`_nidx_cmdlow_seen = 0x00000187` = **391 of the first 1024 samples**.

**`PRESENT_STATE 0x24` bit 24 is the CMD line's own level, and it went LOW 391 times.** The 48-bit
command frame at 400 kHz is 120 µs of line activity, and the window's 1024 samples span
12,771 × 1024 / 1789 = **7309 ticks = 380 µs** — so a 48-bit frame is the right order of magnitude for
the count, and it is the block's own drive that produced it.

**So 764 §4's candidate 3 — *the block never actually put anything on the bus* — is retired**, and
candidate 1's neighbourhood (the card and the bus) is what is left. **That is exactly the next act 765 §4
and 766 §2 row 1 named**, and the arm's design made the two questions answerable in one boot because rung
24 strictly contains rung 23's window.

## 3. And the controller's health is now established, which is the larger result

The cell that makes the whole picture coherent is from rung 11, in **this same log**:

| key | value |
| --- | --- |
| `_cmd0_complete` | **`0x00000001`** |
| `_cmd0_status_any` | **`0x00000001`** — `SDHCI_INT_RESPONSE` |
| `_cmd0_any_polls` | `0x00000219` = 537 |
| `_cmd0_inhibit_seen` | `0x00000218` = 536 |
| `_cmd0_ticks` | `0x00001328` = 4904 ticks = **255.4 µs** |

**CMD0 (no response) completes in 255 µs and sets its completion bit.** CMD3 (response demanded) is
issued, drives the line, and times out at 665 µs. `_cmd1_` and `_cid_` (the other response-demanding
words) carry `_inhibit_seen = 0` / `status_any = 0` under the **old one-bit enable** — i.e. they timed out
invisibly, which is what 765 predicted.

**So the controller is not merely alive, it is correct**: it powers the bus, runs the clock, takes
commands, drives the CMD line, completes what has nothing to wait for, and arms and fires its response
timeout on what does. **The rungs-13–22 headline — "this controller runs a command and never sets its
status" — is false, and it was false because the error-enable half of `INT_ENABLE 0x34` was zero.**

**And the masked reading is measured one cell deeper than 765 could.** `_nidx_status_pre` — `INT_STATUS`
read **between the enable store and its readback**, i.e. before this command was ever issued — is
**`0x00018000`**. A timeout summary had been **sitting latched and invisible since an earlier command**,
and it became readable at the instant the enable went up. That is 730's rule, applied across a command
boundary.

**Stated at its true strength, because m739 is exactly this shape.** The pair
(enable store → `status_pre = 0x18000`) has **two producers**: *the latch was already there and became
visible*, or *writing the enable set the summary bits itself*. **It does not separate them** — and it
does not have to, because the poll settles it independently: `_nidx_clear_after = 0` shows the stale
latch was **cleared** at the command's start, and the poll then latched `0x00018000` **again** at poll
1789 with `_nidx_timeout = 0` (a break, not a budget). **The timeout this arm reports is this command's
own.**

## 4. The safety contract held, cell by cell

`_nidx_sig_enable = 0` — `SIGNAL_ENABLE 0x38` was never written, and there is **no `_irq_other_*` key
anywhere in the log**: no delivery reached the dispatcher. rungs 17, 19, 20, 21, 22 and 23 all relied on
that, and it is now measured on the widest enable word any of them has written.

`persistent_write_attempted` is `0x00000000` on **every** occurrence; `failure_mask` is `0x00000000` on
every occurrence; nothing was flashed. The ending is unmoved: `_post_end_calls = 0x6`,
`_seam_sctlr = 0x30c57879`, `_sleh_storm = 9`, and the log's only `panic` is the known
`entry_epilogue` first store at `far_frame = 0x0fa0065c` — the ending every returning run has.

## 5. What the frontier is now, stated exactly

Every register the ladder has measured is consistent with a **healthy controller**:

| the block's own state | its cell | value |
| --- | --- | --- |
| in SDHCI mode | `_mode_bit_after` | `1` |
| powered (1.8 V bus, **correct for this board** — see §6) | `_pwr_after` | `0x0b` |
| clock on, stable, enabled | `_cmd2_clock_control` | `0xe047` |
| DLL reset, powered down | `_dll_rst` / `_dll_pdn` | `1` / `1` |
| a no-response command | `_cmd0_` | **completes, 255 µs, `INT_RESPONSE` set** |
| a response-demanding command | `_nidx_` | **issued, line driven 391/1024, `INT_TIMEOUT` at 665 µs** |
| the card | `_nidx_complete`, `_nidx_resp` | **silent, and the RESPONSE register never changes** |

**The anomaly is no longer about the controller at all. It is: the card does not answer, on a bus the
controller is demonstrably driving.**

The candidate space is 764 §4's, minus candidate 3:

1. **The card is not powered.** 765 §3 showed the vendor does not power it either (the rails are
   always-on and `host->vmmc` is NULL), so *"the ladder skipped a power act"* is refuted — but *whether
   the rails are up at the moment of the press* is still the DT's declaration and not a measurement.
2. **The response timeout is not armed on this IP for these `RESP_TYPE` values.** **RETIRED**: the
   timeout fired, at exactly the time `TIMEOUT_CONTROL = 0` predicts.
3. **The block is not actually issuing onto the bus.** **RETIRED**: the CMD line went LOW 391 times.

**And one candidate that was never on the list, which the pads census makes the most likely of what is
left.** The CMD line is driven and the DAT lines read HIGH (`PRESENT_STATE = 0x01f80000`: bits 19–23 set,
bit 24 set) — **which is what an idle, undriven bus looks like. A card that had answered would have
pulled CMD low *and* the block would have latched the response.** The failure is at the card's pins, and
the two things that live between the controller and the pins are the **function mux** and the **clock**.

## 6. Host-side checks that ride along, so the next press is not spent on them

- **The 1.8 V power byte is CORRECT here, and the obvious suspicion is wrong.** `_pwr_wrote = 0x0b` is
  `SDHCI_POWER_180 | SDHCI_POWER_ON` (`sdhci.h:88-91`), derived by the vendor's own path:
  `_mode_capabilities = 0x742dc8b2` has `SDHCI_CAN_VDD_330` (bit 24) **clear**, `CAN_VDD_300` (bit 25)
  clear and `CAN_VDD_180` (bit 26) set, so `ocr_avail = MMC_VDD_165_195` and `fls(ocr_avail)-1` picks
  1.8 V. **And the board agrees**: `msm8974-mtp.dtsi` gives `&sdhc_1` `qcom,vdd-io-voltage-level =
  <1800000 1800000>` — **VCCQ is 1.8 V on this board**, with VCC = 2.95 V from `pm8941_l20`. So the byte
  is a faithful 1.8 V I/O request and not a wrong voltage.
- **The `qcom,pad-*` settings are not a missing act.** They exist and the vendor applies them
  (`msm8974-mtp.dtsi:437-446`; `sdhci-msm.c:966,1113,1189` → `msm_tlmm_set_hdrive`/`set_pull` into the
  **TLMM block, a second device's megabyte**), but they are **drive strength and pull, not a function
  mux** — and the pull is measurably present: bit 24 reads HIGH at the end of every arm on record.
- **`HOST_CONTROL 0x28`'s bus width cannot matter for these commands**: CMD0/CMD1/CMD2/CMD3 are all
  data-less, and a width setting only affects data transfers.
- **The one register no rung had read is now read**: `_nidx_tout_ctl = 0x00000000`, and §1 shows that 0
  is a working 667 µs timeout rather than a disabled one.

**So the next act is the clock at the card's pins and the card's own power-on state — the two things a
register read of the controller cannot see.**

## 7. What this document does not say

- **It does not say the storage driver is closer.** It says the frontier moved off the controller, which
  is a large step and not a solution: no storage, no filesystem, no mount.
- **It does not name a mechanism for the card's silence.** §5 states the candidate space with two of its
  three members retired and one new one named at the strength of a reading (`PRESENT_STATE`'s idle
  levels).
- **It does not re-open 764 §1.** The DLL answer stands and its path stays closed.
- **It does not claim 730 was wrong.** §3 applies 730's rule and states honestly, in m739's shape, that
  `_nidx_status_pre` alone has two producers — and that the poll settles the fresh latch without it.
- **It does not spend a press on rung 23.** Rung 24 contained it, and rung 23's `armed-storage-3a92aa52`
  is now a **superseded, unspent** park: its question is answered, and its park stays intact.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. What it buys is that **the
  next press has a different subject and a much smaller space**: the controller is exonerated, and what
  is left is the card's power and the clock at its pins.

## 8. New instance — **m776: a field's DIRECTION stated backwards in a live instruction**

`TIMEOUT_CONTROL 0x2E`'s `0x00` was described, in 765 §4 and carried into 766 and into the rung-23 and
rung-24 readiness narrations, as *"the field's **longest** setting rather than the absence of a timeout"*.
**It is the shortest.** The field's value *v* selects `2^(13+v)` TMCLK cycles, so `0x00` is 2^13 ≈ 667 µs
and `0x0E` is the longest; and this press **measured** the 667 µs, because the arm that read the register
is also the arm whose command timed out at 665 µs.

**The sentence was directionally inverted and the inversion was the only thing wrong with it** — its
*point* (do not read `0` as "the block cannot time out") survives intact, and the press proved the point
more strongly than the sentence did. **Shape to suspect first: a claim about a field's DIRECTION (longest
/ shortest, first / last, high / low) written from an intention rather than read from the table.** The
intention here was right — warn the reader off a false inference — and the arithmetic under it was never
performed. **The test: for any "most / least" claim about a register field, write the formula and
evaluate it at both ends.** Related: [[mi4-one-value-two-definitions]] (a value with two readings),
[[mi4-measurement-defects]] (a number that was an artifact), [[mi4-a-claim-in-comment-is-not-a-check]]
(the sentence was in a *refusal* — the most load-bearing place a wrong direction can sit).

**And the correction is OWED where it is most load-bearing.** The sentence is in
`src/entry/entry_storage.c`'s own `#error` text, and that file is one of the entry image's **23 recorded
sources** — so editing it makes the gate refuse the arm this press just spent (763 §5's measured cost:
*"a comment that buys a rebuild of the arm a press is waiting on is not worth it"*). It is corrected in
this document, in `tools/verify_press_ready.sh` (not an entry source, so safe) and here; **the source
text is left as written and named as owed**, to be paid by the next step that rebuilds the entry image.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block issues a
response-demanding command, drives the CMD line, and **times out because the card does not answer** — and
「让os可以正常启动并且挂载存储」 is not reached, so **TWRP-to-storage stays withheld.**
