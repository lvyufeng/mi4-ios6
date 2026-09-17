# Experiment 123 — the link gap, and the force-include that was reaching nine files

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/link_gap.sh` (new), `stages/stage90/shims_arm/string.h`,
`tools/build_xnu_arm_kernel.sh`

| Configuration | before (experiment-122) | after |
| --- | --- | --- |
| `STAGE90_BOOT` | 381 of 419 | **389 of 419** |
| `RELEASE` | 499 of 587 | **507 of 587** |

And a second measurement, which is the point of the stage: **1187 symbols are referenced by every
compiled object and defined by none.**

## 1. The link gap, measured for the first time

Every stage so far has reported a *compile* count, which says nothing about what is missing: most of
the manifest's 587 files do not matter for any given symbol. The question that decides how far the
kernel is from linking is the other direction, and it needs no linker — `nm` on the objects answers
it exactly:

```
objects:                                        539
symbols defined:                              13682
symbols referenced, undefined:                 5407
MISSING (referenced by every, defined by none): 1187
  of which compiler runtime (libgcc/compiler-rt): 9
  of which a source file must provide:           1178
```

`tools/link_gap.sh` computes it and keeps the three lists in `out/link_gap/`.

The 9 are `__aeabi_*` — ARM EABI helpers for 64-bit division, soft-float conversion and bulk memory
operations. They come from `libgcc`/`compiler-rt` at link time and from nowhere in XNU, so they are
separated out rather than counted as missing source.

**Attribution is what makes the number a work list rather than a score.** Of the 1178, at least 182
are defined by a file that currently fails to compile, and the top of that list is:

| missing symbols defined here | file |
| --- | --- |
| 42 | `osfmk/vm/vm_pageout.c` |
| 16 | `osfmk/kern/task.c` |
| 16 | `osfmk/vm/vm_compressor.c` |
| 15 | `libkern/gen/OSAtomicOperations.c` |
| 9 | `osfmk/vm/vm_map.c` |
| 8 | `bsd/kern/bsd_init.c` |

with a long tail of `bsd/netinet/*` and `bsd/netinet6/*` behind it. That count is a **floor**: it
comes from matching definition-shaped lines at column 0, and much of XNU puts the return type on its
own line. The direction is right and the ordering is what matters.

## 2. A force-include was reaching nine files, and the reason was two typedefs

The zlib cluster was eight files failing on the same line:

```
libkern/zlib/zutil.h:194:18: error: typedef redefinition with different types
    ('long' vs 'typeof (((int *)0) - ((int *)0))' (aka 'int'))
    typedef long ptrdiff_t;
EXTERNAL_HEADERS/stddef.h:31:41: note: previous definition is here
```

`zutil.h:193-194` is `#if KERNEL` / `typedef long ptrdiff_t;` — in a file Apple compiles into the
ARM kernel. So **in Apple's build `ptrdiff_t` is not defined at that point.** Two things here were
defining it:

- `stages/stage90/shims_arm/string.h` included `<stddef.h>` to get `size_t`. It stands in for the
  kernel SDK's `<string.h>`, and by this evidence that header does not pull in `stddef.h`.
- `-include stdatomic.h` was in the build's force-include set, and
  `EXTERNAL_HEADERS/stdatomic.h:38` is `#include <stddef.h>`.

Both fixes are the same shape: **do not reach for the standard header to get `size_t`; take the
compiler's own builtin.** The shim now does

```c
#ifndef _SIZE_T
#define _SIZE_T
typedef __SIZE_TYPE__ size_t;
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif
```

and `-include stdatomic.h` is gone. **381 → 389 of 419, no regressions.**

Three things about that are worth recording rather than leaving in the diff:

- **Removing `stddef.h` without this took the build to 0 of 419.** `NULL` was being supplied by it
  as a side effect, and `osfmk/kern/kcdata.h` is the first file that notices. A header removed from
  one place is a dependency removed from everywhere it was reached.
- **`_SIZE_T` is claimed because two other headers claim it.** `EXTERNAL_HEADERS/stddef.h:28` and
  `osfmk/libsa/types.h:52` both guard `size_t` with `_SIZE_T`, and `libsa/types.h:54` says
  `unsigned long` — written for a Darwin target, where that is what `__SIZE_TYPE__` is. On
  `--target=armv7-none-eabi` the compiler says `unsigned int`, so left to race one of the two loses
  and four `osfmk/arm` files fail on the redefinition. Claiming the guard is what makes ours win.
- **`-include stdatomic.h` was invisible until the failures were attributed.** It was added for
  `enum memory_order` in the ARM layer's atomics, and nothing in the kernel build includes it by
  name — so its effect on libkern was a side effect nobody was looking at. The zlib failures had
  been on the list for several stages, described as "`z_off_t` in zlib", which is not what they are.

## 3. Measured and not adopted: the Darwin target triple

Since the two type mismatches come from XNU being written for `armv7-apple-darwin` while this build
uses `armv7-none-eabi`, the triple was measured directly:

| Target | `__SIZE_TYPE__` | Minimal | Linkable here |
| --- | --- | --- | --- |
| `armv7-none-eabi` | `unsigned int` | 389 | yes — GNU `ld`, ELF |
| `armv7-apple-darwin` | `long unsigned int` | 390 | **no** — no `ld64`/`lld` on this host |

One file better, and it produces Mach-O objects that nothing on this host can link. `/usr/lib/llvm-14/bin`
has `llvm-nm`, `llvm-size`, `llvm-objdump` and `llvm-ar` but no `ld64.lld`. So the Darwin triple is
the *right* target in principle — XNU is a Mach-O kernel — and it is not adopted until there is a
linker for it. That is a decision with the link step, not with the compile step, and it is written
down here so it is not rediscovered as a surprise.

## What is left

30 failures in the minimal configuration, 80 in `RELEASE`. `libkern/zlib` is entirely clear. The
remaining clusters are small: `clock_t` and `uid_t` in `bsd/sys/times.h` and `bsd/sys/kauth.h`,
`u_char`/`caddr_t` in `osfmk/device/subrs.c`, `osfmk/kern/btlog.c` and `osfmk/vm/vm_compressor.c`,
the `sync_qos_count_t` include-order question experiment-121 left open, and four genuinely absent
headers (`pty.h`, `loop.h`, `compat_43.h`, `sys/modctl.h`).

## How to reproduce

```bash
XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  MANIFEST=$PWD/out/xnu_arm_manifest_min.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_min_obj \
  ./tools/build_xnu_arm_kernel.sh          # 389 of 419
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh          # 507 of 587

./tools/build_xnu_arm_layer.sh             # 32 of 32, 445 undefined
./tools/link_gap.sh                        # 1187 missing, 9 of them compiler runtime
```
