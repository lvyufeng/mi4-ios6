# Experiment 101 — the Phase 3 platform shim, first hardware run: one missing assignment

Date: 2026-09-17
Commit under test: `a63de03`, plus the fix described below
Build switch: `STAGE90_XNU_MSM8974_SHIM = 1` (default off)
Other switches: `HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Captures: `/tmp/kmsg-shim1.txt` (first run), `/tmp/kmsg-shim2.txt` (after the fix)

## Why this shim exists

Measured, now, in `experiment-100`: `pe_arm_map_interrupt_controller` computes
`ml_io_map(soc_phys + reg[0], ...)` with `soc_phys` from `/arm-io`'s `ranges[1]`, and on this tree
that evaluates to `0xf2020000` for a timer at `0xf9020000`. Independently, `pe_arm_init_timer` is
a chain of `#if defined(ARM_BOARD_CLASS_*)` checks whose fallthrough is `return 0`, and the 32-bit
ARM `board_config.h` defines exactly three board classes, all Apple. So on MSM8974 the stock path
fails twice over, and the fix is to *replace* `pe_arm_init_interrupts` rather than to configure it.

This is the first hardware run of that replacement. It registers its `tbd_ops` through a mirror of
`ml_init_timebase`'s guard and then checks the facts the real bring-up depends on.

## Run 1: 8 checks, 8 failures — and the cause was one missing assignment

```
msm8974_shim_status=0xd0000008        checks=8   failures=0x00000008
msm8974_shim_cpu_data_guard_ok=0x00000001
msm8974_shim_registered=0x00000001    registration_verified=0x00000001
msm8974_shim_get_decrementer_registered=0x00000001
msm8974_shim_set_decrementer_registered=0x00000001
msm8974_shim_int_address=0x00000000   <-- and int_value=0x00000000
msm8974_shim_timer_intid=0x00000013   gicc_eoir=0xf9002010   cntfrq=0x0124f800
msm8974_shim_cntp_tval_readback=0x0000fff2   cntp_ctl=0x00000001   decrementer_roundtrip=1
msm8974_shim: not satisfied
```

`failures` is a mask and its only set bit was `FAIL_EOI_PAIRING` (`0x8`). The check reads

```c
if (r->int_address != MSM8974_GICC_EOIR || r->int_value != MSM8974_TIMER_CNTP_INTID)
```

and **nothing ever assigned `r->int_address` or `r->int_value`.** The mechanism underneath was
fine — `registration_verified=1` is only set after the shim reads the stored address and value
back and compares them, and `g_registered_int_address`/`g_registered_int_value` were being set
correctly all along. Two result fields were declared, logged and checked, and never written, so a
working mechanism read exactly like a broken one.

The fix reads them through the shim's own accessors (`stage90_xnu_msm8974_shim_int_address()` /
`_int_value()`) rather than from the file-scope statics, because the accessors are what a caller
uses and the check is about what a caller can see.

## Run 2: green

```
msm8974_shim_status=0x90000001        checks=8   failures=0x00000000
msm8974_shim_int_address=0xf9002010   int_value=0x00000013
msm8974_shim_timer_intid=0x00000013   gicc_eoir=0xf9002010
msm8974_shim_cntfrq=0x0124f800        decrementer_roundtrip=0x00000001
msm8974_shim_arm_prepared=0           arm_committed=0   arm_ticks=0   arm_gicc_ctlr=0
stage90 xnu_msm8974_shim: registration and hardware facts verified
kernel_entry returned success
```

What the eight checks now establish, on hardware:

- **The EOI pairing.** `int_value` is the IAR (the interrupt ID to write) and `int_address` is
  where it is written: `0x13` — the CNTP PPI, interrupt 19 — into `GICC_EOIR` at `0xf9002010`.
  That pair is the whole contract between XNU's `tbd_ops` registration and the GIC.
- **The registration took**, verified by reading the installed ops back and comparing against the
  functions the shim supplied — not merely that the call happened. The negative case runs too: a
  registration against a non-boot `cpu_data` is refused, which XNU would silently drop and the
  shim records.
- **The counter the callbacks drive is the one whose interrupt is wired.** `CNTFRQ` reads
  19200000 (`0x0124f800`), the decrementer roundtrips through the registered callback, and
  `CNTP_CTL` reads back armed.
- **The timer is left disarmed** (`arm_prepared=0`, `arm_committed=0`), which is deliberate: the
  payload's own timer code owns arming from this point, and the shim must not take an interrupt
  source away from it mid-run.

## What this does and does not establish

**Does:** the MSM8974 replacement for XNU's ARM platform bring-up is correct about the facts it
can check on this hardware, and the two ways the stock path fails here are both side-stepped
rather than worked around.

**Does not:** it is not wired into anything. No XNU code calls it, because no XNU code runs a
platform bring-up here yet — the Stage-owned `arm_init` ladder is still what executes. It is a
verified component waiting for a caller, and the caller is the point at which XNU's own
`pe_arm_init_interrupts` would be substituted.

## The pattern, again

This is the sixth instance of the same shape in this project: **a value that is asserted or
plumbed in two places and written in only one.** The previous five were the device-tree child
count, three descriptor literals, the "caches are off" comparisons, and `boot_args`' duplicate
definition. This one differs in that nothing was *wrong* — the value was simply never copied —
but the symptom is identical: a failure that points at the mechanism when the mechanism is fine.

The generalisable lesson, which is now the reason this is a memory rather than a note: when a
check fails on a value that was never used for anything else, **check whether anything writes
it** before investigating what produces it.
