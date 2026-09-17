# Experiment 134 — the replacement for `pe_arm_init_interrupts`, on hardware

Date: 2026-09-17
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured
Artifacts: `stages/stage90/xnu_msm8974_shim.c`, `stages/stage90/stage90.h`

**Result: the map/dispatch half of the replacement runs on the device, 5 checks, 0 failures, and
the log carries the number that justifies replacing the function at all.**

This is the device-side step the last two experiments pointed at: the shim already registered
`tbd_ops` and drove the timer (experiment-104), and what it had never done was the *other* half of
`pe_arm_init_interrupts` — the map step and the board-class dispatch.

## What the stock function does, and which part cannot work here

`pexpert/arm/pe_identify_machine.c:528-566`:

```c
gSocPhys = pe_arm_get_soc_base_phys();                     /* /arm-io ranges[1] */
gPicBase  = ml_io_map(soc_phys + reg[0], reg[1]);          /* interrupt-controller = "master" */
gTimerBase = ml_io_map(soc_phys + reg[0], reg[1]);         /* device_type = "timer" */
...
return pe_arm_init_timer(args);                            /* board-class dispatch */
```

- **Step 1 is a fact about the device tree and is reproduced exactly.** `/arm-io` carries
  `ranges = {0, 0xf9000000, 0x07000000}`, so `gSocPhys = 0xf9000000`.
- **Steps 2 and 3 are where the `reg` model bites.** This tree's `reg` is an **absolute** address,
  so Apple's `soc_phys + reg[0]` adds the SoC base to an address that already contains it.
- **`pe_arm_init_timer` is a closed set of Apple board classes** with `return 0` as the
  fallthrough, so it fails for any MSM8974 device type no matter what the tree says.

## What ran, and what it reported

```
stage90 xnu_msm8974_shim: map/dispatch - replacing pe_arm_init_interrupts
msm8974_map_soc_phys=0xf9000000
msm8974_map_pic_base=0xf9000000
msm8974_map_timer_base=0xf9020000
msm8974_map_apple_pic_base=0xf2000000
msm8974_map_apple_timer_base=0xf2020000
msm8974_map_dispatch_would_return=0x00000000
msm8974_map_checks=0x00000005
msm8974_map_failures=0x00000000
stage90 xnu_msm8974_shim: map/dispatch ok - bases computed directly, dispatch replaced
msm8974_shim_registered=0x00000001
msm8974_shim_checks=0x0000000d
msm8974_shim_failures=0x00000000
```

Four things worth reading out of that, and the middle two are the point:

- `map_soc_phys = 0xf9000000` — step 1 reproduced, and the value is in the log so a tree edit that
  moved it would be visible rather than silent.
- **`map_apple_pic_base = 0xf2000000`, `map_apple_timer_base = 0xf2020000`** — what the *stock*
  formula produces from this tree. Neither is where the hardware is. The GIC is at `0xf9000000` and
  the timer at `0xf9020000`, which are the two numbers directly above them. **This is the
  replacement's justification as a pair of measured values rather than a paragraph**; experiment-100
  measured the same gap through XNU's own device-tree code, and this is the same gap from the other
  side.
- **`map_dispatch_would_return = 0`** — spec §1's claim ("there is no configuration in which the
  stock function succeeds") evaluated on the device for this device type.
- Checks and failures folded into the shim's single pair of totals: **13 checks, 0 failures**, which
  is the 8 the registration half already had plus these 5.

Both recovery nets behaved as before; the device returned to Android 10 unattended.

## What this does and does not establish

**Does:** the MSM8974 address computation is correct on hardware, the replacement is exercised as a
unit, and the reason for replacing Apple's function is now a logged number.

**Does not:** it is not *called by XNU*. `xnu_msm8974_map_platform()` runs because the payload calls
it, exactly as the `tbd_ops` registration does — XNU is not running, so nothing in XNU's boot path
reaches this code. What it establishes is that the replacement is **correct and executable**, which
is the precondition for wiring it in; wiring it in needs an XNU that reaches
`pe_arm_init_interrupts`, which is the host-side link.

The two lines are still separate, and this stage moved the device-side one. The remaining device-side
work is the third thing `pe_arm_init_interrupts` does that is still not replaced: `pe_arm_init_timer`
supplies a `tbd_fiq_handler`, and the shim deliberately leaves that NULL (spec §2.3 — FIQ is the live
path on this build, and the payload has only ever driven IRQ).

## How to reproduce

```bash
cd stages/stage90
STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_MSM8974_SHIM=1' ./build.sh
./preflight_boot_check.sh          # reports "MSM8974 platform shim (Phase 3): ON"
./run_and_capture.sh               # exit 0, device returns unattended
grep -a 'msm8974_map_\|msm8974_shim_failures' /tmp/cancro-last_kmsg.txt
```

The switch defaults off, and a default `./build.sh` was run afterwards so the tree's committed state
is the one that boots without the shim.
