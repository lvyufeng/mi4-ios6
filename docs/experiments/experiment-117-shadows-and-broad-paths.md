# Experiment 117 — two shims were shadowing real headers, and a broad include path cost four files

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `stages/stage90/shims_arm/` (two shims deleted), `tools/build_xnu_arm_kernel.sh`

## The pattern again, in the place it is most expensive

`experiment-108` and `-115` both recorded that "the XNU security framework is not in the OSS
tarball" and wrote shims accordingly:

| Shim written | What the tarball actually has |
| --- | --- |
| `shims_arm/security/_label.h` | `security/_label.h` |
| `shims_arm/san/kasan.h` | `san/kasan.h` |

Both exist, at the **tree root** rather than under `osfmk/`, and a search of `osfmk/` for them
returns nothing — which is how the conclusion was reached. This is the **fourth** time in this
project that "not available" meant "looked in one directory":

1. MIG — published at `apple-oss-distributions/bootstrap_cmds`.
2. The generated mach headers — a build step away, not a wall.
3. The build configuration — `config/MASTER.arm` and `SETUP/config/`, at the root all along.
4. `security/` and `san/` — also at the root.

An audit of every shim against the tree confirms the rest are genuinely absent
(`mach_counters.h`, `xpr_debug.h`, `mach_pagemap.h`, `task_swapper.h`, `mach_kdp.h`,
`mach_ldebug.h`, `zone_debug.h`, `TargetConditionals.h`), and that `mach_debug.h` and `string.h` are
correct redirects rather than shadows. So the two deleted here are the exception, and the audit is
now written down rather than repeated.

Adding `-I$XNU` (the tree root) makes both real headers resolve. **196 of 401** in the minimal
configuration, up from 191.

## A broad include path is not the same as an exported one

Reaching for `-I$XNU/osfmk/libsa` looked free: `libsa` is a real component in Apple's
`COMPONENT_LIST`, and its `types.h` defines `uint_t`, `u_int`, `u_long` and `caddr_t` — values this
build had been supplying by hand.

It cost four files: **191 → 187**. The reason is the one this project keeps meeting from the other
side. `osfmk/libsa/` also holds `string.h`, `stdlib.h` and a `sys/` subdirectory, all written for
the *bootloader* context, and putting the directory on the include path lets them shadow the real
ones.

Apple's build does not do that. `makedefs/MakeInc.def:463-469` points `INCFLAGS_IMPORT` at
`$(OBJROOT)/EXPORT_HDRS/$(COMPONENT)`, which is populated from each directory's `EXPORT_MI_LIST` in
its `Makefile` — a **selected list**, copied flat into a per-component root. Exposing the whole
source directory is a different and worse thing, and the difference is measurable: 4 files.

Dropping it returns 196. The component order used is Apple's (`MakeInc.def:46`: `osfmk bsd libkern
iokit pexpert libsa security san`), with the note in the script that a single build cannot vary that
per file the way the real mechanism does.

## Corrected numbers

| Configuration | files tried | compile | fail | timed out |
| --- | --- | --- | --- | --- |
| `RELEASE` | 569 | **204** | 364 | 1 |
| `STAGE90_BOOT` (minimal) | 401 | **196** | 205 | 0 |

Both configurations improved — 197→204 and 191→196 — and both improvements come from *deleting*
two hand-written files and *not* adding a directory to a search path. Neither is a code change to
XNU.

## What remains, and it is now a small, named list

```
copyinstr    — osfmk/kern/misc_protos.h vs bsd/libkern/libkern.h, genuinely different signatures
uthread_t    — bsd/sys/user.h, not reached by files that need it
ORDINARY / struct tty   — bsd/kern/tty.c and neighbours
fls, ffs     — libkern.h vs osfmk's own
```

These are view collisions between the Mach side and the BSD side, and they are the shape Apple's
**exported header set** exists to resolve: `EXPORT_HDRS` gives each component a filtered view of the
others rather than the whole tree. Reproducing that faithfully means reading each directory's
`EXPORT_MI_LIST` and building the per-component roots — which is a mechanical step, and it is the
next one.

**XNU still does not run.** No OS entered, no driver running; nothing here touches the device.
