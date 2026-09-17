# Experiment 110 — XNU's entire ARM layer compiles

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/build_xnu_arm_layer.sh`, eleven new headers in `shims_arm/`

## The result

```
$ ./tools/build_xnu_arm_layer.sh --undefined 12
osfmk/arm: 32 of 32 compile to objects
text: 118982 bytes across 32 objects

== what it still needs to link ==
443 distinct undefined symbols
```

**Every `.c` file in XNU's `osfmk/arm` compiles to a real object.** That is the layer containing
`arm_init.c` (what `_start` branches to), `arm_vm_init.c`, `pmap.c`, `machine_routines.c`,
`locks_arm.c` and `trap.c` — the bring-up path and the pmap. It was **3 of 32** three turns ago.

## The whole difference was configuration

Not one line of XNU's code was changed. The entire movement from 3 of 32 to 32 of 32 is the flag
set in `tools/build_xnu_arm_layer.sh`, and the shape of the work was exactly what
`experiment-108` predicted after MIG came down: *choosing values*.

The steps that mattered, in order of leverage:

| Value | What it unlocked |
| --- | --- |
| `-DXNU_KERNEL_PRIVATE=1` | `vm_tag_t` — 35→10 errors in `arm_init.c` alone |
| `-DMACH_KERNEL=1` | `pmap.c`, `trap.c`, `locks_arm.c`, `status.c`. **Silent when wrong**: `kern/xpr.h:83` takes its *userland* branch without it |
| `-DMACH_BSD=1` | `bsd_info`/`uthread`, which several ARM files read |
| `-DKPC=1 -DMONOTONIC=1` | `kpc_arm.c`, `monotonic_arm.c` — forced, not preferred: those files are *in* this layer and read the members the guards gate |
| `-DXPR_DEBUG=0 -DLOCK_PRIVATE=1` | avoids `xpr_debug.h`; unlocks `LCK_MTX_THREAD_MASK` |
| `-DCONFIG_SCHED_TIMESHARE_CORE=1 -DCONFIG_SCHED_TRADITIONAL=1` | `struct run_queue` |
| `-mfpu=neon-vfpv4 -mfloat-abi=softfp` | `machine_cpuid.c`'s VFP identification register reads — a *codegen* error, invisible to a syntax-only check |

That last row is worth naming: `machine_cpuid.c` passed `-fsyntax-only` and failed to produce an
object, so a sweep that only parses would have reported 32 of 32 while the build was 31 of 32. The
script now builds objects rather than parsing.

## Eleven headers the build supplies and the tarball does not

Every one is recorded with what it is and why an empty or minimal version is the accurate stand-in,
each established by checking the consumer rather than assuming:

| Header | Kind |
| --- | --- |
| `mach_counters.h` | build-generated from a `.counts` file; `counters.h` types everything itself |
| `xpr_debug.h` | build-generated, and **unavoidable** — `xpr.h:83` includes it unconditionally under `MACH_KERNEL` |
| `chud/arm/chud_xnu_private.h` | `chud/i386/` ships and `chud/arm/` does not; written from the i386 file, and `chudcpu_data_t` deliberately **not** copied because ARM's `cpu_data` has a `void *cpu_chud` where i386's has the struct inline |
| `sys/syscall.h` | absent entirely; declared from what the ARM file and the i386 tree agree on |
| `System/mach/{resource_monitors,clock_types}.h` | path shims — the sources use the SDK name, the headers are `osfmk/mach/` |
| `mach_pagemap.h`, `task_swapper.h`, `mach_kdp.h`, `mach_ldebug.h`, `zone_debug.h`, `san/kasan.h`, `security/_label.h` | absent; empty because nothing on these paths uses a symbol from them |
| `mi4ios6_build_config.h` | not a stand-in: the declarations *this configuration* supplies (`uint_t`) |

And one genuine generator, from `experiment-108`: 24 MIG headers from Apple's own `.defs`.

## What this does and does not mean

**Does:** the ARM layer compiles as a unit, reproducibly, against the real 4570 tree, with a flag
set that is documented and a script that re-runs it. The pmap — the thing every later phase depends
on — is in that 32.

**Does not:** link, and does not run. **443 distinct undefined symbols**, and most of them live
outside `osfmk/arm`: the kernel proper (`kern/`, `vm/` minus the ARM-specific parts), `libkern`,
`bsd`, and compiler runtime (`__aeabi_memcpy4`, `__aeabi_uldivmod`). Compiling is the first of three
steps and the script says so rather than letting a green run imply more.

The honest position: **the ARM layer compiles; XNU does not link, does not build, and does not
run.** What changed is that the remaining work has moved from "obtain a generator and choose values"
to "compile more of the tree and satisfy a link" — both ordinary, mechanical, and measurable.
