# Experiment 168 — the second real XNU object ran on the device, and named `PE_init_platform`

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
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=PE_init_platform

No errors detected
```

**`cpu_data_init` is not the hit any more, and that is the measurement.** Experiment-159 stopped at
`stub_hit=cpu_data_init`; this image links `osfmk/arm/cpu.c`, so XNU's own `cpu_data_init` executed on
the hardware — filling in `BootCpuData` field by field, including the `cpu_pmap_cpu_data` tail and the
`cpu_asid_high_bits` loop (`cpu.c:332-401`) — and `arm_init` moved on. The next thing it asks for is
the next statement in `arm_init.c` after `:157`'s `cpu_data_init(&BootCpuData)`, which is `:159`'s
`PE_init_platform(FALSE, args)`. The prediction came from reading the source; the run confirmed it.

The device returned to Android on its own and reported no errors. Nothing was flashed:
`persistent_write_attempted = 0x00000000` in every contract that reports it, and the hardware watchdog
was the only net across the jump — the software dead-man cannot survive `_start` switching TTBR0/
TTBR1, which is why the gate says so explicitly.

## One object, chosen by the previous run

`stages/stage90/xnu_arm_boot/build_entry.sh` now links one more XNU object, with the same shape as the
four it already had (an `STAGE90_ENTRY_*_OBJ` override, a `require` that names the command to run if
the file is missing, and the same build that produced the measurement image):

```bash
ARM_CPU_OBJ=${STAGE90_ENTRY_CPU_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_cpu.o}
```

`cpu.c` is a good first object after `arm_init.c` because it is a **leaf**: `cpu_data_init` is
straight-line field initialization whose only references outside itself are the storage symbols
`ExceptionVectorsTable` and `RTClockData`, and this image already carries both. So it cannot pull the
closure in with it — which matters, because the closure of `arm_init` is the whole kernel
(experiment-158) and a rule like "add what looks relevant" would not stop anywhere.

| | exp-159 | now |
| --- | --- | --- |
| text | 13224 B | 17120 B |
| image | 82244 B | 98688 B |
| `.bss` | 0x00214000 – 0x00215648 (5704 B) | 0x00218008 – 0x00219788 (6016 B) |
| generated stubs | 43 (38 functions, 5 storage) | 86 (76 functions, 10 storage) |

The stub count nearly doubles because `cpu.o` brings its own undefined references — the image's stub
list is the *closure of `arm_init`*, so it grows as a frontier, not as a measure of how much is
missing from XNU. What is being measured on the device is **where the edge of this image is**, and
each run reports the next edge by name.

## The size limit exp-159 left open is now measured

exp-159 recorded "the thing to decide is how large the image can be". The limit is not a preference:
`topOfKernelData` is `0x00220000` (the payload sets it to `ENTRY_BASE + 0x20000`, and `xnu_entry_jump.c`
refuses to jump if the image's bss would reach past it), because that is where `_start` builds its own
page tables. So everything this image owns must end below it, and exceeding it is not a build error —
it is XNU's page tables landing on this image's data.

```
  bss          0x00218008 .. 0x00219788 (6016 bytes, zeroed by the payload)
  limit        0x00220000
  headroom     26760 bytes
```

`build_entry.sh` checks it (`ENTRY_DATA_LIMIT`), which is how the number above is quotable rather
than estimated. 26 KB is room for one to three more objects of `cpu.o`'s size, which is consistent
with the one-object-per-run rhythm — but `pexpert/arm/pe_init.o` is 40 KB, so the next step is
already close enough to the ceiling that the step after it will have to move the base or the limit
rather than merely add a file.

## What the next edge is, and why it is worth a run

`PE_init_platform(FALSE, args)` with `vm_initialized == FALSE` is:

```c
DTInit(PE_state.deviceTreeHead);
pe_identify_machine(boot_args_ptr);
```

followed by device-tree lookups for `target-type`, `model` and `/chosen`'s `debug-enabled`
(`pexpert/arm/pe_init.c:283-360`). `pexpert_arm_pe_init.o` is already compiled, is in **both**
manifests, and is 40460 bytes. Linking it would make **XNU's own device-tree reader run on the
hardware** against the tree this project built and validated
(`tools/xnu_dt_requirements.py`, which is what found that XNU silently skips cpu nodes without
`state` and needs a node named `arm-io`). That is the natural next run, with the size caveat above.

## Reproduce

```bash
# the entry image, with XNU's own objects
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 17120, image 98688, bss end 0x00219788, 86 stubs

# the payload embeds a *copy* of that image; the copy is the producer's
cp out/stage90/xnu_arm_entry_blob.c stages/stage90/xnu_arm_entry_blob.c
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)

# the gate, then the non-persistent boot, in one command (never flashes)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -3

# which object defines the next edge, and whether it is already built
grep -n 'PE_init_platform' out/stage90/xnu_arm_entry_undef.txt
ls -l out/xnu_kernel_obj/pexpert_arm_pe_init.o
sed -n '155,160p' external/xnu-4570.1.46/osfmk/arm/arm_init.c
```
