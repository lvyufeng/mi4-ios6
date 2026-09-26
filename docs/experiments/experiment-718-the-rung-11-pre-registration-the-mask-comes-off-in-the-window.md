# 718: the rung-11 pre-registration — the mask comes off: does the completion arrive inside the cache-off window the payload masks interrupts in?

**Date:** 2026-09-26. **Arm to build:** `STAGE90_XNU_STORAGE_PROBE=10` with
**`STAGE90_XNU_PWR_WAIT_TICKS=384000`** (20 ms) — *the rung-10 arm with one mechanism added and no
value moved*. **Purpose:** take the wait with **IRQs unmasked**, which is the only shape left in
which the completion this project has been waiting for since rung 9 can arrive, and by doing it
answer the one question the whole fixture has never been able to ask: what happens when an interrupt
is delivered **inside** the idle-exit window the payload masks interrupts in.

## 1. What rung 10 settled, and why this rung is the only shape left

Rung 10 (experiment-717) measured the completion **20.172 ms after the byte** and the handler's
arrival **60.78 µs after the spin gave up**, against 100.122 ms and 60.05 µs on the 100 ms arm. Two
consequences, both already stated in 717 §2 and repeated here because they are this rung's premise:

* **The device is fast and the mask is what the poll was measuring.** There is no 100 ms device
  latency to catch; `_pwr_irq_at − _wait_t0` puts the completion inside the first 20 ms of the byte's
  life, and the handler's distance from the spin's *end* does not move when the bound moves.
* **No budget can fix it.** The bound and the mask are not independent quantities — the mask ends
  where the code that owns it returns, and the spin is inside that code. The vendor's
  `wait_for_completion` works because it **sleeps**, i.e. because the context it runs in can be
  interrupted.

And the window is not a property of the *driver's* call, it is a property of **this fixture's site**:
`st_pwr_wait` is called from `entry_storage_probe`, the probe runs inside the payload's cache-off
idle-exit seam (`xnu_live_seam_sctlr = 0x30c57879`, `C` clear), and the probe's `_wait_cpsr` reads
`0x80000093` — SVC with `I` set — **on every arm of this ladder**. There is no site inside the
fixture that is not masked, so "take the wait somewhere the mask is already clear" (717 §2's second
shape) is not available *in the fixture*; only the first shape is. That is why this rung is a source
change and not a placement change.

## 2. The rung, and the one mechanism

One mechanism, added to the function rung 9 introduced, with the budget and every other switch left
exactly where rung 10 left them:

```
mrs cpsr_saved, cpsr        <- the state to restore, read BEFORE anything changes
cpsie i                     <- the mask comes off (I only; F is left as it is)
mrs cpsr, cpsr              <- the premise, published as _wait_cpsr
<poll, unchanged, bounded by the same 384,000 ticks>
msr cpsr_c, cpsr_saved      <- the mask goes back on, from the SAVED value and not a constant
mrs cpsr_after, cpsr        <- the state left behind, published as _wait_cpsr_after
```

Three properties of that shape are deliberate:

1. **The restore writes the saved register, not `cpsid i`.** The probe must leave the machine as it
   found it, and a constant would be a second definition of what "as it found it" means (the arm's
   own `_wait_cpsr` is the first). If the caller's `I` had been clear, an unconditional `cpsid i`
   would mask an interrupt the payload had left open and the probe would have *changed* the machine
   while appearing to clean up after itself.
2. **`cpsie i` and not a CPSR write.** Only `I` is cleared; `F` is already clear in this context
   (`0x80000093`), the mode is untouched, and nothing else in the CPSR is rewritten.
3. **The new cell is the state left behind, not the state the poll ran in.** `_wait_cpsr` keeps its
   definition — *the CPSR the poll runs under* — so the rungs stay comparable: 9 and 10 read
   `0x80000093` there, this arm **must read `0x80000013`** (`I` clear, everything else identical), and
   that difference *is* the arm. `_wait_cpsr_after` is the new quantity: *the CPSR the probe gives
   back*, which on this arm must read `0x80000093` — the same value rungs 9 and 10 read as their
   premise, which is exactly the reading that says the restore restored the *saved* state and not
   some other state that happens to look tidy.

