# 717: the rung-10 press — the bound is not the device: the completion arrived 20.17 ms after the byte, and the handler's arrival tracks the end of the masked spin to within 0.73 µs

**Date:** 2026-09-26 00:51:40–00:53:56 UTC. **Arm:** `armed-storage-pwrwait-aa2b051d`
(`STAGE90_XNU_STORAGE_PROBE=9`, `STAGE90_XNU_PWR_WAIT_TICKS=384000` — rung 9's wait at **20 ms**).
**Verdict:** readiness 5/5 exit 0 (00:51:41), **one** gate (`--allow-xnu-entry`, exit 0, 579 stdout
lines, 00:52:47), **exactly one** runner `--expect-arm=armed-storage-pwrwait-aa2b051d` → **EXIT 0** at
00:53:56 (returned and captured). Firer `/tmp/g668/press-on-clear.v5.sh` (sha `7f23cae5…`), budget
600 s, `done. gate=0 runner=0` — **no firer is armed**. Capture 625,029 B, sha256
`98584a316213b735…`, archived by hand as
`out/stage90/captures/rung10-pwrwait20-20260926-005430-{last_kmsg.txt,press,gate,run}`. One
non-persistent `fastboot boot`; nothing flashed; `33e80afe` off the bus throughout; the device came
back on its own (`adb devices` → `4a2fe00b device`).

## 1. The answer, in one table

Both arms run the same site, the same predicate, the same poll; one switch differs, and it is the
bound. Every number below is from the two captures, in ticks of the **19,200,000 Hz** counter the
hardware itself reported (`_post_cntfrq=0x0124f800`).

| quantity | 714's arm (bound 1,920,000 = 100 ms) | **this arm (bound 384,000 = 20 ms)** | difference |
|---|---|---|---|
| `_wait_bound` | 1,920,000 | **384,000** | the one switch |
| `_wait_ticks` | 1,921,194 (100.062 ms) | **386,131 (20.111 ms)** | **−80.000 ms** = the bound's own move |
| `_wait_polls` | 428,032 | **86,016** (84 batches of 1,024) | −4.98× |
| `_wait_timeout` | 1 | **1** | the poll spent its whole bound again |
| `_wait_ctl_before` → `_wait_ctl_after` | `0x00` → `0x00` | **`0x00` → `0x00`** | the controller's ack still not there |
| `_wait_cpsr` | `0x80000093` | **`0x80000093`** | SVC, `I` SET — the premise, re-read |
| `post_t0` − (spin end) | 217 ticks (11.30 µs) | **217 ticks (11.30 µs)** | **identical** |
| `_pwr_irq_at` − (spin end) | 1,153 (60.05 µs) | **1,167 (60.78 µs)** | **+14 ticks = +0.73 µs** |
| `_pwr_irq_at` − `_wait_t0` | 1,922,347 (100.122 ms) | **387,298 (20.172 ms)** | **−80.000 ms** |
| `_pwr_irq_at` − `post_t0` | 936 (48.75 µs) | **950 (49.48 µs)** | **+14 ticks = +0.73 µs** |

**Read the last three rows against the second.** The bound moved by 80 ms and so did two things: the
spin's length, and the handler's distance **from the byte**. The handler's distance **from the end of
the spin** did not move — 60.05 µs against 60.78 µs, a spread of 0.73 µs, which is 0.9 % of the
quantity and four orders of magnitude below the 80 ms that separated the two arms.

## 2. So reading (b) is dead, and it does not even need the coupling argument

714 §3 named two readings and refused to choose:

* **(a)** the IRQ was pending throughout the spin and was delivered when the mask lifted;
* **(b)** the controller really takes about 100 ms to acknowledge, and 100 ms was a hair too short.

**(b) is refuted by one cell, without any argument about what is pinned to what.** `_pwr_irq_at`
minus `_wait_t0` = **20.172 ms**: on this arm the completion the wait was waiting for **arrived inside
20.17 ms of the byte** (`_wait_t0` is taken at the wait's start, immediately after the byte). A
mechanism that takes ~100 ms cannot deliver in 20.17 ms. There is no 100 ms device latency to miss —
the 100 ms 714 measured was **its own mask**, which is exactly the reading 714's own evidence
favoured and could not prove.

**(a) is confirmed quantitatively.** The handler entered 60.78 µs after the poll gave up and 49.48 µs
after the probe's own post record — and on the 100 ms arm the same two offsets are 60.05 µs and
48.75 µs. The unmask is a property of the code **after** the probe, not of the probe:

```
rung 9:  spin end 0x07660cff  post_t0 0x07660dd8 (+217)  irq_at 0x07661180 (+1,153 = +60.05 us)
rung 10: spin end 0x0735475c  post_t0 0x07354835 (+217)  irq_at 0x07354beb (+1,167 = +60.78 us)
```

`post_t0 − spin end` is **217 ticks on both arms**, which is the probe's own tail reaching its post
record and nothing else. The 0.73 µs spread on the other two rows is the noise of that same tail.

**What this buys the driver's port, stated as a requirement rather than as a finding:** the completion
is available essentially immediately after the byte (within 20.17 ms, and the latch is set within
microseconds — rung 7's own pair, re-read on this run as `_pwr_status_before=0x00` →
`_pwr_status_after=0x02`). **A bounded spin in a context that masks interrupts cannot observe it at
any budget**, because the budget and the mask are not two independent quantities — the mask ends where
the code that owns it returns, and the spin is inside that code. The vendor's `wait_for_completion`
works precisely because it **sleeps**; a port that keeps the predicate and drops the sleep loses the
only mechanism by which the completion can arrive. The next rung therefore has exactly two shapes,
and they are not a matter of taste: **unmask around the spin, or take the wait somewhere the mask is
already clear.** Both are now design choices with a measured fact behind them instead of an
assumption — which is what 714 §7 asked for and what 715's pre-registration was written to decide.

## 3. The cells that say this arm changed only what it says it changed

Every inherited cell reads exactly as it did on the pressed rung-9 arm — the arm is one constant away
from an arm that has been to the device, and the run behaves like it: `_storage_calls=1`,
`_loads=6`, `_writes` published 0→4 through the mode sequence, `_rst_stores=1`, `_mode_bit_after=1`,
`_gate=1`, `_bcr=0x0`, `_cbcr=0x4ff1`, `_gcc_map=1`, `_map=1`, `_pwr_before=0x00` → `_pwr_after=0x0b`
with `_pwr_vdd=7`, `_pwr_cc_before=0xE045` → `_pwr_cc_after=0xE047`.

* **711 §3 is answered a second time, on a second arm**: `_wait_cc_before = _wait_cc_after = 0xE047`,
  identical halves either side of the twenty milliseconds. The bit is the machine's state; it is not a
  race, and this is now two arms rather than one.
* **The handler's side is unchanged and correct**: `_pwr_irq_calls=1`, `_pwr_irq_intid=0xaa` (170),
  `_pwr_irq_ctl_before=0x0` → `_pwr_irq_ctl_after=0x1`, `_pwr_irq_status=0x02`, `_pwr_irq_ack=0x01`,
  `_pwr_irq_status_after=0x00` (the latch let go), `_pwr_irq_state=0x02` and `_pwr_irq_io_level=0x08`
  — the two fields the predicate reads, written by the handler's tail. No `_irq_other_*` key at all.
* `_pwr_irq_calls_once_before_byte=0` and `_pwr_irq_calls_probe_end=0` are **expected by construction
  on both arms** and are therefore not evidence about the spin: the handler cannot run between the
  byte and the read that follows it, and the probe's tail is 11.30 µs wide. 710 §2's pre-registration
  predicted them; a cell that cannot be other than 0 while the arm is masked does not predict
  anything about the arm. Recorded as **m723** so the same two names are not read as a finding twice.
* `_wait_done`/`_wait_state_after`/`_wait_io_after`/`_wait_calls` all `0` — the waiter's side of the
  handshake, 20 ms later, still nothing, because the handler had not run.
* The ending did not move: `_post_end_calls=0x8` with `_post_cntfrq=0x0124f800` = 19,200,000 Hz and
  the elapsed pair past 7,997 ms, i.e. the 690 clock fired on its own schedule and the machine stayed
  up through it. **The shorter bound cost the run 20 ms of a 6,000 ms window and nothing else**,
  which is the safety property the rung was designed for, re-measured: the poll is bounded, so the run
  always continues.

## 4. Cost, and what the goal is

**Cost:** one press, 136 s of wall clock (readiness 00:51:41 → runner exit 00:53:56), 20 ms of the
run's ending window, and no store to any device — the write set is still `_rst_stores=1` plus the four
inherited `_writes=4` plus rung 7's power byte.

**Bought:** the separation of 714 §3's two readings, by measurement rather than by preference (reading
(b) refuted outright, reading (a) confirmed to 0.73 µs), and with it the design constraint the port
needs. It also converted a pre-registration into a **matched prediction**: 715 §3 pre-registered
`_wait_ticks ≈ 384,000 + a mid-batch remainder` (measured 386,131, overshoot 2,131 ticks = 111 µs,
inside one 1,024-read batch), `_wait_bound = 0x0005DC00` in the linked artifact (measured in the log),
and `_pwr_irq_at − _wait_t0 ≈ 385,000 ticks` under (a) against `≈ 1,922,000` under (b) (measured
387,298 — the extra ~2,300 ticks are the batch overshoot plus the 1,167-tick wait for the mask).

**THE GOAL IS STILL NOT MET.** No command, no sector, no partition table, no mount, no driver beyond
the fixture: this rung's whole device act is two halfword reads and a poll of one byte, and its log
carries no key that names the medium. **TWRP-to-storage stays withheld.** What this press buys is the
next rung's shape, and it is now a measured choice rather than a guess:

* **the wait taken where the interrupt can be delivered** — the vendor's own semantics, ported
  faithfully — with `_wait_ctl_after` and `_wait_timeout` as the cells that decide whether the
  completion is observed, and the interrupt's own latency from the byte as the new reading;
* **owed on the storage path, carried:** the card's rail; the RCG rate write (705/706 measured
  192 MHz while the driver believes 400 kHz); rung 6's two stores to `hc_mem + 0x10C` with the readback
  cell; the command path (`sdhci_send_command`), where "the card answers" begins;
* **carried unchanged:** what actually returns a run; the ending's first store faulting
  (`0x0fa0065c`); the width clause of the store census never fired on a WIDENED DEVICE store; the 691
  §5 `entry_note_wfi` readback; `entry_reset.h`'s false IMEM claim; 676 §6 / 677 §6; the 684-owed
  runner clause; `tools/xnu_dt_requirements.py` and the `"master"` value; the two peer-lane tripwire
  repairs; BIT(29) of `_clk_ahb_cbcr`; the seam address pinned in two files; and the gate's narration
  of `STAGE90_XNU_STORAGE_PROBE`, still eight rungs short (2 through 9) — peer lane, by message.
