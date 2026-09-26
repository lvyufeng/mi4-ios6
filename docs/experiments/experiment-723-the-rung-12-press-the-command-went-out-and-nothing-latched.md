# 723: the rung-12 press — the command went out, the controller latched NOTHING for 1.2 s, and the gate between the two commands refused

`armed-storage-cmd-892b8b68` (`STAGE90_XNU_STORAGE_PROBE=11`) was pressed and **returned**: readiness 5/5
exit 0, **one** gate (exit 0, 581 lines), **exactly one** runner `--expect-arm=armed-storage-cmd-892b8b68`
→ **EXIT 0** (returned and captured). One non-persistent `fastboot boot`; nothing flashed; `33e80afe` off
the bus throughout; the device came back on `adb` on its own, on no power press. The arm's own record is
[722](experiment-722-the-rung-12-arm-built-and-parked-two-clauses-the-build-refused.md) and the
pre-registration is
[721](experiment-721-the-rung-12-pre-registration-the-drivers-first-command-and-the-cards-first-answer.md).

The press, as the firer recorded it:

```
[02:04:54] readiness exit=0
[02:04:54] the arm survived the wait and is still 'armed-storage-cmd-892b8b68'
[02:05:25] GATE EXIT=0  (581 stdout lines)
[02:06:36] RUNNER EXIT=0  (0 returned+captured / 1 host-side / 2 did not return / 3 a return adb missed)
[02:06:36] done. gate=0 runner=0
```

The firer is `/tmp/g668/press-on-clear.v5.sh` sha `7f23cae5…`, budget 600 s, `done. gate=0 runner=0` →
**no firer is armed**, so 660's closure has released itself again. The runner does not archive its
capture, so it was archived by hand: `out/stage90/captures/rung12-cmd-20260926-020454-{last_kmsg.txt,
gate.log, run.log, firer.log, press.log}`, the capture 632,182 B, sha256 `e86501c8…`.

## 1. The answer, cell by cell

The thirty-one `xnu_live_storage_cmd*` records are the whole of this rung. In the order the pre-registration's
§5 names them:

| cell | value | reading |
|---|---|---|
| `_cmd_calls` | 1 | the command path ran, once |
| `_cmd_int_enable`, `_cmd_sig_enable` | 0, 0 | **GATE 1 passed**: neither interrupt-enable register is set, so the block cannot raise SPI 123 → intid 155 — the line that would reach the dispatcher as `_irq_other_count` and END the run |
| `_cmd_enabled_out`, `_cmd_refused` | 0, 0 | the refusal path was not taken; `_cmd_sent` is 1 and not 0 |
| `_cmd_ps_before`, `_cmd_ps_after` | 0x01f80000, 0x01f80000 | `PRESENT_STATE` unchanged across the whole act; both inhibit bits clear |
| `_cmd0_op`, `_cmd0_arg`, `_cmd0_word` | 0, 0, **0x0000** | `MMC_GO_IDLE_STATE`, argument 0, `MMC_RSP_NONE` — the driver's own first command word |
| `_cmd0_inhibit_before` | 0 | **GATE 2 passed** on its first read: the controller was not holding a command |
| `_cmd0_inhibit_polls`, `_cmd0_inhibit_ticks` | 1, 9 | the inhibit gate cost one read (9 ticks, 0.47 µs) |
| `_cmd0_stale`, `_cmd0_clear_wrote`, `_cmd0_clear_after` | 0, 0, 0 | **GATE 3 passed**: the latch was read, written straight back and read again, and no command bit was set before CMD0 was issued |
| `_cmd0_sent` | 1 | the command was issued — `ARGUMENT` and `COMMAND` were written |
| `_cmd0_polls`, `_cmd0_ticks` | 5,140,480; 23,040,104 | the completion poll ran its whole budget (23,040,000 ticks = 1.200 s; 4.5 ticks ≈ 234 ns per read) |
| `_cmd0_status_after` | **0x00000000** | **`INT_STATUS` read back zero on every poll** |
| `_cmd0_complete`, `_cmd0_err` | 0, 0 | no RESPONSE bit, and no error bit of any kind — not `TIMEOUT` (0x00010000), not CRC, END, INDEX or AUTO_CMD_ERR |
| `_cmd0_timeout` | 1 | **our** bound expired; the controller's own timeout never fired |
| `_cmd0_rsp_present`, `_cmd0_resp` | 0, 0 | CMD0 asked for no response, so `RESPONSE 0x10` was not read — the control cell the pre-registration named |
| `_cmd_gated`, `_cmd_sent`, `_cmd_done` | **1**, 1, 0 | the gate between the commands refused |
| `_cmd1_*` | **absent** | CMD1 was never issued: there are 31 `_cmd` records and **zero** `cmd1` keys in the log |