The poll itself is not touched: same `CORE_PWRCTL_CTL` byte as its end condition, same
`CLOCK_CONTROL` halfwords either side, same batch of 1,024 reads between two clock samples, and **no
device store anywhere** — the ack the poll waits for is still the handler's store.

## 3. The cells this rung pre-registers

* **`_wait_cpsr = 0x80000013`** — the arm's premise, and the cell that decides whether the arm is a
  reading at all. SVC, `I` **clear**, `F` clear, `N` set: `0x80000093 & ~0x80`. If this reads
  `0x80000093` the `cpsie` did not take (or the compiler moved the `mrs` above it) and the run is
  **void as a comparison**, not negative.
* **`_wait_cpsr_after = 0x80000093`** — the new cell. The state the probe gives back, equal to the
  value the same code read as its premise on rungs 9 and 10.
* **`_wait_timeout = 0x0`** — **the flip this rung exists for.** On rungs 9 and 10 it read 1 on both
  arms. The loop leaves with `timeout = 0` only if the polled device byte was non-zero, and that byte
  is written by no arm of this ladder except rung 8's handler (rungs 9 and 10 read it as `0x00`
  before the wait and as `0x00` after 20 ms and 100 ms of polling).
* **`_wait_ctl_after = 0x01`** — the controller's own ack, `BUS_SUCCESS`, read by the probe out of
  `CORE_PWRCTL_CTL`. On rungs 9 and 10: `0x00` on both sides of the wait.
