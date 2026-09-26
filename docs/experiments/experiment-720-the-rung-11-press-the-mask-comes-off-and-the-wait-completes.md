# 720: the rung-11 press — the mask comes off and the wait completes: the poll never ran, and the completion is the interrupt's own store

**Date:** 2026-09-26 01:15:52–01:18:06 UTC. **Arm:** `armed-storage-pwrwait-unmask-13366e08`
(`STAGE90_XNU_STORAGE_PROBE=10`, `STAGE90_XNU_PWR_WAIT_TICKS=384000` — 716's arm with the `I` bit
cleared for the poll). **Verdict:** readiness 5/5 exit 0 (01:16:30), **one** gate
(`--allow-xnu-entry`, exit 0, 580 stdout lines, 01:17:00), **exactly one** runner
`--expect-arm=armed-storage-pwrwait-unmask-13366e08` → **EXIT 0** at 01:18:06 (returned and captured).
Firer `/tmp/g668/press-on-clear.v5.sh` (sha `7f23cae5…`), budget 600 s, `done. gate=0 runner=0` —
**no firer is armed**. Capture 622,312 B, sha256
`fef6a5e8bf6cf3dea0ad8b28ca1e87f4d1d8908a2ae840f71e9b75afc9bd5d38`, archived by hand as
`out/stage90/captures/rung11-unmask-20260926-011552-{last_kmsg.txt,press,gate,run}`. One
non-persistent `fastboot boot`; nothing flashed; `33e80afe` off the bus throughout; the device came
back on its own (`adb devices` → `4a2fe00b device`).

**The rung's answer: the vendor's own completion, satisfied.** `_wait_timeout = 0` and
`_wait_ctl_after = 0x01` — the controller carries `BUS_SUCCESS` for the first time on this ladder —
and the cell that explains how is `_wait_polls = 1` beside `_wait_ticks = 1,403`: **the poll never got
to run.** The exception was taken in the window between `cpsie i` and the poll's first read, so the
wait's entire cost is the interrupt's own path and the 20 ms budget was never touched.

## 1. The answer, in one table

Every number is from the capture, in ticks of the **19,200,000 Hz** counter the hardware itself
reported (`_post_cntfrq = 0x0124f800`).

| cell | rung 9 (masked, 100 ms) | rung 10 (masked, 20 ms) | **this arm (unmasked, 20 ms)** |
|---|---|---|---|
| `_wait_cpsr` | `0x80000093` (I **set**) | `0x80000093` | **`0x80000013`** — I **clear** |
| `_wait_cpsr_after` | — | — | **`0x20000093`** (see §5) |
| `_wait_ctl_before` → `_wait_ctl_after` | `0x00` → `0x00` | `0x00` → `0x00` | **`0x00` → `0x01`** |
| `_wait_timeout` | `1` | `1` | **`0`** |
| `_wait_polls` | 428,032 | 86,016 | **1** |
| `_wait_ticks` | 1,921,194 (100.062 ms) | 386,131 (20.111 ms) | **1,403 (73.07 µs)** |
| `_wait_done` | `0` | `0` | **`1`** |
| `_wait_calls` | `0` | `0` | **`1`** |
| `_wait_state_after` / `_wait_io_after` | `0` / `0` | `0` / `0` | **`0x02` / `0x08`** |
| `_pwr_irq_calls_probe_end` | `0` | `0` | **`1`** |
| `_pwr_irq_at` − `_wait_t0` | 1,922,347 (100.122 ms) | 387,298 (20.172 ms) | **1,033 (53.80 µs)** |
| `post_t0` − (wait end) | 217 (11.30 µs) | 217 (11.30 µs) | **231 (12.03 µs)** |

