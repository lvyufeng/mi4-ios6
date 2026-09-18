# Experiment 173 — `PE_init_platform` reached its last statement, `pe_init_debug`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=pe_init_debug

No errors detected
```

## What the name says, and what it does not

`pe_init_debug()` is the **last statement** of `PE_init_platform`'s `!vm_initialized` block —
`pexpert/arm/pe_init.c:372`, three lines before the function's closing brace — and nothing else in
this image calls it (`grep -rn 'pe_init_debug(' ` over the tree finds `pexpert/arm/pe_init.c:373`
and the i386 file). So this line says the whole function ran:

- `pe_identify_machine` returned, which means the loop over `/cpus` terminated. That is the first
  time a piece of XNU's platform identification has run to completion on this device.
- The six device-tree lookups after it all ran: `target-type` and `model` on the `device-tree`
  node, then `debug-enabled`, `firmware-version`, `unique-chip-id` and `dram-vendor-id` on
  `/chosen`. Each is guarded by `kSuccess ==`, so a property this project's tree does not have was
  simply skipped — **the log does not prove any of the six is present**, only that they were looked
  for and that the lookups did not fault.

What the name does *not* say: whether the cpu node's `state` matched `"running"`. The comparison at
`pe_identify_machine.c:117` now uses the real `strncmp`, and both outcomes — match, or no match and
the loop continues to the next child — end with the loop exiting and the function returning. The
frequency values the tree was asked for are the one thing this run cannot see, which is why the
probe below matters.

Two absence-of-failure facts are still worth having. No exception vector fired: every offset the
reader computed into this project's tree was a valid address. And no `kalloc_canblock` stub fired:
in 4570's `device_tree.c` the only `kalloc` is `DTEnterEntry`'s `DTSavedScope` push at `:290`, so
the tree was walked with path lookups and a flat iterator, never by descending into a nested
iterator.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## What changed in the image

| | exp-172 | now |
| --- | --- | --- |
| XNU objects linked | 11 | 12 (`osfmk/arm/strncmp.s`) |
| text | 59492 B | 59684 B |
| image | 131608 B | 131608 B |
| `.bss` | 0x00220070 – 0x00221e80 | unchanged |
| undefined | 106 (92 functions, 14 storage) | 105 (91 functions, 14 storage) |

`strncmp.o` is the clean kind of step: 224 bytes of text, defines `strncmp` and four local labels,
and references nothing at all — `nm -u` on it is empty — so the count moves by exactly the one
symbol the last run named.

## The next object, and the probe that is still waiting

`pe_init_debug` is defined by `pexpert/gen/pe_gen.c` (`out/xnu_kernel_obj/pexpert_gen_pe_gen.o`,
184 bytes for the function). Linking it removes the last call between `PE_init_platform` and its
return, which is what the probe added in exp-171 has been waiting for: `ml_parse_cpu_topology`, the
statement `arm_init.c:217` executes next, reporting `pe_arm_get_soc_base_phys()` — the `ranges[1]`
that XNU's own reader took out of the `arm-io` node this project built.

That object is worth more than one symbol. `pe_gen.c` also defines `PE_putc`, `PE_init_printf`,
`PE_enter_debugger`, `PE_i_can_has_kernel_configuration` and `PE_get_random_seed`, all of which are
stubs in this image today, and it is the same source the *payload* links as `out/stage90/xnu-objects/pe_gen.o`
behind `STAGE90_XNU_REAL_DT`. XNU's console path is a later experiment's problem
(`arm_init.c:337`, `PE_init_kprintf(FALSE)`), but the object that owns half of it arrives here.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 59684, image 131608, 105 undefined, 91 stubs

cp out/stage90/xnu_arm_entry_blob.c stages/stage90/xnu_arm_entry_blob.c
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|soc_base_phys\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -3

# why the last statement of PE_init_platform, and who else could have called it
grep -rn 'pe_init_debug(' external/xnu-4570.1.46/pexpert/ | grep -v pe_init_debug_command
sed -n '368,376p' external/xnu-4570.1.46/pexpert/arm/pe_init.c

# the only allocation in the reader, and the function that reaches it
grep -n 'kalloc\|kfree' external/xnu-4570.1.46/pexpert/gen/device_tree.c
```