So the arm's act is complete and its answer is exact: **with the interrupt-enable gate closed, the inhibit
gate clear and the latch cleared, `SDHCI_COMMAND 0x0E` was written with the word `0x0000` — and for 1.2
seconds the controller set no bit in its interrupt status at all.** Not the completion bit, and not its
own ~1 s command-timeout bit either. `_cmd0_err = 0` with `_cmd0_status_after = 0` is a stronger and
weaker statement than a timeout: nothing in the block's command state machine ran to any conclusion.

## 2. This is a seventh outcome, and it is *weaker* than the one §6 pre-registered for it

§6 named six outcomes. This is none of them:

- **Not 1** (`_cmd1_complete = 1`, the card answered): CMD1 was never issued.
- **Not 2** (`_cmd1_err = 0x00010000`): the pre-registration's own outcome for a card that does not
  answer is **the controller's** `SDHCI_INT_TIMEOUT` bit — "the command went out and the card did not
  answer", which it called the first statement this line has ever had about the card. What happened is
  weaker: **`_cmd0_err = 0`, no bit at all.** The command did not merely go unanswered; the block never
  reported it as having been attempted.
- **Not 3** (`_cmd0_err` non-zero): there is no error to decode.
- **Not 4** (`_cmd_enabled_out = 1`): the enables read 0, and that gate — the one that protects the run —
  is the good news in this log.
- **Not 5** (`_cmd0_inhibit_timeout = 1` or `_cmdN_clear_after != 0`): both gates passed on their first
  read.
- **Not 6** (no return): the run came back, `EXIT 0`, in 102 s.

**And the gate between the two commands is why this run answers only about CMD0.** §6's outcomes 1 and 2
were both written as if CMD1 would be issued; the arm refuses it unless CMD0 completed with no error
(`_cmd_gated = 1`), on the grounds that a second command on a bus whose first is still open is the one act
this rung must not take. That was the right contract and it cost the press one command's worth of
information — a fact about the *pre-registration*, whose outcome table should have had a cell for "CMD0
issued, CMD1 refused".

**The inherited rungs are unchanged and still green**: `_pwr_wait_timeout = 0` with `_wait_ctl_after =
0x01` (the controller's `BUS_SUCCESS`, 1,415 ticks = 73.7 µs — the rung-10 result holds with a command in
the arm), `_mode_bit_after = 1` (`HC_MODE_EN` set by the vendor's sequence), `_rst_wrote = 1` /
`_rst_cleared = 1`, and the clock: `_clk_set_cc_after = 0xe045` with `ST_SDHCI_CLOCK_CARD_EN` set and
`_clk_set_cc_stable = 1`, `_clk_set_rate_writes = 0` (the RCG rate write is still not performed) and
`_clk_set_vendor_after = 0` against `_clk_set_vendor_w1 = 0x00000200` — the vendor's clock-enable store
still does not take, exactly as rung 6 measured.

The ending did not move: `_seam_post_end_ticks = 0x06ddd000` (115,200,000 ticks = 6000 ms),
`_post_end_calls = 7`, `_post_t0` / `_post_elapsed` present with the last elapsed `0x07ca629c`, the seam
pair `a1 = b1 = 0x8047eb04` with `_seam_sctlr = 0x30c57879` (`C` clear, so both reads are DRAM readings —
the 686 correction holds), and the OS's floors intact (`sleh_storm = 9`, `far_frame = 0x0fa0065c`, pid 1's
`open`/`read`/`getpid`/`exit`/`wait` and two ASTs).

