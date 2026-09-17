# Experiment 121 — what Apple's compiler defines and what Apple's MIG is told

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/build_xnu_arm_kernel.sh`, `tools/build_xnu_arm_layer.sh`, `tools/gen_mach_headers.sh`

| Configuration | before (experiment-120) | after |
| --- | --- | --- |
| `STAGE90_BOOT` | 328 of 405 | **375 of 417** |
| `RELEASE` | 368 of 573 | **496 of 585** |

`RELEASE` is 85 % compiled. No line of XNU's code was changed. The stage is two findings, and each
is a thing the project had been supplying differently from Apple — one by omission, one by
approximation.

## 1. `__APPLE__` is part of the compiler, not of the source

The scripts here compile with `clang --target=armv7-none-eabi`. Apple's build uses a Darwin target
triple, and `__APPLE__` comes with that triple rather than from any header. So this build was
compiling XNU with the macro that most of its `#if` conditions are written against undefined.

The clearest casualty is a vendored library inside the tree:

```c
/* osfmk/prng/YarrowCoreLib/include/yarrow.h:91 */
#ifndef YARROWAPI
#if     defined(macintosh) || defined(__APPLE__)
#define YARROWAPI
#else
#define YARROWAPI __declspec(dllimport)
#endif
#endif
```

and at `:53`, `#if defined(macintosh) || defined(__APPLE__) / #include "WindowsTypesForMac.h"` —
which is where `BYTE`, `UINT`, `LONGLONG` and `LPVOID` come from. Off that branch, the Windows
library takes its `__declspec` path and seven files die on it: 46 uses of `BYTE`, 45 of `UINT`, and
5 `__declspec attributes are not enabled`.

Adding `-D__APPLE__=1` is worth **17 files in the minimal configuration and 99 in `RELEASE`**, with
no regressions in either. Two related flags were measured and not adopted:

| Probe | Minimal | Note |
| --- | --- | --- |
| `-D__APPLE__=1` | **345** | adopted |
| `-D__APPLE__=1 -D__MACH__=1` | 345 | `__MACH__` adds nothing |
| `--target=armv7-apple-darwin` | 346 | one more file; changes ELF→Mach-O, which is a link-step decision |

In `osfmk/arm` nothing takes an `#ifdef __APPLE__` branch, so the layer build is byte-identical with
and without it — 32 of 32, 118,970 bytes, 445 undefined symbols. It is passed there anyway, with the
measurement written down, so that the two build scripts describe one configuration rather than two.

## 2. MIG is run **twice** per `.defs`, and the project was running it once

`osfmk/mach/Makefile:361-382` has two rules and they do not use the same defines:

```make
%_user.c   : %.defs
	$(MIG) $(MIGFLAGS) $(MIGKUFLAGS) -user $*_user.c -header $*.h -server /dev/null -sheader /dev/null $<
%_server.c : %.defs
	$(MIG) $(MIGFLAGS) $(MIGKSFLAGS) -server $*_server.c -sheader $*_server.h -header /dev/null -user /dev/null $<
```

with `MIGKUFLAGS = -DMACH_KERNEL_PRIVATE -DKERNEL_USER=1 -maxonstack 1024` and
`MIGKSFLAGS = -DMACH_KERNEL_PRIVATE -DKERNEL_SERVER=1` (`osfmk/mach/Makefile:247-248`).

`gen_mach_headers.sh` ran MIG **once**, asking for `-header` and `-sheader` in the same invocation.
The `.defs` language is full of `#if KERNEL_SERVER` / `#if KERNEL_USER` blocks, so that produced a
server header generated with `KERNEL_SERVER` undefined and a user header generated with `KERNEL_USER`
undefined. The previous commit's comment reasoned "the header is the same either way". It is not.

The concrete loss is `osfmk/mach/mach_types.defs:606-615`:

```
#if KERNEL_SERVER
#ifdef	MACH_KERNEL_PRIVATE
simport <ipc/ipc_voucher.h>;	/* for voucher conversions */
simport <kern/ipc_kobject.h>;	/* for null conversion */
simport <kern/ipc_tt.h>;	/* for task/thread conversion */
simport <kern/ipc_host.h>;	/* for host/processor/pset conversions */
...
```

