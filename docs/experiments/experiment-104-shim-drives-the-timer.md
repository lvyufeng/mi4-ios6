# Experiment 104 — the platform shim drives the timer, through XNU's own interface

Date: 2026-09-17
Commit under test: `6f8d1e0`, plus the changes described below
Build switch: `STAGE90_XNU_MSM8974_SHIM = 1`
Other switches: `HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Captures: `/tmp/kmsg-armdemo1.txt` (first run — one bug), `/tmp/kmsg-armdemo2.txt` (the fix)

## What changed

`experiment-101` verified the MSM8974 shim's facts but noted it had **no caller**: `prepare`,
`commit` and `disarm` existed and nothing called them. This gives them one, and the result is the
first time the platform layer XNU would use has actually driven hardware on this device.

```
msm8974_shim_arm_interval_us=0x00002710       10000 us
msm8974_shim_arm_ticks      =0x0002ee00       192000 ticks = 10 ms x 19.2 MHz
msm8974_shim_arm_gicc_ctlr  =0x00000001       CPU interface enabled
msm8974_shim_arm_irq_before =0x00000001
msm8974_shim_arm_irq_after  =0x00000002       the interrupt arrived
msm8974_shim_arm_delivered  =0x00000001
msm8974_shim_arm_elapsed_us =0x000026e9       9961 us - 0.4% from what was asked for
msm8974_shim_arm_cntp_ctl   =0x00000000       disarmed, not left armed
msm8974_shim_arm_disarmed   =0x00000001
stage90 xnu_msm8974_shim: timer armed through tbd_ops, fired, serviced, disarmed
kernel_entry returned success
```

The arm went through `g_registered_ops.tbd_set_decrementer` — the callback XNU would call, not
the shim's inline helper — and the interrupt came back through the payload's own GIC handler and
counter (`stage90_timer_irq_count` 1 → 2). So the whole path is exercised: XNU's `tbd_ops`
interface, the ordered distributor → timer → delivery sequence, real IRQ delivery, and a clean
disarm.

**The elapsed time is the check worth keeping.** 9961 µs against a requested 10000 µs is 0.4%,
which says the shim's `usec * 96 / 5` tick conversion is right *and* that the interval the timer
actually measured is the interval it was given. A conversion that was wrong by a small factor, or
a `CNTFRQ` that disagreed with the hardware, would both show up here as a proportional error.

## Where it runs, and why not next to the registration

The demo is called from `kernel_entry` **after** `gic_sgi_selftest()` and `gic_timer_selftest()`,
not next to the shim's registration check. That ordering is the point: by the time it runs, "an
interrupt can be delivered at all on this platform" is an established fact from the payload's own
selftests, so a failure here is about the shim's *sequence* rather than about the platform. Placed
earlier, the two explanations would be indistinguishable from one log.

It disarms on every path, including the failure paths, so it cannot leave an interrupt source armed
that the payload's own timer code did not expect to find.

## A bug this found, and the way it was found

Run 1 reported nothing at all from the demo — no result block, and three of its fields still zero.
The log had exactly one line between the timer selftest and the TTBR0 roundtrip:

```
stage90 xnu_msm8974_shim: interval too large to encode
```

`prepare`'s guard read:

```c
/* Guard the conversion: ticks = usec * 19.2 must fit CNTP_TVAL's 32 bits. */
if (interval_us > (0xffffffffu / STAGE90_XNU_MSM8974_SHIM_EXPECTED_CNTFRQ)) { ... }
```

That is `interval_us > 4294967295 / 19200000` — **223 microseconds**. The intent was "the product
`interval_us * 19.2` must fit in 32 bits"; the code expressed "the product's *quotient* must be
less than the frequency", which is not a constraint at all, and it rejected every realistic
interval including the 10 ms asked for.

The right bound is on the intermediate the code actually computes: `interval_us * 96` must not
overflow, so `interval_us <= 0xffffffff / 96` ≈ 44.7 million microseconds — 44 seconds, which is
the range that makes sense for a decrementer.

Worth noting how it surfaced: not as a wrong number but as *no output at all*, because `prepare`
returns early on the guard and the demo's caller ignores the return value. Reading the code would
not have found it — the arithmetic looks like a bound either way. Running it did, and the log line
that explained it was one the shim already wrote.

## What this does and does not establish

**Does:** the platform layer is now load-bearing rather than descriptive. XNU's `tbd_ops` contract
carries a real interval into real hardware, the interrupt comes back, and the timing is right.

**Does not:** make XNU call it. XNU's `ml_init_timebase` is still not the thing that registers
these ops — the shim registers them itself, through a mirror of XNU's guard. The gap between "the
interface works" and "XNU uses the interface" is still XNU running, which is Phase 4.

It does mean that when XNU does run, the layer beneath it has been exercised on this hardware
rather than only described.