* **`_wait_polls` — small, and the size is a reading.** If the delivery lands before the poll's first
  read the count is 1; if it lands inside the first batch of 1,024 the count is that much plus one.
  What must NOT happen is 86,016 (rung 10's number, the whole bound spent).
* **`_wait_ticks` — small, against rung 10's 386,131.** The delivery's own latency from `_wait_t0`,
  plus the batch remainder. Rung 9 measured 4.49 ticks per read, so a batch is ~4,600 ticks = 0.24 ms
  and the loop can overshoot by at most that much after the handler has run.
* **`_wait_done = 0x1`, `_wait_calls = 0x1`, `_wait_state_after` with `REQ_BUS_ON` set,
  `_wait_io_after`** — **the vendor's predicate, satisfied, for the first time.** `_wait_done` is
  `((req & curr_pwr_state) | (req & curr_io_level)) != 0` over the two fields the handler's tail
  writes (`sdhci-msm.c:2092-2096`); it read `0` on rungs 9 and 10 because the handler had not run.
  **These are also the first readings of the flag/ack pair after the handler ran** — on rungs 9 and 10
  the waiter read them before the handler existed, so this arm is the first that can show whether the
  handler's `.bss` writes are visible to the probe's reads (`_wait_done = 1` beside
  `_wait_ctl_after = 0x01` says they agree; `_wait_done = 0` beside `_wait_ctl_after = 0x01` is the
  two-definitions defect 686's correction describes, measured instead of argued).
* **`_wait_ctl_before = 0x00`, `_wait_req = 0x2`, `_wait_reset = 0`** — unchanged; the ack is still
  absent before the wait and the vendor's already-satisfied branch is still not taken.
* **`_wait_cc_before = _wait_cc_after = 0xE047`** — unchanged (711 §3's answer, third arm).
* **`_pwr_irq_at − _wait_t0` — the new reading, and it is now the interrupt's OWN latency.** The
  handler's entry stamp against the poll's start: the interval from the mask coming off to the
  exception being taken, plus the few instructions between `_wait_t0` and the `cpsie`. On rungs 9 and
  10 this difference was the *spin* (1,922,347 and 387,298 ticks); here it must collapse to the
  delivery's own cost. **This is the number a port of `wait_for_completion` needs**: not "is the
  device fast" but "how long after the context can be interrupted does the completion arrive".
* **`_pwr_irq_*`, the handler's side, unchanged in value and now read from inside the window**:
  `_pwr_irq_calls = 1`, `_ctl_before = 0x0` → `_ctl_after = 0x1`, `_status = 0x02`, `_ack = 0x01`,
  `_status_after = 0x00`, `_state = 0x02`, `_io_level = 0x08`, and **no `_irq_other_*` key at all**.
  The client's device acts are the same five accesses at the same widths; what is new is the context
  it makes them in.
* Inherited and expected unchanged: `_storage_calls=1`, `_loads=6`, `_writes` 0→4, `_rst_stores=1`,
  `_mode_bit_after=1`, `_gate=1`, `_bcr=0x0`, `_cbcr=0x4ff1`, `_gcc_map=1`, `_map=1`,
  `_pwr_before=0x00`→`_pwr_after=0x0b`, `_pwr_vdd=7`, `_pwr_cc_before=0xE045`→`_pwr_cc_after=0xE047`.
* The ending does not move: `_post_end_calls ≥ 8` with `_post_elapsed` past 6,000 ms at
  `_post_cntfrq = 19,200,000` Hz.

**The write set is unchanged, and that is the safety contract.** Not one store is added: the write set
is still `_rst_stores=1` plus the four inherited `_writes=4` plus rung 7's power byte, and the one new
`.bss` word is the probe's own cell. The rung's whole device act is still two halfword reads of
`CLOCK_CONTROL` and a poll of one `CORE_PWRCTL_CTL` byte.

## 4. Why the premise is *nearly* certain, and where the real question is

The delivery three presses have already made is the argument: on rungs 8, 9 and 10 the handler was
**actually taken at this site** (`_pwr_irq_calls = 1` with the correct `_ctl_before`/`_ctl_after`
pair, `_status = 0x02`, `_ack = 0x01`, no `_irq_other_*`) — so the line is enabled, in Group 0, at a
priority `GICC_PMR = 0xf0` admits, targeted at this CPU, and its vector path works. `_wait_cpsr`'s
`I` bit is the *only* thing between "the completion has been latched since the byte" and "the handler
runs". So `_wait_timeout = 0` is close to a demonstration, and **this pre-registration does not
pretend otherwise** — a rung whose answer is not in doubt is still worth a press when the *context* is
new, and here it is:

* **No interrupt has ever been delivered inside this window.** Every previous delivery happened at
  the end of the probe, ~50 µs after the spin gave up, when the payload was already on its way out of
  the idle exit. This arm takes the exception **with the caches off** (`SCTLR.C` clear in the seam,
  read as `_seam_sctlr` on every run) and with the payload's own idle-exit code mid-flight. That is
  precisely the state the payload masks `I` to avoid, and this image has never entered it.
* **The handler's writes are read by the probe for the first time.** See `_wait_done` above: on every
  earlier arm the waiter's flag reads happened *before* the handler ran, so the cache-boundary
  question the rung-9 comment argues about (one flag, two definitions) has never had a comparison.
  This arm provides it, and the probe's own context (`C` clear, Strongly-ordered device reads) makes
  agreement through DRAM the expected answer rather than a coincidence.

## 5. The outcomes, each a different next rung

1. **`_wait_timeout = 0`, `_wait_ctl_after = 0x01`, `_wait_calls = 1`, `_wait_done = 1`, with the
   ending still firing.** The completion is observable with the mask off, inside the window, and
   **the storage path has its first completed handshake**: the vendor's predicate satisfied by the
   vendor's own ack. The port's requirement is met and the next rung is a step *up the line* rather
   than another reading about the wait — the card's rail, the RCG rate write, or the command path
   (`sdhci_send_command`), where "the card answers" begins.
2. **`_wait_timeout = 1` with `_wait_cpsr = 0x80000013`.** The mask was off, 20 ms passed, and the
   handler still did not run — which would mean something *below* the CPSR gates this line in this
   context (the CPU interface's priority mask, the distributor's group, the target). The next rung is
   then a read of `GICC_PMR`, the line's priority, `IGROUPR` and `ITARGETSR` **taken inside the
   unmasked window**, and the arm's premise-cells are already published. This is the outcome that
   would be worth more than outcome 1, and §4 is the reason it is unlikely.
3. **`_wait_cpsr = 0x80000093`.** The `cpsie` did not take. The run is void as a comparison and the
   first thing to re-read is the disassembly of `st_pwr_wait` itself.
4. **`_irq_other_*` present, or the run ending on `exception: irq line`.** Some other enabled line
   (an SGI, or a PPI) was asserted in the unmasked window and the dispatcher stopped with its intid in
   the log. This is a fact about the machine, contained by design, and it names the line — the enabled
   set at boot is wider than this image's (`ISENABLER0` reads `0x00107fff`: intids 0–14 set by
   something that is not this image, plus intid 20, the timer, which rung 8's arming writes).
5. **No return (exit 2).** This arm's own hazard, named here rather than discovered: the handler is
   entered in a context it has never run in, and a device access that cannot complete inside the
   cache-off window is a bus wait nothing ends. The gate is still read before any store, the poll is
   still bounded, the watchdog is still armed, and nothing is flashed — but a non-return costs this
   press and an unattributed return before it, and it refutes none of the cells above.

## 6. What this arm does NOT do

No command, no sector, no partition table, no mount, no new device store, and no driver beyond the
fixture: the rung's whole device act is two halfword reads and a poll of one byte, and neither of the
two armed shapes that *may sleep* is added (`sdhci_msm_setup_vreg` / `_pins` /
`set_vdd_io_vol` stay absent, because the vendor's own `IRQF_ONESHOT` + NULL primary declares a
threaded handler and this image has no thread to sleep in — `cpsie i` is a mask coming off for a
spin, **not** a sleep).

**The goal is still not met** — 「把基础驱动跑起来」 is not satisfied by a wait, however well it
completes, and the rung moves no byte of the medium. **TWRP-to-storage stays withheld.** What this
press buys, if it comes back with outcome 1, is that the last of the three mechanisms the driver's
first handshake needs — request, interrupt, completion — is measured working **in the context the
fixture can offer**, which is the precondition for every rung above it.

## 7. Addendum, after the event: outcome 1, with one prediction missed in a way worth keeping

The arm was built as specified (experiment-719, `armed-storage-pwrwait-unmask-13366e08`) and pressed
(experiment-720). **Outcome 1 happened**: `_wait_timeout = 0` beside `_wait_ctl_after = 0x01`,
`_wait_polls = 1`, `_wait_ticks = 1,403` (73.07 µs), `_wait_done = 1`, `_wait_calls = 1`,
`_pwr_irq_calls_probe_end = 1`, and the ending fired on schedule. §4's warning that the premise was
nearly certain and the *context* was the question was the right way round: the delivery came
immediately and the window tolerated it.

Three things this document got wrong or left implicit, all recorded rather than rewritten:

1. **§3's `_wait_cpsr_after` value was wrong, and it was wrong about the scope of an instruction.**
   It said the cell "must read `0x80000093`". It reads **`0x20000093`** — the same control field
   (SVC, `I` set, `F` clear) with the *condition flags* left as the poll's own `cmp` left them,
   because `msr cpsr_c` writes the control field and not the flags. The value the pre-registration
   named is the one a full `msr cpsr_fsxc` would have produced, and restoring the flags would have
   been wrong here — they belong to the running C code. The cell is right; the sentence was wrong.
2. **§3's `_wait_polls` prediction ("1 if the delivery lands before the poll's first read") fitted the
   measurement for a reason the document did not consider**: the exception is taken at the first
   instruction boundary after `cpsie i`, so the poll's first read may never execute before the
   handler. `_wait_polls = 1` therefore has two possible readings and this arm cannot separate them by
   itself — what separates them is an earlier press: rungs 9 and 10 polled the same byte 428,032 and
   86,016 times and read `0x00` every time, so the controller does not answer by itself and the ack
   the poll found is the handler's store.
3. **§3 did not say that `_wait_cpsr` is read *after* the `cpsie`**: if the exception is taken at the
   next instruction boundary, the premise reading is taken with the handler already run. It still
   measures the context the poll runs in (its definition), but it does not order the unmask against
   the delivery — `_pwr_irq_calls_probe_end` does, and it read `1` where the three masked arms read
   `0`.

The last paragraph of §1 asked what the mask was hiding. The answer is nothing: with the mask off the
wait costs the interrupt's own path (~54 µs) and the budget is never touched.