A `simport` becomes an `#include` in the generated server header. Preprocessed away, it never
reached MIG, and `vm_user.c`, `memory_object.c`, `vm_map.c`, `mach_port.c` and `mk_timer.c` failed
on **34 occurrences** of `IKOT_NAMED_ENTRY`, `IKOT_TIMER`, `IKOT_NONE`, `IKOT_IOKIT_CONNECT` and
`ipc_kobject_type_t` — all of which have been in `osfmk/kern/ipc_kobject.h` the whole time.

Two runs also produce `*_user.c`, which the manifest lists and which were not being generated at
all: the file count rises from 405 to 417 in the minimal configuration and 573 to 585 in `RELEASE`.

`-novouchers` is in Apple's `MIGFLAGS` (`MakeInc.def:470`) and is deliberately **not** passed: it
suppresses the voucher conversion routines, which the kernel's own sources reference. Recorded
because silently matching Apple here would be wrong.

## 3. Three regressions, and the mechanism is identified

| File | Configuration |
| --- | --- |
| `bsd/security/audit/audit_syscalls.c` | both |
| `bsd/uxkern/ux_exception.c` | both |
| `bsd/kern/uipc_mbuf.c` | `RELEASE` only |

They are a consequence of the fix, not an unrelated break, and the chain is exact:

```
bsd/kern/uipc_mbuf.c:96 → iokit/IOKit/IOMapper.h:35 → IOTypes.h:36 → system.h:40
  → osfmk/mach/mach_interface.h:35 → out/mach_headers/mach/exc_server.h:89
  → osfmk/kern/ipc_kobject.h:72 → osfmk/ipc/ipc_kmsg.h:111
```

`exc_server.h:89` is one of the new `simport` includes. `ipc_kmsg.h` uses `sync_qos_count_t` and
`ipc_kmsg_t` unconditionally, and both live in `osfmk/ipc/ipc_types.h` **inside**
`#ifdef MACH_KERNEL_PRIVATE` (`ipc_types.h:46`) — and a BSD translation unit is one that does not
define it (experiment-118).

Adding `-DMACH_KERNEL_PRIVATE` for `bsd` was tested and is not the answer: it reaches
`osfmk/kern/sched_prim.h:574`'s `#error`, the same scheduler-configuration boundary that established
the per-component rule in the first place. So these three files now reach `ipc_kmsg.h` through a
path that cannot compile it, and whether Apple's own build avoids that path by a different include
order is **not established** — it is recorded as open rather than papered over.

## What is left

42 failures in the minimal configuration, 89 in `RELEASE`. The largest remaining cluster is the MIG
`consume_ref` types — `mem_entry_name_port_move_send_t` (12), `semaphore_consume_ref_t` (9),
`thread_act_consume_ref_t` (7) — which are declared as
`type X = mach_port_move_send_t` in `mach_types.defs` and rendered into the generated server headers
without a definition. `mach_port_move_send_t` itself is declared (`std_types.defs:128`,
`type mach_port_move_send_t = MACH_MSG_TYPE_MOVE_SEND`), so the question is whether the published
MIG's handling of a named type aliased to a port disposition differs from 4570's. Next stage.

## How to reproduce

```bash
./tools/gen_mach_headers.sh                       # 44 generated, 8 types-only, 0 failed
XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  ./tools/gen_option_headers.py
./tools/gen_libkern_version.sh

MANIFEST=out/xnu_arm_manifest_min.txt
XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  ./tools/xnu_config/list_sources.py STAGE90_BOOT --write $PWD/$MANIFEST

XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  MANIFEST=$PWD/$MANIFEST XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_min_obj \
  ./tools/build_xnu_arm_kernel.sh                 # 375 of 417
```

Regenerate the manifest after regenerating the MIG headers: two runs produce `*_user.c`, which the
manifest lists, so a stale manifest understates the file count.
