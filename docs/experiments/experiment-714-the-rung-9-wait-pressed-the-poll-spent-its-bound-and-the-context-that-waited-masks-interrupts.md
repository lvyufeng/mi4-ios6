# 714: the rung-9 wait pressed — the poll spent its whole bound, the completion arrived 60 µs late, and the context that waited had IRQs masked

**Date:** 2026-09-26 00:31–00:32 UTC. **Arm:** `armed-storage-pwrwait-8b824cac`
(`STAGE90_XNU_STORAGE_PROBE=9`, `STAGE90_XNU_PWR_WAIT_TICKS=1920000`). **Verdict:** readiness 5/5 exit 0
(00:31:01), **one** gate (exit 0, 578 stdout lines, 00:31:31), **exactly one** runner
`--expect-arm=armed-storage-pwrwait-8b824cac` → **EXIT 0** at 00:32:39 (returned and captured). Capture
625,346 B, sha256 `816b792244f726b9…`, archived by hand as
`out/stage90/captures/rung9-pwrwait-20260926-003122-{last_kmsg,press,gate,run}`. Firer
`/tmp/g668/press-on-clear.v5.sh` (sha `7f23cae5…`), budget 600 s, `done. gate=0 runner=0` — **no firer is
armed**. `fastboot boot` only, `33e80afe` off the bus throughout, and the device came back on its own
(`adb devices` → `4a2fe00b device`).

**The rung's answer is a refutation of the arm's design rather than of the controller.** The wait could
not have succeeded, and the log says why in one cell.

## 1. What the nineteen cells read

| cell | value | reading |
|---|---|---|
| `_wait_req` | `0x2` | `REQ_BUS_ON` — the only request `sdhci.c:1372` makes |
| `_wait_bound` | `0x001d4c00` = 1,920,000 | the switch itself, in the linked artifact (the build asserts its `movw`/`movt` pair) |
| `_wait_reset` | `0x0` | the vendor's already-satisfied branch was **not** taken — the poll ran |
| `_wait_ctl_before` | `0x00` | `CORE_PWRCTL_CTL` before the wait |
| `_wait_ctl_after` | `0x00` | **the controller never carried `BUS_SUCCESS`** |
| `_wait_timeout` | `0x1` | the poll spent its whole bound |
| `_wait_polls` | `0x00068800` = 428,032 | = 418 inner batches of 1,024 reads |
| `_wait_ticks` | `0x001d50aa` = 1,921,194 | 100.062 ms; the bound crossed **mid-batch** (overshoot 1,194 ticks = 62.2 µs), 4.49 ticks per read |
| `_wait_cpsr` | `0x80000093` | **SVC mode, `I` SET (IRQs masked), `F` clear** |
| `_wait_done_before` / `_wait_state_before` / `_wait_io_before` | `0` / `0` / `0` | the handler's fields were never filled |
| `_wait_done` / `_wait_state_after` / `_wait_io_after` | `0` / `0` / `0` | still nothing, 100 ms later |
| `_wait_calls` | `0x0` | the handler had not run at the wait's own end |
| `_wait_cc_before` / `_wait_cc_after` | `0xE047` / `0xE047` | `CLOCK_CONTROL 0x2C` as a halfword, **identical either side** |

And the handler's own keys, from the same log: `_pwr_irq_calls_once_before_byte=0x0`,
`_pwr_irq_calls_probe_end=0x0`, `_pwr_irq_calls=0x1`, `_pwr_irq_ctl_before=0x0` →
`_pwr_irq_ctl_after=0x1`, `_pwr_irq_status=0x02`, `_pwr_irq_ack=0x01`. No `_irq_other_*` key at all.

## 2. The cell that explains the rung: `I` is set

