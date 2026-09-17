# Experiment 100 — the first public-XNU code to execute on the device

Date: 2026-09-17
Commit under test: `fd02d31`, plus the changes described below
Build switch: `STAGE90_XNU_REAL_DT = 1` (default off)
Other switches: `HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Captures: `/tmp/kmsg-realdt1.txt` (first attempt), `/tmp/kmsg-realdt2.txt` (the shipped build)

## The milestone

**`pexpert/gen/device_tree.c` — Apple's own source, compiled from the tarball and linked into the
payload unmodified — ran on MSM8974 and walked the device tree this project built.**

```
stage90 xnu_real_dt: running XNU's own pexpert/gen/device_tree.c on this tree
xnu_real_dt_status=0x90000001        xnu_real_dt_checks=5   xnu_real_dt_failures=0
xnu_real_dt_root_lookup_ok=0x00000001
xnu_real_dt_lookup_cpus_ok=0x00000001
xnu_real_dt_cpu_children=0x00000004
xnu_real_dt_cpu0_timebase_hz=0x0124f800        19200000 - the correct value
xnu_real_dt_cpu0_state_is_running=0x00000001
xnu_real_dt_cpu_state_running_count=0x00000004
xnu_real_dt_find_name_arm_io_ok=0x00000001
xnu_real_dt_arm_io_device_type_len=0x00000004
xnu_real_dt_arm_io_ranges_len=0x0000000c
xnu_real_dt_arm_io_ranges0=0x00000000
xnu_real_dt_arm_io_ranges1_soc_phys=0xf9000000
xnu_real_dt_find_device_type_timer_ok=0x00000001
xnu_real_dt_timer_reg_len=0x00000018
xnu_real_dt_timer_reg0=0xf9020000
xnu_real_dt_timer_reg1=0x00001000
xnu_real_dt_xnu_timer_map_addr=0xf2020000
stage90 xnu_real_dt: XNU's device-tree code walked this tree and agreed
kernel_entry returned success
```

Every claim this project has made about the tree is now checked by XNU's own reader rather than
by a Stage-owned one. `/cpus` is found by path, its four children are enumerated with
`DTCreateEntryIterator`/`DTIterateEntries`, each `state` is read and compared to `"running"`, and
cpu0's `timebase-frequency` comes back as 19200000 through `DTGetProperty`. `/arm-io` is found by
**property value** (`DTFindEntry("name", "arm-io")`), a different code path from the path walk.
`/timer` is found by `DTFindEntry("device_type", "timer")`.

This is the first time any public-XNU object has executed on this device. The payload's own notes
said so repeatedly — "None of the public-XNU objects are linked into or executed by the booted
StageNN payload" — and that sentence is now false in a controlled, switchable way.

## Why the device-tree layer first, of the five objects

Three reasons, and they are why this was worth doing before any of the harder ones:

1. **It is on the real path.** `pe_identify_machine.c` and `pe_init.c` call exactly these entry
   points; everything else about the platform bring-up reads the tree through them.
2. **Its dependencies are tiny.** `device_tree.o` needs `kalloc`, `kfree` and `strcmp` — and the
   project's `xnu_object_shims.c` already provided the first two, in a file that had been
   compiled and never linked for fifteen stages.
3. **It can be checked.** The tree it walks is one this payload built, so every value XNU's code
   returns can be compared against the value that was put in. `timebase-frequency = 19200000` and
   four `state = "running"` CPUs are correct answers, not merely plausible ones.

## The reg-model gap is now a measured number

The last line is the one to keep. `pe_arm_map_interrupt_controller` (pe_identify_machine.c:554)
computes:

```c
gTimerBase = ml_io_map(soc_phys + *reg_prop, *(reg_prop + 1));
```

where `soc_phys` is `*(ranges_prop + 1)` from `/arm-io` — that is `0xf9000000` here — and
`*reg_prop` is the timer's `reg[0]`, `0xf9020000` here. So the probe evaluates what XNU's own
arithmetic produces:

```
0xf9000000 + 0xf9020000 = 0x1_f2020000  ->  0xf2020000 in a 32-bit register
```

**XNU would map the timer at physical `0xf2020000`, which is not the timer.** The timer is at
`0xf9020000`. What the project's notes called "the `reg` model" — Apple's code wants `reg` as an
*offset* from the SoC base while our tree carries absolute addresses — is now a concrete,
reproducible number produced by Apple's own code path rather than an argument from reading.

That does not change the Phase 3 plan, which was already "replace `pe_arm_init_interrupts`" for
an independent reason (`pe_arm_init_timer` is a chain of Apple board-class `#if`s with `return 0`
as the fallthrough, so it fails on MSM8974 whatever the tree says). It makes the second reason
concrete, and it means the fix has a number attached to check against.

## A probe bug this found, of the project's most familiar shape

The first run (`/tmp/kmsg-realdt1.txt`) reported `failures=1` with
`arm_io_device_type_ok=0x00000000` — the probe had asserted that `/arm-io`'s `device_type`
equals `"arm-io"`. **XNU does not check that.** `pe_identify_machine.c:234` copies the string
into `gPESoCDeviceTypeBuffer` and that is all; our tree says `"soc"` and XNU's own code is
perfectly happy with it.