## 3. What the answer points at, and the alternative §1 did not consider

The one register that separates this arm from every arm under it is in the log and is not new: **the
standard block's own `POWER_CONTROL 0x29` reads 0x00** (`_reg_power_control`,
`_reg_power_control_after`), and it read 0x00 on the rung-4 reference arm too. The SDHCI specification's
own power-on sequence (2.0, §3.2) sets **SD Bus Power** before the SD clock, and a block whose BUS_POWER
bit is clear is not required to drive SDCLK to the card. This ladder has never written that byte, and it
has been kept out of the linked image on purpose: `build_entry.sh` asserts that `hc_mem`'s write set is
unchanged at `47 44 44 41`, which is what makes a store to `POWER_CONTROL` — where writing **0** is a
bus-off request on this SoC (531 §8) — unreachable by construction.

**The driver writes it, and it is the one part of its start-up this line has never ported.** `sdhci_set_power`
(`sdhci.c:1314-1373`) writes `SDHCI_POWER_ON | SDHCI_POWER_330` = **0x0F** (`sdhci.h:88-91`) with
`SDHCI_QUIRK_SINGLE_POWER_WRITE` set for this variant (`sdhci-msm.c:2897` — so a single write and no
interim 0), and then calls `check_power_status(host, REQ_BUS_ON)` (`sdhci.c:1365`, `:1372`) — **the wait
rung 9 and rung 10 already ported and measured**, whose `BUS_SUCCESS` this run still reports.

**And this is a gap in my own pre-registration.** §1 enumerated three alternatives and talked them out of
the rung: the card's rail (refuted because rung 11 measured `BUS_SUCCESS` + `IO_HIGH`), the RCG rate write
(2× off and 200 kHz is legal), and rung 6's `0x10C` stores (measured as not taking). All three are about
the **vendor's** registers. §1 never considered the **standard** register file's own power byte, which is a
different register in a different window — and it is the one the driver writes immediately before the
first command. The measurement has now pointed at it, which is the direction that should have been read
out of `sdhci_set_power` in the first place.

**So the rung above this one has two halves and the second is the one that makes it a reading rather than
an attempt:**

1. **The driver's own power byte**: one store, `0x0F` at `hc_mem + 0x29`, gated on reading 0x00 first, with
   531 §8's hazard named (the *off* value is the dangerous one; this is the driver's *on* value) and with
   `st_pwr_wait` (rung 9's bound) as the vendor's own BUS_ON handshake — which rung 10 measured the arrival
   of inside the cache-off window.
2. **The discriminator that says whether the controller ever started a command at all**: sample
   `PRESENT_STATE`'s `SDHCI_CMD_INHIBIT` (bit 0) **immediately after** the `COMMAND` write and again during
   the poll, and read `COMMAND 0x0E` back. If `CMD_INHIBIT` never rises, the block never started the
   command; if it rises and stays, the command is in flight and the block is waiting for something the
   driver's start-up has not yet given it. That cell is what separates "the clock never reached the card"
   from "the command was never issued", and without it the next arm would be another attempt rather than a
   measurement.

## 4. The goal

The device was never bricked and never hard-hung: one non-persistent `fastboot boot`, nothing flashed, the
run came back on its own. The stage-by-stage rule holds — built, parked, recorded, gated, pressed, and
this document is the press's own record.

**But 「把基础驱动跑起来」 is still unmet and this press does not meet it.** The command path now exists on
hardware and its gates hold, which is real progress: the line has, for the first time, put a command
register on the bus on purpose. But nothing answered, and the run's own honest summary is that it does not
know yet whether the command left the controller. **XNU is not 正常加载, the OS has not been entered, and
「如果os已经能进去了的话」 is not triggered — so TWRP-to-storage stays withheld.** What this press bought
is that every rung above it is now about *why a transaction does not complete*, with the three gates
proved shut and the ending proved untouched.
