# 715: the rung-10 pre-registration — the bound is the experiment: does the handler's arrival track the byte or the end of the masked spin?

**Date:** 2026-09-26. **Arm to build:** `STAGE90_XNU_STORAGE_PROBE=9` with
**`STAGE90_XNU_PWR_WAIT_TICKS=384000`** (20 ms) — *one switch differs from the arm just pressed*, and
nothing else. **Purpose:** convert rung 9's inference into a measurement, because the answer decides
what the *next* rung can be.

## 1. What rung 9 measured, and what it could not

The rung-9 press (`armed-storage-pwrwait-8b824cac`, EXIT 0) spent its whole 100 ms bound and the
controller never carried `BUS_SUCCESS`; the cell that explains it is `_wait_cpsr=0x80000093` — the
waiter runs in SVC with **IRQs masked** — and the handler that *writes* that bit was delivered
**1,153 ticks = 60.05 µs after the poll gave up**, i.e. as soon as it could be. Full record:
experiment-714.

Two readings survive that run, and experiment-714 names them rather than choosing:

* **(a) The IRQ was pending throughout the spin and was delivered when the mask lifted.** The
  controller acknowledged early; rung 9 measured its own masked window and not the device.
* **(b) The controller really takes about 100 ms to acknowledge**, and the bound was a hair short.

**The evidence in hand leans to (a), and it is worth stating explicitly because it is evidence the
next press will either confirm or kill.** Rung 7's own pair around the byte reads
`CORE_PWRCTL_STATUS` microseconds after the store and finds it **already latched**
(`_pwr_status_before=0x00` → `_pwr_status_after=0x02` = `BUS_ON`) — so the event the line carries was
latched essentially immediately, and the handler was runnable from then on. And this image's
handler-side triple agrees: `_pwr_irq_calls_once_before_byte=0`, `_pwr_irq_calls_probe_end=0`,
`_pwr_irq_calls=1` — it ran exactly once, after everything, when the mask came up.

**But (a) and (b) cannot be separated by one run, because rung 9 set its bound and its mask to the
same length (100 ms) by construction.** The two numbers agree to within 60 µs because the experiment
made them agree. That is the defect this rung removes.

## 2. The rung, and the one variable

Same arm, **one switch**: the bound, 1,920,000 ticks → **384,000 ticks (20 ms)**. Nothing else moves —
same probe, same handler, same site, same ending on the same 690 clock. The inference is untouched:
rung 9's cells all read what they read, and this arm re-reads the same cells with a shorter spin.

