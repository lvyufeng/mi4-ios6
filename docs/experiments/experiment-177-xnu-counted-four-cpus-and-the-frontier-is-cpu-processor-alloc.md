# Experiment 177 — XNU counted four CPUs, and the frontier is `cpu_processor_alloc`

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
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_avail_cpus=0x00000004
 stub_hit=cpu_processor_alloc

No errors detected
```

**XNU's own `ml_parse_cpu_topology` walked this project's `/cpus` node and counted 4 CPUs.**

`ml_parse_cpu_topology` (`osfmk/arm/machine_routines.c:455`) is `DTLookupEntry(NULL, "/cpus")`,
`DTInitEntryIterator`, `DTIterateEntries` over the children, `++avail_cpus` for each, then
`panic("No cpus found!")` if the count came out zero. `stage90_main.c:678-701` emits `/cpus` with
four `cpu@0..cpu@3` children. Four is what came back. So on hardware, in XNU's own ARM platform
code, with XNU's own page tables live: the lookup found the node, the iterator walked exactly its
four children, and the counter ended at 4 — not 0 (which would have been the `panic` path), and not
a number set by a property read.

This is the third time a value has been the result rather than a symbol name, and it is the one the
Phase 2 work has been aiming at since `tools/xnu_dt_requirements.py` predicted it from the source:
the `state` property, the `device_type`, and the node count are now all confirmed by XNU reading
the tree, on the device.

## The link said what to delete, again

`machine_routines.o` defines `ml_parse_cpu_topology` for real, and `entry_stubs.c` had hand-written
a probe by that name in exp-171. The link reported it before anything else happened:

```
machine_routines.o: in function `ml_parse_cpu_topology':
machine_routines.c:(.text+0x53c): multiple definition of `ml_parse_cpu_topology';
xnu_arm_entry_stubs.o:entry_stubs.c:(.text+0x3b8): first defined here
```

That is the mechanism `entry_stubs.c`'s own header describes — "Whatever this file still defines
that one of those objects also defines is a link error, not a silent override ... the link is what
says which ones had to go" — and it is the second time it has fired, after `DTInit` in exp-170.

The probe moved one edge on rather than disappearing: `cpu_processor_alloc` (`arm_init.c:234`) is
the first call after the count is known, so it now logs `ml_get_cpu_count()` — the accessor out of
`machine_routines.o` that returns the same `static avail_cpus` — and then names itself. That is
where `xnu_entry_avail_cpus=0x00000004` comes from.

The placement is safe for a reason worth stating: `cpu_processor_alloc` is also called from
`cpu.c:307`, inside `cpu_data_alloc`. exp-176's run reached `ml_parse_cpu_topology` while
`cpu_processor_alloc` was still a generated stub that had never fired, so no earlier call site
executes before `arm_init.c:234`. The proof is the previous run, not an inspection.

## The first object that is not small

Every object until now added three to five undefined symbols. `osfmk/arm/machine_routines.o` is
4135 bytes of text across **71 functions** and references **79** undefined symbols. Measured by
linking an empty object in its place and diffing the two sets:

```
resolved:  debug_boot_arg  machine_startup  ml_get_boot_cpu_number  ml_get_cpu_count
           ml_init_arm_debug_interface  ml_io_map  ml_io_map_wcomb  ml_parse_cpu_topology
           ml_static_vtop  ml_vtophys
added:     56 (__aeabi_uldivmod, cache_info, clock_config, io_map, kernel_bootstrap, panicDebugging,
           pmap_find_phys, machine_info, mem_stats' globals, the cpu_signal/processor_* family, ...)
```

105 − 10 + 56 = 151. exp-176 raised the question of whether "one object" is still the right unit
when the object named by the device is a 71-function file. The answer this run gives is yes, and
the reason is mechanical rather than aesthetic: the linker resolves an object's references whether
or not the referencing function ever runs, so *which* of the 56 the run reaches next is the
measurement — and the run still stopped at exactly one symbol, one edge past where it stopped
before. The image is nowhere near its limit: 1941112 bytes of headroom.

Two things in the new undefined set that looked like they might not be:

- **`mach_assert` is a data symbol, not a function.** The stub generator sized it `D 0x4`, which is
  right: with `MACH_ASSERT 0` in the generated `mach_assert.h`, `osfmk/kern/debug.c:171` defines
  `int mach_assert = 1` and `machine_routines.c:71` declares `extern int mach_assert`. The reloc
  the object carries against it is a `MOVW`/`MOVT` pair loading the address of an `int`, which is
  what a data stub provides. A function stub here would have been a branch into a zeroed word.
- **`__aeabi_uldivmod` is the compiler's**, not XNU's — a 64-bit division helper. It is a generated
  reporting stub, so if any reachable path needs it the run will name it rather than compute a
  wrong answer. Nothing on the path taken here does.

| | exp-176 | now |
| --- | --- | --- |
| XNU objects linked | 15 | 16 (`osfmk/arm/machine_routines.o`) |
| text | 62276 B | 67588 B |
| image | 132384 B | 148768 B |
| `.bss` | 0x00220378 – 0x00221e48 | 0x00224378 – 0x00226188 |
| boot_args offset | +143360 | +163840 |
| `topOfKernelData` | +2097152 | +2097152 |
| undefined | 104 (91 functions, 13 storage) | 151 (129 functions, 22 storage) |

`topOfKernelData` did not move even though the image grew 16 KB, because it is 1 MB aligned and the
image is still 1.9 MB below its old value — the derivation in exp-175 doing what it is for.

The `.bss` moved by exactly 0x4000: `machine_routines.o` carries `.data`, which the linker places
ahead of the bss, and the payload's `memset` of the bss range follows the header.

## What is next

The device named `cpu_processor_alloc`, defined in `osfmk/arm/cpu_common.c:472`. It is one of the
five functions `cpu_common.c` exports (`cpu_processor_alloc`, `cpu_processor_free`,
`cpu_processor_steal`, `cpu_processor_reclaim`, `cpu_to_processor`), so this is likely a small
object — the opposite of the one just linked. `arm_init.c:234` assigns its return to
`BootCpuData.cpu_processor`, and `:236` immediately dereferences that through
`CpuDataEntries[master_cpu]`, so the value it returns is a real dependency and not only a symbol.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 67588, image 148768, 151 undefined, 129 stubs

# the measurement that the object is large, and which 56 are new
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_MACHINE_ROUTINES_OBJ=/tmp/empty.o ./build_entry.sh)
comm -13 <(sort out/stage90/xnu_arm_entry_undef.txt) <(sort /tmp/undef_with_mr.txt)

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'avail_cpus\|stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -3

# the count, against the tree that produced it
sed -n '455,492p' external/xnu-4570.1.46/osfmk/arm/machine_routines.c
sed -n '677,702p' stages/stage90/stage90_main.c

# mach_assert is an int, which is why its stub is 4 bytes of data
cat /tmp/opthdr/mach_assert.h
grep -n 'int mach_assert' external/xnu-4570.1.46/osfmk/kern/debug.c
```
