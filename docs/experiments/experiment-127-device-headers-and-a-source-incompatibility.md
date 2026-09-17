# Experiment 127 — the loopback header, and the first failure that is not a configuration gap

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/gen_device_headers.sh` (new), `tools/build_xnu_arm_kernel.sh`

| Configuration | before (experiment-126) | after |
| --- | --- | --- |
| `STAGE90_BOOT` | 400 of 419 | 400 of 419 |
| `RELEASE` | 565 of 587 | **569 of 587** |
| undefined after linking | 777 | **699** |

No line of XNU's code was changed.

## 1. A third generator: `config(8)`'s device headers

`bsd/kern/bsd_init.c:875` is `#include <loop.h>`, with `#if NLOOP > 0 / loopattach();` on the next
lines; `bsd/netinet6/ip6_input.c:154` includes it too with the comment "we need it for NLOOP", and
`in6_src.c` and `nd6.c` follow. Four files. The header is missing because **`config(8)` writes it**:
`pseudo-device loop` in a kernel configuration becomes `#define NLOOP 1` in `loop.h` — the same
mechanism that produces the `OPTIONS/` headers, from a different input.

And 4570 publishes **no `device` or `pseudo-device` lines at all** — `grep -c '^pseudo-device'
*/conf/files` is 0 in every component — so the tables that would say `NLOOP 1` are not in the
tarball, exactly as `MONOTONIC` was not (experiment-112). `NLOOP` is therefore a **choice**, and the
one recorded is 0, for a reason that makes it a decision rather than a guess:

- `bsd/net/if_loop.c` is `optional loop`, and `loop` is not a set option, so the file is not in the
  manifest and **nothing would provide `loopattach()`**. With `NLOOP 1` that is an undefined symbol
  at link time; with `NLOOP 0` it is not referenced.
- `#if NLOOP > 0` excludes the loopback attach path entirely — strictly less code, the same
  reasoning `MACH_ASSERT 0`, `ZONE_DEBUG 0` and the minimal configuration all use.

`tools/gen_device_headers.sh` writes it per configuration, alongside `gen_option_headers.py` and
`gen_libkern_version.sh`. **569 of 587, no regressions.**

## 2. And one failure that is not a configuration gap at all

`osfmk/vm/vm_object.c:355` is `*object = vm_object_template;` — a whole-struct assignment — and
`osfmk/vm/vm_object.h:174` declares one of its members `const`:

```c
const unsigned int	wired_page_count; /* number of wired pages
                                           use VM_OBJECT_WIRED_PAGE_UPDATE macros to update */
```

Assigning to a struct that has a `const` member is a constraint violation in C
(6.3.2.1p1, "modifiable lvalue"), and both compilers on this host reject it:

| | |
| --- | --- |
| `clang --target=armv7-none-eabi`, `-std=gnu89/gnu99/gnu11/gnu17` | error, all four |
| `arm-none-eabi-gcc -std=gnu99` | `error: assignment of read-only location '*p'` |
| `-Wno-error`, `-fms-extensions` | no change; the diagnostic carries no `[-Wflag]`, so it is not suppressible |

This is checked against the other XNU trees in `external/`, and it is specific to 4570:

| Tree | `wired_page_count` |
| --- | --- |
| `xnu-4570.1.46` (Darwin 17.0.0) | `const unsigned int` |
| `xnu-upstream` (12.3.0) | `unsigned int` |
| `xnu-2050.18.24`, `apple-xnu-rel-2050` | `unsigned int` |

So the `const` was introduced in the 4570 line, Apple built it, and a current compiler does not
accept the construct. The most likely explanation is that the clang Apple used in 2016 accepted it
and the diagnostic was tightened later — but that is an inference, not a measurement, and the honest
statement is that **this one file is not a configuration gap**: every other failure in this project's
list has been a missing value, a missing header or a scope error, and this is a source construct that
no flag, include order or macro on this host can make legal. It is left failing, and recorded,
rather than patched — the project's rule is that XNU's code is not modified.

It is 2 of the 62 symbols `boot_closure.py` attributes to the boot path, which is a fair measure of
how much it matters.

## What is left

19 failures in the minimal configuration, 18 in `RELEASE` — and `boot_closure.py` now puts only
**12 of them on the boot path, accounting for 62 symbols**:

| symbols | file |
| --- | --- |
| 17 | `osfmk/vm/vm_compressor.c` |
| 15 | `libkern/gen/OSAtomicOperations.c` |
| 7 | `bsd/dev/arm/conf.c` |
| 6 | `bsd/kern/bsd_init.c` |
| 4 | `bsd/kern/subr_log.c` |
| 3 | `libkern/os/log.c`, `osfmk/kern/task.c` |

The link is at **699 undefined symbols**, from 1187 when this measurement was first taken, and the
mechanisms that remain are individually identifiable rather than structural.

## How to reproduce

```bash
./tools/gen_device_headers.sh                      # NLOOP 0 -> out/xnu_device/<CONFIG>/loop.h
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh                  # 569 of 587
./tools/link_xnu_arm.sh                            # 699 undefined
./tools/boot_closure.py                            # 12 files / 62 symbols on the boot path
```