Inherited and unchanged from the pressed rung-9 and rung-10 arms: `_wait_req = 0x2`,
`_wait_reset = 0`, `_wait_bound = 0x0005DC00` (384,000 — the switch, in the linked artifact),
`_wait_done_before = 0` with `_wait_state_before = 0` and `_wait_io_before = 0` (the vendor's
already-satisfied branch was **not** taken, so the poll ran),
`_wait_cc_before = _wait_cc_after = 0xE047` (**711 §3's answer, now on a third arm**),
`_storage_calls = 1`, `_loads = 6`, `_writes` published 0→4, `_rst_stores = 1`,
`_mode_bit_after = 1`, `_gate = 1`, `_bcr = 0x0`, `_cbcr = 0x4ff1`, `_gcc_map = 1`, `_map = 1`,
`_pwr_before = 0x00` → `_pwr_after = 0x0b` with `_pwr_vdd = 7` and
`_pwr_cc_before = 0xE045` → `_pwr_cc_after = 0xE047`.

The handler's side is unchanged in value and new in context: `_pwr_irq_calls = 1`,
`_pwr_irq_intid = 0xaa` (170), `_pwr_irq_ctl_before = 0x0` → `_pwr_irq_ctl_after = 0x1`,
`_pwr_irq_status = 0x02`, `_pwr_irq_ack = 0x01`, `_pwr_irq_status_after = 0x00` (the latch let go),
`_pwr_irq_state = 0x02`, `_pwr_irq_io_level = 0x08`, `_pwr_irq_pad_ctl_before = _after = 0x0a1c`.
**No `_irq_other_*` key, no `_irq_spurious_*`, no `_irq_cli_storm`** — the unmasked window delivered
this image's own client and nothing else.

## 2. The poll never ran: what `_wait_polls = 1` and `_wait_ticks = 1,403` say together

The exception is taken at the first instruction boundary after `cpsie i`, and the CPU does not return
to the poll until the handler has finished. So the timeline, all from this log:

```
_wait_t0        = 0x076545e2
_pwr_irq_at     = 0x076549eb   -> +1,033 ticks = +53.80 us  (the client's first statement)
wait end        = _wait_t0 + _wait_ticks = 0x07654b5d  -> +1,403 ticks = +73.07 us
_post_t0        = 0x07654c44   -> +231 ticks past the wait's end
```

`_wait_polls = 1` means **exactly one iteration of the poll body was ever executed**, and it returned
the ack. The read that returned it and the `now` stamp that ends the wait are two adjacent
instructions — yet 1,403 ticks separate them from `_wait_t0`. The only thing that fits is that the
handler ran *in between*: the exception was taken before the poll's first read, the client's first
statement landed at +53.80 µs, its work took ~19.27 µs (370 ticks), and the poll's one read then saw
the handler's own store.

**And the alternative reading is refuted by an earlier press, not by an argument.** If the poll's
first read had executed *before* the handler, `_wait_polls = 1` would mean the controller answered by
itself within a few ticks of the mask coming off. It does not: **rungs 9 and 10 polled that same byte
428,032 and 86,016 times and read `0x00` every time**, on the same byte written by the same arm — so
this machine does not raise `CORE_PWRCTL_CTL`'s `BUS_SUCCESS` on its own inside 100 ms. The ack is the
handler's store, the handler is what the interrupt delivered, and the wait was satisfied by the
interrupt. **That is the vendor's `wait_for_completion`, ported and working.**

**The interrupt's own latency is the new reading, and it is 53.80 µs** from `_wait_t0` (taken a couple
of instructions before the `mrs`/`cpsie`) to the client's first statement — of which the last part is
this image's own dispatcher and the client's prologue. Rungs 9 and 10 measured the *same* path from
the other end: 1,153 and 1,167 ticks (**60.05 and 60.78 µs**) from the spin's end, which is where the
payload's own idle-exit code lifted `I`. **Two arms that never entered the unmasked state and one
that did agree on the order of the delivery's cost**, and the 6 µs between them is the difference
between the two starting points, not a second mechanism.

**The budget was never touched.** 384,000 ticks were available and 1,403 were spent; the wait's whole
cost is the interrupt. The bounded poll is no longer an approximation of the block it replaced — with
the mask off it *is* the block, and the bound is only the failure path.

## 3. `_wait_done = 1` beside `_wait_ctl_after = 0x01`: the comparison this ladder never had

The rung-9 design argues at length that the poll's end condition must be the **controller's** byte and
not the image-side flag, because the probe runs with `SCTLR.C` clear while the handler may run with
the caches on — *one flag, two definitions*. On rungs 9 and 10 that argument had no comparison: the
waiter read `g_pwr_irq_done`, `g_pwr_curr_state` and `g_pwr_curr_io` **before** the handler had ever
run, so those cells were `0` by construction (m723).

