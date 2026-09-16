# Experiment 93 — Stage90 Phase 0: preflight watchdog run

Date: 2026-09-16
Commit under test: `7eacf76` ("Stage90 Phase 0: repair the handoff test harness")
Build switches: `STAGE90_HANDOFF_MODE = PREFLIGHT_WATCHDOG_ONLY`,
`STAGE90_ENTRY_LADDER_LEVEL = FULL`, `STAGE90_BYPASS_ENTRY_STUB = 0`
Payload: `out/stage90/stage90-qcdt.img`, sha256 `1f80aa96…7ce37f19`

## What was being tested

The Phase 0 exit criterion from `docs/status/roadmap.md`: prove that the timer
interrupt can preempt a running payload and drive the PC-sample dump →
`platform_reboot()` → PS_HOLD warm-reboot path to completion, so that a later
hang self-recovers instead of needing a manual power-cycle.

This was the first hardware run of the repaired harness. In
`PREFLIGHT_WATCHDOG_ONLY` the payload does **not** install the candidate L1 and
does **not** jump anywhere: it stays under the bootloader's original known-good
mapping, arms the same PC-sampling watchdog `FULL` mode uses, and spins in a
bounded identity-mapped loop (4 × `SAMPLE_INTERVAL_US` × `SAMPLE_MAX` ≈ 32 ms).

## Procedure

```
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot boot out/stage90/stage90-qcdt.img
```

`fastboot` reported `Sending 'boot.img' (2926 KB) OKAY` / `Booting OKAY`, exit 0.

## Result: the device did not come back

Host kernel log on the Mi 4's USB port (`usb 3-10`):

```
USB disconnect, device number 63            <- leaving Android for the bootloader
new high-speed USB device number 98
  idVendor=18d1 idProduct=d00d Product: Android SerialNumber: 4a2fe00b
                                            <- fastboot mode, as expected
USB disconnect, device number 98            <- fastboot boot handed off to the payload
```

**No further enumeration.** The Mi 4 presented no USB device in any mode for the
~20 minutes the run was observed — no `fastboot`, no `adb`, no bare USB device.
Polled continuously with `adb devices` and `fastboot devices`.

So the payload ran and the device did not return to a live state on its own.
Two readings are consistent with the evidence and cannot be distinguished from
the host:

1. The payload hung, and neither the sampling watchdog nor the caller's
   `platform_reboot()` reached a working PS_HOLD reset.
2. PS_HOLD=0 *did* cut the PMIC hold and the device powered off, in which case
   it is dark and waiting for the power button.

Either way the Phase 0 exit criterion is **not met**: the device required manual
intervention. Recovery needs a power-button hold; nothing was written to
storage, so the device is not bricked — `fastboot boot` is non-persistent.

## Why this is informative anyway

This is the first run in which the payload was *known* to reach the handoff with
a real ladder behind it. The previous run's `loader_status = 0xd0000000` was a
structural harness failure (levels 0/1 faked their results and could never pass
the preflight), so no earlier run ever exercised `PREFLIGHT_WATCHDOG_ONLY` at
all. This run did.

## What reading the code afterwards established

Reading the reset path to design the fix turned up three things worth recording:

1. **The PS_HOLD reset path is proven; the coverage was not.** Experiments 03
   onwards all report the phone returning to Android automatically ~25–30 s after
   `fastboot boot`, with the ADB transport id advancing (e.g. experiment-03:
   "After about 25 seconds, the phone returned to normal Android without a
   manual power-button reset"). So `*restart_reason = RESTART_NORMAL; *ps_hold = 0`
   does produce a real restart into Android on this device. What was missing is
   *reach*: `stage90_arm_pc_sampling_watchdog()` was only ever called from inside
   `xnu_handoff_run`, so the DT build, `boot_args`, pexpert discovery, the timebase
   and MMU selftests, and the whole `arm_init` ladder ran with no recovery at all.
   A hang anywhere in that stretch leaves the device with no way back.

2. **The payload never enables the GIC.** `stage90_xnu_arm_vm_init_irq_window_run()`
   — the function that calls `stage90_gic_irq_init()` — is declared and defined but
   has **no caller** in the current build, so it is dead code. Timer and SGI
   interrupts nevertheless work (stage84's SGI/IRQ timer stage passed on hardware),
   which means the distributor and CPU interface are already enabled by aboot:
   `gic_sgi_selftest()` explicitly asserts `GICD_CTLR & 1` and `GICC_CTLR & 1` and
   returns 0 otherwise. The dead-man therefore only repairs the disabled case rather
   than assuming it must enable the GIC itself, and leaves the priority mask alone —
   widening PMR would only add interrupt sources.

3. **`gic_irq.c` has a wrong distributor base and is unreachable.** It defines
   `GIC_DIST_BASE 0xf9001000u`, while `gic.c` and `gic_validate_snapshot()` use
   `0xf9000000` — the value the device tree and the passing selftests agree on. The
   file is not in `build.sh`'s `SOURCES`, so the error has never been exercised.
   Left as-is here (fixing dead code is not this change); noted so nobody wires it
   into the ladder without correcting the base first.

## What was changed in response

- `STAGE90_HANDOFF_MODE` now defaults to `HARD_SKIP`; stepping up to
  `PREFLIGHT_WATCHDOG_ONLY` or `FULL` is a deliberate per-run decision.
- A **dead-man reset** (`STAGE90_DEADMAN_*`) is armed at the end of
  `kernel_entry`'s GIC validation, i.e. immediately before the loader preflight and
  everything after it. It reuses the same dump-then-`platform_reboot()` path and is
  re-armed after the handoff returns. On the happy path it never fires, because
  every exit already ends in `platform_reboot()`.
- `STAGE90_DEADMAN_SELFTEST=1` arms it and then spins forever, making the dead-man
  the only route back to Android — the direct hardware proof, and safe by
  construction.
- `stages/stage90/preflight_boot_check.sh` gates a run: it checks the image against
  `SHA256SUMS.txt`, checks the payload references no storage symbols, and refuses a
  mode the caller has not explicitly allowed.

None of this has been on hardware yet: the device did not come back from this run,
so the next step needs a power-button hold.

## Next step

Read `/proc/last_kmsg` after the power-cycle. It carries the payload's
ram_console output, which will name the last line the payload reached and
whether the watchdog fired. That is what distinguishes the two readings above.

If the power-cycle clears DRAM and `last_kmsg` comes back stale, the baseline run
is `STAGE90_HANDOFF_MODE = HARD_SKIP` with the dead-man armed (the default build),
which either completes cleanly or self-recovers.
