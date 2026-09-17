# Experiment 151 — the force-includes were breaking a file, and the fix is per-file

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/shims_stdbool/` (new), `tools/build_xnu_arm_kernel.sh`

| | before (experiment-150) | after |
| --- | --- | --- |
| `RELEASE` | 607 of 615 | **608 of 615** |
| `STAGE90_BOOT` | 413 of 426 | **414 of 426** |
| stub symbols in the image | 240 | **224** |
| boot-path stubs | 66 | **51** |

## The finding: Apple force-includes nothing, and this build force-includes ten headers

`libkern/gen/OSAtomicOperations.c:33-36`:

```c
enum {
	false	= 0,
	true	= 1
};
```

It fails because `false` and `true` are **macros** by then, from `EXTERNAL_HEADERS/stdbool.h:36-37`.
The chain was traced through the preprocessed output rather than guessed:

```
mach/vm_param.h:79   (#ifdef KERNEL)
  -> libkern/os/overflow.h:45
    -> EXTERNAL_HEADERS/stdbool.h:36    #define false 0
```

and `mach/vm_param.h` is reached from `mach/thread_policy.h` → `thread_info.h` → `clock_types.h` →
`vm_region.h` → `dyld_kernel.h` — **a header this build force-includes for every file, because XNU's
`thread_t` has QoS policy members and Apple's build arranges that differently.**

**Apple's build force-includes nothing.** This project's ten-header force-include set exists to stand
in for what `MakeInc.*` would otherwise supply, and its cost had never been measured. Here it is: it
drags a header chain into 615 translation units that mostly do not include it, and one of them defines
`false` and `true` as an enum.

## The fix, and the measurement that chose its scope

A `stdbool.h` that defines `bool` but **not** the two macros, placed ahead of `EXTERNAL_HEADERS`.
Applied globally:

| scope | files compiling | newly passing | broken |
| --- | --- | --- | --- |
| every file | **530** of 615 | 1 | **78** |
| **this file only** | **608** of 615 | 1 | 0 |

**78 of the kernel's files write `true` or `false` and need the macros; one file defines an enum with
those names and needs them absent.** That is not a global setting and it is not a flag — it is a
per-file one, applied with the same mechanism `COMP_FIRST` and the component roots already use.
`tools/shims_stdbool/` holds the one header and its reason.

It is worth naming what this is: **an eleventh instance of the project's one-value-two-definitions
class, and the first where the duplicate is a *macro* rather than a symbol.** `false` means two things
in one translation unit, and which is right depends on the file.

## What it bought

`OSAtomicOperations.c` provides 15 of the boot path's stubs — `OSAddAtomic`, `OSCompareAndSwap`,
`OSIncrementAtomic`, `OSBitOrAtomic` and their neighbours, the atomic primitives the scheduler and
locks sit on. Measured:

```
stub symbols in the measurement image   240 -> 224
boot-path stubs                          66 ->  51
```

and the categories are now: **32 from files that fail to compile, 10 C++, 4 compiler runtime, 4 with
no source in the tree, 1 anomalous.**

## Where the host side now stands

**`RELEASE` 608 of 615.** The seven remaining, no two alike:

| file | why | can anything here fix it? |
| --- | --- | --- |
| `vm_object.c` | `const` member assigned | **no** — no flag, no shim, and no source edit by this project |
| `subr_prof.c` | malformed, identical upstream | **no** |
| `vnode_pager.c` | `size_t` — the target triple | no, and the Mach-O route is measured dead (experiment-150) |
| `subr_log.c`, `log.c` | `FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT` | no — a value with no evidence in the tarball |
| `if_bridge.c` | `NBPFILTER` | no — needs the `bpfilter` option and its dependencies |
| `kperfbsd.c` | `ffs`/`fls`, a file that straddles Mach and BSD | no — no flag set satisfies both views |

**Every one of the seven is now a stated limitation rather than an unfinished item**, and the same is
true of the boot path's remaining 51 stubs: 10 are C++ (a runtime this project has not built), 4 are
`__aeabi_*` from libgcc, and the rest are behind the seven files above.
