# Experiment 130 — two boot-path files investigated, neither fixed, and why

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/gen_device_headers.sh` (a recorded non-change)

| Configuration | result |
| --- | --- |
| `RELEASE` | 592 of 609 — **unchanged** |
| `STAGE90_BOOT` | 402 of 420 — **unchanged** |
| link | 454 undefined — **unchanged** |

No line of XNU's code was changed, and no number moved. This stage is two investigations that
produced a precise negative result each, recorded so the next attempt starts from where this one
stopped rather than from the same failure message.

## 1. `OSAtomicOperations.c` — XNU's own headers conflict, and the include is traced

`libkern/gen/OSAtomicOperations.c:33-36` is

```c
enum {
	false	= 0,
	true	= 1
};
```

and 15 of the boot closure's symbols depend on this file. It fails because `false` and `true` are
**macros** by the time that line is reached:

```
OSAtomicOperations.c:34:2: error: expected identifier
		false	= 0,
EXTERNAL_HEADERS/stdbool.h:36:15: note: expanded from macro 'false'
```

The include chain is established by preprocessing with the build's own command line:

```
libkern/gen/OSAtomicOperations.c
  -> kern/debug.h
    -> mach/vm_param.h:79          #include <os/overflow.h>
      -> libkern/os/overflow.h:45  #include <stdbool.h>
        -> EXTERNAL_HEADERS/stdbool.h:36   #define false 0
```

`libkern/os/overflow.h:45` is unconditional, and `mach/vm_param.h:79` is unconditional, and both
files are `standard`. So there is no `#if` to satisfy and no flag that changes it — and the
`-Dfalse=...`-shaped escape does not exist because `stdbool.h` defines them only under
`#ifndef __cplusplus`.

**And Apple compiles this file**: `libkern/conf/files:15` lists it `standard`, and
`libkern/conf/files.arm` adds nothing that excludes it. So Apple's build reached the same line
without the macros being defined, and **why is not established**. The plausible answers — an
`stdbool.h` ordered differently, a `vm_param.h` that does not reach `overflow.h`, or a `__cplusplus`
compile — are guesses, and this project's rule is that a guess does not go in a build script. Left
open with the chain written down.

## 2. `conf.c` — `NPTY 0` does not compile in 4570, and `pty` cannot be turned on

`bsd/dev/arm/conf.c:111` includes `<pty.h>` unconditionally and `NPTY` gates its pts/ptc externs.
The `loop.h` treatment was tried — generate `pty.h` with `#define NPTY 0`, which is consistent with
`bsd/kern/tty_pty.c` being `optional pty` and `pty` not being a set option (so that file is not
compiled either). Measured:

```
bsd/dev/arm/conf.c:189:36: error: use of undeclared identifier 'ptsselect'
```

and **`ptsselect` exists nowhere in the tree** — the `#else` branch of `conf.c` defines `ptcselect`
but not `ptsselect`. So Apple's `NPTY 0` path does not compile, at all, in 4570; it is a source
defect on a path their configuration never takes. `NPTY` must therefore be non-zero, and then
`bsd/kern/tty_pty.c` has to be compiled with it or the link reports `ptsopen`, `ptcopen`,
`ptyioctl` and their neighbours as undefined.

**That exposes a limit of `list_sources.py`, and it is the useful part.** `pty` is a *device*, not an
option. 4570 publishes no `device`/`pseudo-device` lines (experiment-127), so `optional pty` can
never match, and `pty` cannot be switched on through the configuration fragment the way
`MONOTONIC_BASE` was. Doing this properly means giving the tool a **device table** to read — the
same thing that would make `NLOOP` a fact instead of a choice. Left as a recorded next step rather
than half-implemented, because `NPTY 1` alone compiles `conf.c` and adds 7 undefined symbols at link
time, which is a move sideways presented as progress.

## What the two have in common, and what it means for the next stage

Both are the boot path's largest remaining items (15 and 7 of 42 symbols) and both need something
this project does not have:

- `OSAtomicOperations.c` needs to know **how Apple ordered those headers** — a fact about their
  build, not about the source.
- `conf.c` needs a **device table** — a file 4570 does not publish.

Neither is a value to choose, which is what every fix since experiment-119 has been. The remaining
boot-path list is now: 2 files needing facts from outside the tarball, and 9 smaller ones
(`bsd_init.c` 6, `subr_log.c` 4, `os/log.c` 3, `kern/task.c` 3, `uipc_mbuf.c` 2, `vm_object.c` 2 —
the last of which is the `const` member and is not fixable here at all, experiment-127).

## How to reproduce

```bash
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh                 # 592 of 609
./tools/link_xnu_arm.sh                           # 454 undefined
./tools/boot_closure.py                           # 11 files / 42 symbols
```