On this arm the handler has run when the waiter reads them — for the first time on the ladder — and
the two sides **agree**: `_wait_ctl_after = 0x01` (the device) with `_wait_done = 0x1`,
`_wait_state_after = 0x02` and `_wait_io_after = 0x08` (the image's side of the same handshake). The
vendor's predicate — `(REQ_BUS_ON & curr_pwr_state) | (REQ_BUS_ON & curr_io_level)` — is **satisfied**
for the first time, and it is satisfied by fields the handler wrote microseconds earlier in the
same window the probe is running in, with the caches off on both sides. The expected answer, and the
first occasion this project has had to expect one.

## 4. `_pwr_irq_calls_probe_end = 1` — the delivery happened inside the probe

Every masked arm carries `_pwr_irq_calls_probe_end = 0`: the handler had not run at the probe's tail,
because the mask outlived the probe. m723 recorded that cell as **expected by construction** and
therefore not evidence about the spin. This arm reads **`1`**, which is the same cell with the
construction removed: the client had run before the probe's tail, i.e. **inside the probe**, i.e. in
the window. The rung's central claim — an interrupt delivered inside the payload's cache-off idle-exit
seam — is not inferred from `_wait_cpsr`; it has its own witness in the handler's own counter, and
m723's note now has the counter-example that makes it a reading.

The run also survived it: the ending fired on its own schedule (`_post_end_calls = 0x7`,
`_post_cntfrq = 0x0124f800`, the elapsed pair past `0x06e15472` = 6,011.8 ms of 6,000 ms) and the
machine stayed up through it. **The context was new and it did not misbehave** — and the cost was
73 µs of a 6,000 ms window.

## 5. The prediction that missed, and it missed in a way worth naming

The pre-registration (§3 of experiment-718) said `_wait_cpsr_after` **must read `0x80000093`** — the
value rungs 9 and 10 read as their premise, offered as the reading that would prove the restore
restored the *saved* state. It read **`0x20000093`**.

The difference is the **condition-flag field** (bits 28–31): the saved value had `N` set (bit 31,
`0x80000000`), the measured value has `C` set (bit 29, `0x20000000`) — the carry the poll's own `cmp`
left behind. **And that is what `msr cpsr_c` is for: the `_c` suffix writes the CONTROL field — mode,
`T`, `F`, `I` — and not the flags.** Restoring the flags would have been wrong: they belong to the
running C code, and this function is a called function, not an exception return.

So the reading is *better* than the prediction, and the prediction's defect is the defect class this
project meets most often, one field over: **the pre-registration named a value without naming the
scope of the instruction that produces it**, and the value it named was the one a full `msr
cpsr_fsxc` would have produced. Read the control field and the arm's claim holds exactly:

```
0x20000093  =  0x20 | mode 0x13 (SVC) | I set (0x80) | F clear
              ^^ the C flag from the poll      ^^^^^^^^^^^^^^^^^^^^^ the restored mask
```

**The mask went back on**, the mode is unchanged, `F` is untouched, and the flags are the ones the
poll's arithmetic left — which is the honest state for a function that borrowed the interrupt
disables and nothing else. Recorded here as a miss rather than repaired: the cell is right, the
sentence in the pre-registration was wrong about which bits the instruction writes.

One more limitation of the cell, stated so it is not read as more than it is: `_wait_cpsr` is read
**after** the `cpsie`, and if the exception was taken at the very next instruction boundary the
premise reading is taken *after* the handler has already run. It still measures *the context the poll
runs in* — the definition it keeps — but it does not order the unmask against the delivery. The cell
that does is `_pwr_irq_calls_probe_end`.

## 6. Cost, what this buys, and the goal

**Cost:** one press, 134 s of wall clock (readiness 01:16:30 → runner exit 01:18:06), 73 µs of the
run's ending window (against 20,000 µs on the arm below it, whose whole bound was spent on its own
mask), and no store to any device: the write set is still `_rst_stores=1` plus the four inherited
`_writes=4` plus rung 7's power byte. **No new device access, no new `.bss` word, no new symbol** —
the arm's only addition to the artifact is one published cell and two cp15 instructions.

**Bought:** the last of the three mechanisms the driver's first handshake needs. The request (rung 7's
byte), the interrupt (rung 8's client on intid 170) and the completion (this rung's wait) now stand in
one run with the vendor's own predicate satisfied, in a context that can be interrupted — which is
what 717 §2 asked for and what 714's own failure named. Two questions this rung closes for good:

* **the completion is not slow**: it is available the moment the context can be interrupted, and the
  latency is the exception's own path (~54 µs here, ~60 µs measured from the other end on rungs 9 and
  10). No budget is needed beyond a bound on the failure path.
* **the window tolerates an interrupt**: the first delivery inside the payload's cache-off idle-exit
  seam ran the client, wrote the ack, filled the flags and returned, and the run continued to its
  ending — so the port does not need a site outside the seam for this wait to be satisfiable.

**THE GOAL IS STILL NOT MET.** No command, no sector, no partition table, no mount, no driver beyond
the fixture: this rung's whole device act is still two halfword reads and a poll of one byte, and its
log carries no key that names the medium. **TWRP-to-storage stays withheld.** With the wait working,
the next rung is a step up the line rather than another reading about the wait — and the list is
unchanged and already owed:

* **the card's rail** — nothing in this image powers a supply;
* **the RCG rate write** — 705/706 measured 192 MHz where the driver believes 400 kHz, and
  `sdhci_msm_set_clock`'s `clk_set_rate` is the RCG's only writer;
* **rung 6's two stores to `hc_mem + 0x10C`** with the readback cell;
* **the command path** (`sdhci_send_command`), where "the card answers" begins;
* **carried unchanged**: what actually returns a run; the ending's first store faulting
  (`0x0fa0065c`); the width clause of the store census never fired on a WIDENED DEVICE store; the 691
  §5 `entry_note_wfi` readback; `entry_reset.h`'s false IMEM claim; 676 §6 / 677 §6; the 684-owed
  runner clause; `tools/xnu_dt_requirements.py` and the `"master"` value; the two peer-lane tripwire
  repairs; BIT(29) of `_clk_ahb_cbcr`; the seam address pinned in two files; and the gate's narration
  of `STAGE90_XNU_STORAGE_PROBE`, still eight rungs short (2 through 9) — peer lane, by message.
