# Experiment 124 — generate what the Makefiles say: 40 MIG outputs, not everything

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/xnu_config/mig_outputs.py` (new), `tools/gen_mach_headers.sh`,
`tools/build_xnu_arm_kernel.sh`, `tools/boot_closure.py` (new), `tools/link_gap.sh`

| Configuration | before (experiment-123) | after |
| --- | --- | --- |
| `STAGE90_BOOT` | 389 of 419 | **395 of 419** |
| `RELEASE` | 507 of 587 | **513 of 587** |

And the link gap, which is the number that decides how far the kernel is from linking:

| | before | after |
| --- | --- | --- |
| missing symbols | 1187 | **988** |
| …on the boot path | 1094 | **907** |

## 1. Over-generating MIG output was costing five files, and it was measured twice

`gen_mach_headers.sh` ran MIG over **every `.defs` in `osfmk`** and asked for all four outputs each
time. Apple does not do that. Its Makefiles list the outputs explicitly, and the lists are smaller
and oddly shaped:

```
osfmk/mach/Makefile   MIG_UUHDRS   clock.h clock_priv.h ... vm_map.h            (19 headers)
                      MIG_USHDRS   ... notify_server.h ...                       (10 +server.h)
                      MIG_KUHDRS   ... mach_notify.h memory_object.h ...          (14 headers)
                      MIG_KUSRC    ... memory_object_user.c mach_notify_user.c ... (17 sources)
osfmk/device/Makefile DEVICE_FILES device_server.h device_server.c
```

Two entries in particular decide the file counts:

- **`notify.defs` yields `notify_server.h` and nothing else.** `mach/notify.h` is hand-written in the
  tree and carries `MACH_NOTIFY_NO_SENDERS` and the notification structs; a generated one is a
  user-side stub with none of it. Generating it and putting the generated root first — which is where
  Apple puts its own — hides the real header from `osfmk/ipc/ipc_voucher.c` and
  `osfmk/kern/ipc_kobject.c`.
- **`memory_object.defs` yields `memory_object.h`.** Apple *does* generate it, and its kernel
  therefore never sees the collision between the hand-written `osfmk/mach/memory_object.h`
  (user-side `typedef mach_port_t memory_object_t`) and `osfmk/mach/memory_object_types.h`
  (kernel-side `struct memory_object *`). That collision was failing **six `osfmk/vm` files** —
  `vm_map.c`, `vm_pageout.c`, `vm_fault.c`, `vm_user.c`, `vm32_user.c`, `memory_object.c`.

So the two failures that looked like separate problems are the two halves of one: *generate exactly
what the Makefiles list*. `tools/xnu_config/mig_outputs.py` parses the `MIG_*` variable blocks out of
every Makefile that runs `$(MIG)` and writes a spec; `gen_mach_headers.sh` reads it and asks MIG for
only the listed outputs per base. **40 bases, 0 failures.**

Three things the extractor had to learn, each from a build failure rather than from reading:

- **A `user` output implies a header.** `osfmk/mach/Makefile:361-365` is
  `-user $*_user.c -header $*.h` — one run, two outputs — so `sysdiagnose_notification.h` exists even
  though no `HDRS` list names it (`osfmk/kern/sysdiagnose.c:33` includes it).
- **Not all of them are called `MIG_*`.** `osfmk/device/Makefile` hard-codes its outputs in the rule
  and lists them in `DEVICE_FILES`; a first version of the extractor required a `MIG_` prefix and
  skipped the file entirely, after which `device_server.h` was reported missing by the *build*.
- **`",$kinds,"` not `*header*`.** The kinds are a comma-joined string, and a plain `*header*` glob
  matches `"sheader"` — which regenerated `notify.h` and defeated the whole change, in exactly the
  way it exists to prevent.

## 2. And so the include order flips back

`experiment-119` measured the generated root in front of the source tree as *worse* (307 vs 312) and
concluded "a generated root must not sit in front of the tree". That was right for what it tested —
but what it tested was an over-generating MIG root. With the output set taken from the Makefiles, the
measurement reverses:

| Ordering | minimal |
| --- | --- |
| MIG root first (Apple's `-I.`) | **395** |
| MIG root last (experiment-119's conclusion) | 384 |

Apple's order is `INCFLAGS = $(INCFLAGS_LOCAL) $(INCFLAGS_GEN) ...` with `INCFLAGS_LOCAL = -I.`
(`MakeInc.def:466-469`) — the build directory, where MIG and makesyscalls write, ahead of the
component's source tree. The build now does that, and `gen_export_headers.sh` carries a note pointing
at the correction so the old conclusion is not re-derived.

This is the sixth instance of a shape this project keeps meeting, and the first where the *fix* was
"produce less": experiment-117 deleted two shims, experiment-120 deleted ten, this deletes twelve
generated headers. In every case the file was generated or written against a belief about what was
absent, and the belief was the defect.

## 3. The link gap, re-measured, and a question that was not being asked

`tools/boot_closure.py` is new and answers something `link_gap.sh` cannot: **which of the files that
do not compile are on the boot path at all.** It walks the symbol graph from the `osfmk/arm`
objects — take their undefined symbols, find the object defining each, repeat — and reports the
files in that closure that have no object.

```
objects on the boot path: 739 of 911
needed but undefined everywhere: 907  (+9 compiler-runtime)
work list: 27 files, 86 symbols
  osfmk/vm/vm_compressor.c       17
  osfmk/kern/task.c              16
  libkern/gen/OSAtomicOperations.c 15
  bsd/kern/bsd_init.c             8
  bsd/dev/arm/conf.c              7   ...
```

**27 of the 74 failing files matter; the other 47 do not.** Most of the rest are `bsd/netinet6`,
`bsd/nfs` and `bsd/vfs` — subsystem code a kernel reaching a first scheduler tick never calls. That
reframes the remaining work from "74 files" to "27 files, and 86 symbols", and it is a lower bound
(calls through function pointers and assembly entries are invisible to a symbol walk).

## What is left

24 failures in the minimal configuration, 74 in `RELEASE`, and 27 of those on the boot path. The
remaining clusters are `clock_t`/`uid_t`, `u_char`/`caddr_t`, the `sync_qos_count_t` include-order
question experiment-121 left open, and four genuinely absent headers.

## How to reproduce

```bash
./tools/xnu_config/mig_outputs.py --write out/mig_outputs.txt   # 40 bases
./tools/gen_mach_headers.sh                                     # 40 generated, 0 failed
XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  ./tools/xnu_config/list_sources.py STAGE90_BOOT --write $PWD/out/xnu_arm_manifest_min.txt
XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  MANIFEST=$PWD/out/xnu_arm_manifest_min.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_min_obj \
  ./tools/build_xnu_arm_kernel.sh          # 395 of 419

./tools/link_gap.sh                        # 988 missing, 9 compiler-runtime
./tools/boot_closure.py                    # 27 files / 86 symbols on the boot path
```