**What the shorter bound does to the two readings.** The handler's entry is timestamped
(`_pwr_irq_at`, rung 9's own addition) and so is the spin's start (`_wait_t0`), so the quantity to read
is `_pwr_irq_at − _wait_t0`:

| | (a) IRQ pending, delivered when the mask lifts | (b) the device is slow |
|---|---|---|
| `_wait_ticks` | ≈ 384,000 + a mid-batch remainder | same |
| `_pwr_irq_at − _wait_t0` | **≈ 385,000 ticks ≈ 20.06 ms** | **≈ 1,922,000 ticks ≈ 100.12 ms** |
| `_pwr_irq_at − (the spin's end)` | ≈ 1,153 ticks ≈ 60 µs (as on rung 9) | ≈ 1,537,000 ticks ≈ 80 ms |

The separation is **80 ms**, i.e. four times the bound — not a marginal reading. And the second row is
the one that carries the meaning: **under (a) the handler's distance from the END OF THE SPIN stays at
about 60 µs whatever the bound is; under (b) it grows with the bound.** So the rung is not "is 20 ms
long enough" (it is not meant to be long enough) but *what the handler's arrival is pinned to*.

**The third possibility, named so it is not read as (b):** the mask might lift at a fixed offset from
the **byte** rather than from the spin's end. That would print the same ≈100 ms as (b) and this rung
cannot tell it from (b) — but it is not a separate mechanism, it is (a) with a later unmask, and it
would be located by the *next* rung (which would have to find where the mask lifts). Stated here so
that "≈100 ms" on rung 10 is read as "the arrival is NOT pinned to the spin's end", not automatically
as "the controller takes 100 ms".

## 3. The cells this rung pre-registers

Unchanged from rung 9 except where noted; all nineteen `_wait_*` keys are expected present.

* `_wait_bound = 0x0005DC00` = **384,000** — the new switch value in the linked artifact. The build
  clause asserts the budget's `movw`/`movt` pair in `st_pwr_wait`'s **own body**, so an arm whose
  record says 384000 while the code carries 1920000 is refused before anything is parked. **This is
  also the switch's second exercise**: rung 9 proved the pair appears; this arm proves the number in
  the pair is *the record's number and not a constant in the source*, which is m720's shape.
* `_wait_req = 0x2`, `_wait_reset = 0` — the predicate and the branch do not move.
* `_wait_ctl_before = 0x00`, `_wait_ctl_after = 0x00` — expected as on rung 9: the poll cannot see the
  ack either way, since the ack is the handler's store.
* `_wait_timeout = 1`, `_wait_polls` ≈ 428,032 × (384,000 / 1,920,000) ≈ 85,606 = 83 or 84 batches
  (the inner batch is 1,024 reads and the bound crosses mid-batch — rung 9 measured 4.49 ticks per
  read, so 384,000 / 4.49 ≈ 85,500 reads).
* `_wait_cpsr = 0x80000093` — **the arm's premise, re-read**: if this arm's `I` were clear, the whole
  comparison would be about a different context and the rung would be void rather than negative.
* `_wait_done = 0`, `_wait_calls = 0` (at the wait's end), `_wait_state_*`/`_wait_io_*` = 0.
* **`_pwr_irq_at` — the reading this rung is for.**
* `_pwr_irq_calls_once_before_byte = 0`, `_pwr_irq_calls_probe_end = 0`, `_pwr_irq_calls = 1` —
  expected as on rung 9 under **both** readings (the probe's tail is ~11 µs after the spin's end and
  the handler is 60 µs after it on the (a) branch, so the count at the tail stays 0 either way; this
  cell is not the discriminator and is pre-registered here so it is not read as one).
* `_pwr_irq_ctl_before = 0x0` → `_pwr_irq_ctl_after = 0x1`, `_pwr_irq_status = 0x02`,
  `_pwr_irq_ack = 0x01`, no `_irq_other_*` — the acknowledge still happens, once, correctly.
* Inherited and unchanged: `_loads=6`, `_writes` 0→4, `_rst_stores=1`, `_mode_bit_after=1`, `_gate=1`,
  `_bcr=0x0`, `_cbcr=0x4ff1`, `_gcc_map=1`, `_map=1`, `_pwr_before=0x00`→`_pwr_after=0x0b`,
  `_pwr_vdd=7`, `_pwr_cc_before=0xE045`→`_pwr_cc_after=0xE047`, `_wait_cc_before=_wait_cc_after=0xE047`.
* The ending does not move: `_post_end_calls` ≥ 8 with `_post_elapsed` past 6,000 ms at 19,200,000 Hz.

## 4. The four outcomes, each a different next rung

1. **`_pwr_irq_at − _wait_t0` ≈ 20.06 ms** (and `_pwr_irq_at` ≈ 1,153 ticks after the spin's end):
   **(a)**. The completion is available the moment the mask lifts, the wait is portable, and the next
   rung is the faithful port of `wait_for_completion`'s *yield* — take the wait where (or after) the
   mask can come up, and rung 9's cells are then re-read with a real completion.
2. **≈ 100.12 ms**: the arrival is **not** pinned to the spin's end. The wait is not portable as a
   spin at this site at all, and the next rung must either move the wait to where the mask is clear or
   stop depending on the interrupt (the vendor's own probe-time pre-acknowledge, `sdhci-msm.c:2872-2889`,
   is the code that acks without a card).
3. **`_wait_timeout = 0`** (the ack inside 20 ms): the rung is void as a comparison — it would say the
   handler ran *during* the spin, which contradicts `I=1` unless something else changed, and the
   first thing to re-read is then `_wait_cpsr`.
4. **No return (exit 2)**: the arm's own negative cell, unchanged from rung 9 — a load from a block
   whose clock is off is a bus wait nothing ends. The hazard is *smaller* than rung 9's because the
   spin is 20 ms of a 6,000 ms window, and the gate is still read before any store.

## 5. What this arm does NOT do

The same limits as rung 9, unchanged: no command, no sector, no partition table, no mount, no new
device store (the write set is still `_rst_stores=1` plus the four inherited `_writes=4` plus rung 7's
power byte), and nothing is written to the medium. It runs the same site at the same rung with a
shorter bound; the rung digit stays 9 and the switch is the only difference — so **the arm's identity
is its switch record**, and the readiness row must print the value the record carries rather than a
constant in prose (this step's own change to `tools/verify_press_ready.sh`: the narration reads
`STAGE90_XNU_PWR_WAIT_TICKS` out of the entry record and asserts the sentence quotes it).

**The goal is still not met** and this rung does not change that: it is a reading about a wait, not
about the medium. **TWRP-to-storage stays withheld.**

## 6. Addendum, after the event: outcome 1, and the prediction was matched

The arm was built as specified (experiment-716, `armed-storage-pwrwait-aa2b051d`) and pressed
(experiment-717). **Outcome 1 happened**: `_pwr_irq_at − _wait_t0` measured **387,298 ticks = 20.172
ms** against the pre-registered ≈385,000 under (a), and the handler entered **1,167 ticks = 60.78 µs**
after the spin's end against 1,153 = 60.05 µs on the 100 ms arm. So (a) holds, **(b) is refuted** —
and it is refuted by the plainest cell of all: the completion arrived **inside 20.17 ms of the byte**,
so nothing about the device ever took 100 ms. §2's table was right to put the meaning in the *third*
row rather than the second, and §2's named third possibility (the mask lifting at a fixed offset from
the byte) is dead too: the arrival moved with the bound by exactly the bound's own 80 ms.

Two things this document got wrong, both minor and both recorded rather than rewritten: §3 predicted
`_wait_polls` "≈85,606 = 83 or 84 batches" (measured 86,016 = exactly 84 batches, and the bound
crossed mid-batch as predicted, overshoot 2,131 ticks = 111 µs), and §5's claim that the narration
"reads `STAGE90_XNU_PWR_WAIT_TICKS` out of the entry record and asserts the sentence quotes it" **was
not true when it was written** — the narration hard-coded `1920000` and `100 ms`, and this
pre-registration described the repair as if it had already been made. It was made in the same step,
and the check was measured firing before the press (experiment-716 §4, m725). A pre-registration that
describes a change in the past tense before the change exists is the same defect class as the rest of
this file, one tense over.