The completion rung 9 waits for is `BUS_SUCCESS` in `CORE_PWRCTL_CTL`, and **that bit is written by the
interrupt handler** (rung 8's client, `sdhci-msm.c:2069`). `_wait_cpsr` says the polling context is SVC
mode with **IRQs masked** — so while the poll spins, the handler that would set the bit *cannot be
taken*. The handler is delivered **1,153 ticks = 60.05 µs after the poll gives up**:

```
_wait_t0        = 0x0748bc55
wait end        = _wait_t0 + _wait_ticks = 0x07660cff
handler entered = _pwr_irq_at           = 0x07661180   ->  +1,153 ticks = +60.05 us
_post_t0        = 0x07660dd8                           ->  +  217 ticks = +11.30 us
```

The count keys fill in the same story from the other side: the byte raised the line
(`_calls_once_before_byte = 0`) and the handler had *still* not run at the probe's tail
(`_calls_probe_end = 0`) — the wait lives inside the probe — while the final log carries
`_pwr_irq_calls = 1`. It ran exactly once, after all of it, and did the acknowledge correctly when it
did.

**So the arm's premise is falsified by its own measurement: a bounded spin in this context cannot
observe this completion at all.** The vendor's `wait_for_completion` works precisely because it
**sleeps** — it unmasks, the handler runs, the completion is raised, the sleeper wakes. This arm kept
the predicate, the request and the acknowledge, and replaced the sleep with a spin; in a context that
masks interrupts, that removes the only mechanism by which the completion can arrive. The port lost
the one property that makes the wait satisfiable.

## 3. The two readings this press cannot separate — named rather than chosen

* **(a) The IRQ was pending throughout the spin and was delivered when the mask lifted.** On this
  reading the controller acknowledged early, and rung 9 measured its own masked window rather than the
  device.
* **(b) The controller really took about 100 ms, and the bound was a hair too short.** On this reading
  a longer budget would have caught it.

The evidence **favours (a)**: `_wait_cpsr`'s `I`, and a handler delay that tracks the *end of the
spin* rather than any device timer. But one run cannot separate a 100 ms device latency from a 100 ms
masked window, **because this arm set its bound and its mask to the same length by construction** —
the correlation is a property of the experiment. What separates them is a run that **unmasks during
the spin** (or yields), or a completion source that is not the interrupt at all; a second wait taken
after the first would return at once under *both* readings and therefore decides nothing. That is the
design question the next rung inherits, and it is stated here so the next rung does not spend a press
rediscovering it.

## 4. And 711 section 3 is answered, in one run instead of two

711 had two boots that disagreed about `CLOCK_CONTROL 0x2C` bit 1 (`0xE045` vs `0xE047`) and no
before/after pair inside either, so it could not tell a race from the machine's state. This arm reads
that register **three times in one run**:

```
_pwr_cc_before = 0xE045   (rung 7's read, before the power byte)
_pwr_cc_after  = 0xE047   (rung 7's read, after it)
_wait_cc_before = 0xE047  (the waiter's read, at the wait's start)
_wait_cc_after  = 0xE047  (the waiter's read, 100 ms later)
```

**The bit is set by the power byte's own store and is stable across a hundred milliseconds
afterwards.** Not a race; deterministic. It also completes the correction of 711's one unpredicted
cell: 709 read `0xE045` on both sides because **709's arm (rung 6) never wrote the byte at all** — so
the difference between 709 and 711 was the arm, not the machine, and 711's two candidate readings are
both replaced by a third and simpler one.

## 5. The cells that say this arm changed only what it says it changed

Every inherited key reads as it did on the pressed rung-8 arm: `_storage_calls=1`, `_loads=6`,
`_writes` published 0→4 through the mode sequence, `_rst_stores=1`, `_mode_bit_after=1`, `_gate=1`,
`_bcr=0x0`, `_cbcr=0x4ff1`, `_gcc_map=1`, `_map=1`, `_pwr_before=0x00` → `_pwr_after=0x0b` with
`_pwr_vdd=7`. The ending is unchanged: `_post_end_calls=0x8` with `_post_elapsed=0x090f19e0` =
7,915.9 ms at `_post_cntfrq=0x0124f800` = 19,200,000 Hz — the 690 clock fired on its own schedule and
the machine stayed up through it. **The wait cost the run 100 ms of a 6,000 ms window and nothing
else**, which is the safety property this rung was designed for: the poll is bounded, so the run always
continues.

## 6. What the press cost and what it bought

* **Cost:** one press, 68 s of wall clock (gate 00:31:31 → runner exit 00:32:39), 100 ms of the run's
  own ending window, and no store to any device — the write set is still `_rst_stores=1` plus the
  four inherited `_writes=4` plus rung 7's power byte.
* **Bought:** the completion's delivery mechanism, measured. The next rung's design question is now
  stated rather than open: *the wait must either unmask around the spin or stop depending on the
  interrupt*, and the failure mode of not doing so is that the poll measures its own mask.

## 7. Owed, and the goal

**The goal is still not met.** No command, no sector, no partition table, no mount, no driver beyond
the fixture; this rung's whole device act is two halfword reads and a poll of one byte, and its log
carries no key that names the medium. **TWRP-to-storage stays withheld.**

* **The rung the press set up:** the wait taken where the interrupt can be delivered (or a completion
  source that is not the interrupt) — with §3's two readings as the thing to separate, not to assume.
* **On the storage path, carried:** the card's rail (nothing in this image powers a supply); the RCG
  rate write (705/706 measured 192 MHz while the driver believes 400 kHz); rung 6's two stores to
  `hc_mem + 0x10C` with the readback cell; the command path (`sdhci_send_command`), where "the card
  answers" begins.
* **Carried unchanged:** what actually returns a run; the ending's first store faulting
  (`0x0fa0065c`); the width clause of the store census never fired on a WIDENED DEVICE store; the 691
  §5 `entry_note_wfi` readback; `entry_reset.h`'s false IMEM claim; 676 §6 / 677 §6; the 684-owed
  runner clause; `tools/xnu_dt_requirements.py` and the `"master"` value; the two peer-lane tripwire
  repairs; BIT(29) of `_clk_ahb_cbcr`; the seam address pinned in two files; and the gate's narration
  of `STAGE90_XNU_STORAGE_PROBE`, still eight rungs short (2 through 9) — peer lane, by message.
