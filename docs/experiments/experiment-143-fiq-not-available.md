# Experiment 143 — MSM8974 will not deliver an FIQ to non-secure PL1, measured

Date: 2026-09-17
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured
Artifacts: `stages/stage90/xnu_msm8974_fiq_probe.c` (new), `stage90.h`, `xnu_kernel.c`,
`build.sh`, `preflight_boot_check.sh`

**Result: with the CNTP PPI in Group 0, the GIC's Group 0 enabled at both ends, FIQ unmasked and the
timer measurably fired — no FIQ is taken. The probe's three checks pass and the device returns to
Android unattended.**

This settles a Phase 3 design question that had been resting on a quotation.

## Why it is a question worth a device run

XNU chooses its timer vector at assembly time (`locore.s:147`):

```asm
        adr     pc, Lexc_irq_vector
#if __ARM_TIME__
        adr     pc, Lexc_decirq_vector
#else /* ! __ARM_TIME__ */
        mov     pc, r9                  /* -> the shim's tbd_fiq_handler */
#endif
```

`__ARM_TIME__` is used in sixteen places in the ARM tree and **defined in none of them**, so the
`#else` branch is live: **XNU's timer arrives on FIQ on this build**, `pe_arm_init_timer` supplies
`fleh_fiq_generic` for it, and the shim deliberately leaves `tbd_fiq_handler` NULL.

But `docs/status/phase3-msm8974-shim-spec.md` records, from the vendor's own header, that FIQ on this
family *"requires secure mode"* (`msm_watchdog.h:28`), and the cancro device tree does not enable
kernel FIQ. If that is right then **no shim can supply this path**, and the resolution is the one the
spec's §6.2.1 already names: define `__ARM_TIME__` and take the tree's complete IRQ path instead.

## What ran

`xnu_msm8974_fiq_probe.c`, behind `STAGE90_XNU_MSM8974_FIQ_PROBE` (default off), does the smallest
thing that can answer it: configure Group 0 for the measured CNTP PPI, enable it at both ends,
unmask `CPSR.F`, arm the timer, wait, and see whether `vector_fiq` runs.

```
fiq_gicd_ctlr=0x00000001              distributor: Group 0 enabled
fiq_gicc_ctlr=0x00000001              CPU interface: enabled
fiq_igroupr0_before=0x00000000        intid 19 is in Group 0 - and so are intids 0-31
fiq_igroupr0_after_write=0x00000000   the write stuck
fiq_group_write_stuck=0x00000001
fiq_igroupr0_all_group1=0x00000000    NOT the read-only non-secure view
fiq_cntp_tval_written=0x00012c00      76800 ticks = 4 ms
fiq_cntp_ctl_after_arm=0x00000001     CNTP enabled, not masked
fiq_cpsr_after_unmask=0x60000113      F clear - FIQ unmasked
fiq_timer_fired=0x00000001            ISTATUS reached: the wait was long enough
fiq_probe_spin_iterations=0x00001a2c  6700 iterations to get there
fiq_probe_fiq_delivered=0x00000000
fiq_probe_failures=0x00000000         all three checks pass
```

and **no `exception: fiq` line anywhere in the log** — the vector was never entered.

## The arithmetic that makes it a result rather than a shrug

`fiq_timer_fired=1` is the check that gives the negative its meaning, and it is there because the
**first run of this probe was invalid**:

| run | spin | elapsed | timer interval | valid? |
| --- | --- | --- | --- | --- |
| 1 | 2 000 000 iterations of a bare `__asm__ volatile ("")` | ≈1.3 ms | 4 ms | **no** — the wait ended before the interrupt was due |
| 2 | 6700 iterations until `CNTP_CTL.ISTATUS` | ≈3.85 ms | 4 ms | yes |

Run 1 reported "no FIQ" and it meant nothing: the loop was shorter than the timer. The fix was to
bound the wait by the **timer's own condition** rather than by an iteration count, so it cannot be
too short whatever the core's clock is. This is the project's *"a measurement can be the thing that
is wrong"* defect, **eighth instance** — and it was caught by doing the arithmetic before writing the
result down, not by the run.

A second defect in run 1 is worth keeping for the same reason. The probe masked FIQ with `cpsid f`
and then read `CPSR` to confirm — and read F as still clear, reporting a failure. The reason is that
**the payload's own IRQ handler runs throughout and its return path is `subs pc, lr, #0`, which
restores the full CPSR including F from `SPSR_irq`.** Every IRQ arriving between the mask and the
read put F back. The readback now masks IRQ across those two instructions, which is what makes it
mean what it says.

## What the result says, and what it does not

**Says:** an interrupt that is in Group 0, with Group 0 enabled at the distributor and the CPU
interface, on a core with FIQ unmasked, **does not arrive as FIQ**. That is the vendor header's claim,
measured on this device for the first time.

**Does not say which of two mechanisms is responsible**, and the register state does not decide
between them:

- **The CPU interface is in the non-secure view**, where bit 0 of `GICC_CTLR` is `EnableGrp1` and
  Group 0 cannot be enabled at all — under which `GICD_IGROUPR` should read all ones. It reads
  `0x00000000`.
- **Or the distributor is in the secure view but the core cannot take FIQ** from this security
  state — under which `GICD_CTLR` bit 0 should read as `EnableGrp0` = 1 (it does) and `IGROUPR` = 0
  (it does).

The second is consistent with everything observed and the first is not, but `IGROUPR` being readable
and writable is exactly what a non-secure view is not supposed to allow, so neither is established
beyond doubt. What is established is the operational fact, which is what the shim needs.

**Operational consequence, and it is decisive for Phase 3:** a shim cannot supply a working
`tbd_fiq_handler` on this SoC. **The resolution is `__ARM_TIME__`** — the tree's complete IRQ path
(`Lexc_decirq_vector` → `fleh_decirq`, both real code), with the decrementer callbacks pointed at
CNTP, which is the option the spec's §6.2.1 identified as the candidate and which the payload has
already validated end to end (experiment-104: a 10 ms interval measured at 9961 µs through the
payload's own GIC IRQ path).

## Where that leaves the device side

The shim now has all three things `pe_arm_init_interrupts` does accounted for:

| | status |
| --- | --- |
| map step (`gSocPhys`, `gPicBase`, `gTimerBase`) | replaced and run on hardware (experiment-134) |
| board-class dispatch | replaced, and `would_return = 0` measured on hardware (experiment-134) |
| `tbd_ops` registration + timer | run on hardware (experiment-104) |
| **`tbd_fiq_handler`** | **measured unavailable on this SoC — the shim should not provide one** |

That is a design answer, not a gap, and it is the one the spec was waiting for.

## How to reproduce

```bash
cd stages/stage90
STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_MSM8974_FIQ_PROBE=1' ./build.sh
./preflight_boot_check.sh          # reports "FIQ availability probe: ON"
./run_and_capture.sh               # exit 0, device returns unattended
grep -a 'fiq_' /tmp/cancro-last_kmsg.txt
```

The switch defaults off, and a default `./build.sh` was run afterwards so the tree's committed state
is the one that boots without the probe.