So the failure was the probe's, not the tree's: an assertion stated rather than read off the code
it was testing. It is the same shape as `exclusive_probe.c`'s discriminator (experiment-96) and
the three literal-duplication defects — an assumption about what the other side checks, never
verified against the other side. The probe now checks "present and non-empty", which is what XNU
does, and separately reports the string's length so the log still shows what is there.

Worth noting where the fix came from: `sed -n '228,250p'` of `pe_identify_machine.c`. Reading
the consumer, not the tree.

## What changed

- **`xnu_real_dt.c`** (new): the probe. Five checks, each on the same entry points
  `pe_identify_machine` uses — root, `/cpus` with the child iteration and the `state` filter,
  `DTFindEntry("name", "arm-io")` with its `ranges`, `DTFindEntry("device_type", "timer")` with
  its `reg`, and `/chosen`'s `memory-map` (absent, which `pe_init.c` accepts behind a `kSuccess`
  check). It evaluates XNU's own `soc_phys + reg[0]` and reports the result.
- **`stage90.h`**: `STAGE90_XNU_REAL_DT` (default off) and the result struct.
- **`build.sh`**: two include paths (`external/xnu-upstream/pexpert` for the device-tree header,
  `shims/` for the one header it pulls in) — deliberately not the whole XNU tree, so the payload
  cannot accidentally start resolving its own includes against XNU's; and the link step now takes
  `xnu-objects/xnu_object_shims.o` and `xnu-objects/device_tree.o` when the switch is on. Until
  this commit, *nothing* from `xnu-objects/` was linked into the payload at all.
- **`xnu_kernel.c`**: the call site, placed **after** the payload's own DT checks rather than
  before them, so that if the two ever disagree the log shows both verdicts and the boot still
  depends on the Stage-owned one. Non-fatal, like the other probes: this is evidence about a
  boundary, not a precondition for a boot.

## Extended: three of the five objects, all seven checks green

The same switch now links `xnu-objects/{xnu_object_shims,device_tree,bootargs,pe_gen,
arm_pe_bootargs}.o` — every public-XNU object this project compiles — and runs three of them:

```
xnu_real_dt_checks=7   xnu_real_dt_failures=0   xnu_real_dt_status=0x90000001
xnu_real_dt_pe_boot_args_ok=0x00000001
xnu_real_dt_parse_stage_arg_found=0x00000001   parse_stage_arg_value=0x00000053   (83)
xnu_real_dt_parse_debug_arg_found=0x00000001   parse_debug_arg_value=0x00000144
xnu_real_dt_parse_absent_arg_found=0x00000000
stage90 xnu_real_dt: XNU's device-tree code walked this tree and agreed
```

- **`arm_pe_bootargs.c` executes.** `PE_boot_args()` — three lines of Apple ARM source, returning
  `((boot_args *)PE_state.bootArgs)->CommandLine` — is pointed at the payload's own `boot_args` and
  returns the payload's own command line, byte for byte. That is XNU's ARM code reading a
  structure this payload built, through the layout Apple declares.
- **`bootargs.c` executes.** `PE_parse_boot_argn` parses the real command line and finds
  `mi4ios6.stage=83` (as the number 83) and `debug=0x144`, and correctly reports an invented name
  as absent. The negative case is the one that matters: a parser returning "found" for everything
  would be indistinguishable from one that works.

Two more probe bugs surfaced on the way to this, both the same shape as the first:

- It asked `PE_parse_boot_argn` for `"mi4ios6"` and expected a hit. The parser matches a token's
  **whole name** — `strncmp(args, arg_string, i) || (i != strlen(arg_string))` — and the token is
  `mi4ios6.stage`, so the parser was right and the probe was wrong. The comment in the source now
  says where the rule was read from.
- `shims/pexpert/boot.h` defined `struct boot_args` a second time, field-for-field identical to
  `stage90.h`'s, and both become visible in one translation unit the moment a file includes
  both. `BOOT_LINE_LENGTH` was duplicated too, as `256` against `256u`. The shim now defers to
  `stage90.h` when `STAGE90_BOOT_ARGS_DEFINED` is set — the same one-definition remedy as the
  device-tree child count, the descriptor literals and the cache policy.

Also found, and left alone deliberately: the payload's own command line says `mi4ios6.stage=83`,
which is stale by seven stages. It is a claim about the build, and worth fixing — but it is
asserted by the `command_line_stage90_marker` checks in `pe_init_platform_false`, so changing it
is its own small change rather than a drive-by.

## What this does not establish

- **XNU has not been entered.** No jump, no `arm_init`, no `start.s`. One subsystem runs in the
  payload's own context. The Stage-owned `_start`/`arm_init`/`arm_vm_init` symbols are still
  Stage-owned.
- **Only `device_tree.o` of the five objects is linked.** `bootargs.o`, `pe_gen.o`,
  `arm_pe_bootargs.o` and `arm_pe_consistent_debug.o` are still compiled-and-unlinked. Their
  symbol needs are known and small (`PE_state`, `PE_parse_boot_argn`, `Debugger`, `cnputc`,
  `vcattach`, `ml_map_high_window`, `OSCompareAndSwap64`) and `xnu_object_shims.c` already
  provides all of them.
- **The tree is one we built**, so this validates XNU's reader against our writer, not against a
  real iBoot tree.
