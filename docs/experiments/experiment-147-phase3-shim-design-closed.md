# Experiment 147 — the Phase 3 shim's design, closed on hardware

Date: 2026-09-17
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured
Artifacts: `stages/stage90/xnu_msm8974_shim.c` (comments only — the image hash is unchanged)

**Result: 13 checks, 0 failures, the timer delivered through the shim's own `tbd_ops`, and the device
returned to Android 10 unattended.** No code changed — the build produced a byte-identical image
(`sha256 b3c1d57a…`), because this stage is the shim's *documentation* being brought into line with
what has been measured, and the run is the confirmation that it still holds.

## What changed, and why it is worth a stage

The shim's header has said since experiment-101:

> The FIQ question (spec section 6) is deliberately not resolved here: the shim drives the timer as an
> IRQ, and whether XNU's own FIQ vector path is usable on MSM8974 remains open.

**It is no longer open** (experiment-143). So the file now states decision 3 — *no FIQ handler, and
that is a measured answer* — with its evidence:

```
fiq_igroupr0_before=0x00000000    intid 19 is in Group 0
fiq_group_write_stuck=0x00000001  the write stuck - not a read-only non-secure view
fiq_gicd_ctlr=0x00000001          Group 0 enabled at the distributor
fiq_gicc_ctlr=0x00000001          and at the CPU interface
fiq_cpsr_after_unmask=0x60000113  F clear - FIQ unmasked
fiq_timer_fired=0x00000001        the timer measurably reached ISTATUS
fiq_probe_fiq_delivered=0x00000000
```

and states the resolution: **`__ARM_TIME__`** — the tree's complete IRQ path
(`Lexc_decirq_vector` → `fleh_decirq`) with the decrementer callbacks pointed at CNTP, which is the
decision the file already made and which the payload has driven end to end since experiment-104.

`tbd_fiq_handler` is still NULL. **The reason is what changed**: it was "unfinished, a later attempt
can register over this"; it is now "a handler there cannot be reached on this SoC". That is the
difference between a gap and a design, and it is the difference that matters to anyone reading the
file next.

## The run, and what it re-confirms

```
msm8974_map_failures=0x00000000              the map/dispatch replacement (experiment-134)
msm8974_shim_fiq_handler_registered=0x00000000   correct, and now for a measured reason
msm8974_shim_checks=0x0000000d              13 checks
msm8974_shim_failures=0x00000000
msm8974_shim_arm_delivered=0x00000001        the timer fired through tbd_ops
msm8974_shim_arm_disarmed=0x00000001
msm8974_shim_arm_elapsed_us=0x000026e9       9961 us against the 10000 asked for - 0.4%
kernel_entry returned success
```

## Where Phase 3 stands

`pe_arm_init_interrupts` does four things, and all four now have an answer with a hardware result
behind it:

| | status | evidence |
| --- | --- | --- |
| map step (`gSocPhys`, `gPicBase`, `gTimerBase`) | replaced, run | experiment-134 |
| board-class dispatch | replaced; `would_return = 0` measured | experiment-134 |
| `tbd_ops` registration + timer | run through XNU's own interface | experiment-104, re-run here |
| `tbd_fiq_handler` | **measured unavailable; resolution is `__ARM_TIME__`** | experiment-143 |

**What is not done, and it is not a design gap: XNU does not call any of it.** The payload calls these
functions, because XNU is not running. Wiring the shim in needs an XNU that reaches
`pe_arm_init_interrupts`, which is the host-side link — and the host side is at 607 of 615 with the
remaining eight individually distinct, two of them unfixable by any flag and two needing the
Mach-O-versus-ELF decision.

**So the two lines are now both at their respective limits and they do not touch.** That is the
honest state of the project, and it is where this stage ends rather than continuing to polish either.
