# Experiment 132 — the pty device, resolved by measurement; and one value left with no evidence

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/xnu_config/device_table.py`, `tools/gen_device_headers.sh`,
`tools/build_xnu_arm_kernel.sh`, `stages/stage90/shims_arm/os/firehose_buffer_private.h` (new)

| Configuration | before (experiment-131) | after |
| --- | --- | --- |
| `STAGE90_BOOT` | 405 of 423 | **406 of 426** |
| `RELEASE` | 595 of 612 | **599 of 615** |
| undefined after linking | 444 | **449** |
| boot-path work list | 11 files / 42 symbols | **10 files / 33 symbols** |

**The link got five symbols worse and the boot path nine better, and both are the same change.**
That trade is the honest headline of this stage, and it is explained at the end rather than buried.

## 1. What `pty` needed, and why 0 was not an option

Experiment-130 established that `NPTY 0` does not compile: `bsd/dev/arm/conf.c:112` is
`#if NPTY > 0` and its `#else` branch defines `ptcselect` but **not** `ptsselect`, which exists
nowhere in the tree. So the device has to be on — and "on" means four things together, which is what
makes it more than a one-line value:

| | |
| --- | --- |
| `device_table.py` | `pty: 1`, `ptmx: 1` — so `list_sources.py` selects the files |
| `gen_device_headers.sh` | writes `pty.h` with `NPTY 1` and `ptmx.h` with `NPTMX 1` |
| `build_xnu_arm_kernel.sh` | defines `-DNPTY=1 -DNPTMX=1`, so the preprocessor agrees |
| the manifest | picks up `tty_pty.c optional pty`, `tty_ptmx.c optional ptmx`, `tty_dev.c optional ptmx pty` |

`NPTY 1` rather than something larger because the source says so:
`bsd/kern/tty_pty.c:89-92` is `#if NPTY == 1 / #undef NPTY / #define NPTY 32` with a `#warning`. One
is what that code expects.

**Result: `conf.c` compiles**, and it is the file that *defines* `bdevsw`, `cdevsw`, `cdevsw_flags`,
`chrtoblk`, `nblkdev` and `nchrdev` — the device-switch tables the whole BSD device layer needs.
Nine undefined symbols gone.

## 2. Two defects in the tool that was supposed to prevent defects

Both found by running it against a case that should have passed, which is the only way a checker
gets tested:

- **It read the name and not the value.** `-DXPR_DEBUG=0` mentions `xpr_debug` and turns the feature
  *off*; `optional xpr_debug` is about the feature. Reading only the name reported a disagreement
  where there was none — and would have *missed* the real one (`-DMONOTONIC=1`) if the table had
  disagreed. It now parses `-DNAME=value`, with a bare `-DNAME` meaning 1.
- **It compared a device's name to its macro.** The condition is `pty`; the macro is `NPTY`. Comparing
  them directly reported `table says 1, the build script does not define it =1` for a pair that
  agreed — the same one-value-two-definitions shape this table exists to catch, one level up. The
  check now accepts either spelling.

## 3. One header narrowed to one value

`libkern/os/log.c`, `bsd/kern/subr_log.c` and `libkern/c++/OSKext.cpp` include
`<os/firehose_buffer_private.h>`, which is not in the tarball. It is **not** `mach_pagemap.h`-shaped:
everything its users take from it is published under `libkern/firehose/` —
`firehose_chunk_t` (`chunk_private.h:65`), `firehose_stream_t` and `firehose_buffer_t`
(`firehose_types_private.h:69,263`), `firehose_tracepoint_id_t` (`tracepoint_private.h`) — and
`libkern/firehose/Makefile:37-42` exports all four.

So `shims_arm/os/firehose_buffer_private.h` is written as the forwarding header its name says it is,
including those four. The failure changes from

```
fatal error: 'os/firehose_buffer_private.h' file not found
```

to

```
libkern/os/log.c:591:31: error: use of undeclared identifier 'FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT'
```

**One identifier, and it is a value, and there is no evidence for it in the tarball.**
`FIREHOSE_CHUNK_SIZE` is published (`chunk_private.h:35`, 4096); the chunk *count* is not, and it is
used to size real allocations (`subr_log.c:769`, `log.c:591`). Guessing it would under- or
over-allocate a kernel buffer — a runtime defect of exactly the kind this project has spent the
session removing. It is left failing, named, rather than chosen.

That is a better state than before even though no file compiles: the unknown is now **one constant
with a name** instead of a missing header, which is the difference between a question and a guess.

## 4. Why the link went up, and whether that is acceptable

```
removed (9):  bdevsw cdevsw cdevsw_flags chrtoblk chrtoblk_set isdisk nblkdev nchrdev ptsd_kqfilter
added (14):   logopen logclose logread logioctl logselect oslogopen oslogclose oslogread
              oslogioctl oslogselect oslog_streamopen oslog_streamclose oslog_streamread
              oslog_streamioctl oslog_streamselect
```

`conf.c` compiled, so the nine device-switch tables it defines are no longer missing — and it
*references* fourteen log entry points that live in `subr_log.c` and `os/log.c`, the two files
§3 is about. Net: +5.

Kept, for three reasons, and stated so the decision can be re-examined:

1. **It is the configuration Apple's own source requires**, not a preference: their `NPTY 0` path is
   broken, so any kernel built from this source has the device on.
2. **The fourteen are already the queue.** Both files are on the boot-path work list, and they need
   one constant between them. Fixing §3 closes the fourteen *and* seven boot-path symbols.
3. The alternative — leaving `conf.c` uncompiled to keep the link number lower — leaves nine symbols
   permanently missing and every file that touches `cdevsw` failing.

So the link count is the wrong metric to optimise here, which is itself worth recording: it is
**a count of what is missing, not of what is right**, and this change traded five countable misses
for a correct configuration and a smaller real work list (42 → 33 symbols on the boot path).

## How to reproduce

```bash
./tools/xnu_config/device_table.py --write out/device_table.txt   # 4 conditions
./tools/gen_device_headers.sh                                     # NLOOP 0, NPTY 1, NPTMX 1
MANIFEST=$PWD/out/xnu_arm_manifest.txt ./tools/xnu_config/list_sources.py RELEASE --write $PWD/out/xnu_arm_manifest.txt
MANIFEST=$PWD/out/xnu_arm_manifest.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_kernel_obj \
  ./tools/build_xnu_arm_kernel.sh          # 599 of 615
./tools/link_xnu_arm.sh                    # 449 undefined
./tools/boot_closure.py                    # 10 files / 33 symbols
```
