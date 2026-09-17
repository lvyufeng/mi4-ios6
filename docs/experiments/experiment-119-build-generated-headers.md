# Experiment 119 — the headers the build generates, and the three ways this project was not generating them

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/gen_mach_headers.sh`, `tools/build_xnu_arm_kernel.sh`

| Configuration | before (experiment-118) | after |
| --- | --- | --- |
| `STAGE90_BOOT` | 288 of 401 | **315 of 405** |
| `RELEASE` | 329 of 569 | **356 of 573** |

No line of XNU's code was changed. The denominators are not identical between the two columns, and
the reason is in the sections below: the manifest enumerates MIG output, so generating more of it
moves files out of `absent from tarball` and into `C files tried`. Four did, in the minimal
configuration.

The stage is a single idea — *the header exists, it is build output, and this project was not
producing it* — applied to three different places, each with its own reason for having been missed.

## 1. MIG was being run without the kernel defines, so two interfaces generated nothing

`mach_types.defs` guards the entire Universal Page List block behind `#if KERNEL_PRIVATE`:

```
type upl_size_t			= uint32_t;      /* mach_types.defs:81-90 */
type upl_t = mach_port_t
```

`memory_object_control.defs:74-75` includes `std_types.defs` and `mach_types.defs`, so those types
should arrive. They did not, because the preprocessor step in this project's MIG pipeline passed
**no `-D` flags at all**. `memory_object_control` and `upl` therefore failed with
`type 'upl_size_t' not defined`, produced nothing, and took ten files with them.

Apple's MIG is fed `MIGFLAGS = $(DEFINES) $(INCFLAGS) -novouchers …` (`MakeInc.def:470`), where
`DEFINES` carries `-DKERNEL_PRIVATE -DXNU_KERNEL_PRIVATE` (`MakeInc.def:78`), plus
`MIGKSFLAGS = -DMACH_KERNEL_PRIVATE -DKERNEL_SERVER=1` / `MIGKUFLAGS = … -DKERNEL_USER=1` in each
component's Makefile. The kernel half of that is now passed. Result: `upl`, `memory_object_control`
and 12 more `.defs` generate where they previously failed.

**The lesson is the same one experiment-118 learned from the other end**: the defines are not
decoration, and a step that runs the preprocessor without them runs it on a different program.

## 2. The `.defs` set was one directory wide

The first version generated an explicit list of 25 `.defs`, all under `osfmk/mach`, "so that a
failure here is a short list rather than forty". The `.defs` files are not only there:

```
osfmk/atm/atm_notification.defs        ->   <atm/atm_notification.h>
osfmk/device/device.defs               ->   <device/device_server.h>
osfmk/UserNotification/UNDRequest.defs ->   <UserNotification/UNDRequest.h>
osfmk/UserNotification/UNDReply.defs   ->   <UserNotification/UNDReplyServer.h>
osfmk/lockd|gssd|kextd/…_mach.defs     ->   <lockd|gssd|kextd/…_mach.h>
```

`osfmk/device/iokit_rpc.c:59` includes `<device/device_server.h>`; nothing in the tree provides it.
The set is now every `.defs` under `osfmk` (52 of them), with `libsyscall/mach`'s 18 excluded because
those are the *user-side* copies of the same interfaces and a kernel does not build them.

Two consequences worth recording, because both are things the script had to learn:

- **Types-only `.defs` are not failures.** `std_types`, `mach_types`, `clock_types`,
  `mach_debug_types`, `machine_types`, `atm_types`, `UNDTypes` carry no messages; they exist to be
  `#include`d. Running MIG on them produces `no SubSystem declaration` (and for `machine_types`,
  `type 'int16_t' not defined`, since `std_types` is what defines those). They are now detected from
  the preprocessed text and reported as `types only, nothing to generate`. The first attempt tested
  the *source file* for a `subsystem` line, which skipped `mach_notify.defs` — 38 lines of comment
  and one `#include <mach/notify.defs>` — even though `osfmk/ipc/ipc_notify.c:68` includes
  `<mach/mach_notify.h>` and Apple's `osfmk/mach/Makefile` lists it among the MIG outputs. **It is a
  rename of `notify.defs`, and the test has to be on the expansion, not the file.**
- **The server-header spelling is per directory.** `-sheader $@` in `osfmk/mach/Makefile:236-238`,
  `osfmk/device/Makefile:52-53` and `osfmk/atm/Makefile:70-71` gives `<base>_server.h`, but
  `osfmk/UserNotification/Makefile:80-81` is `-sheader $*Server.h` and gives `<base>Server.h`. That
  is the one difference, and getting it wrong is a `file not found` for
  `<UserNotification/UNDReplyServer.h>` (`osfmk/ipc/ipc_kobject.c:107`) that reads like a missing
  MIG run.

## 3. The generated root was in front of the source tree, and it was shadowing it

MIG generates three headers whose paths also exist in the tree as hand-written files:

```
out/mach_headers/mach/memory_object.h   vs   osfmk/mach/memory_object.h
out/mach_headers/mach/notify.h          vs   osfmk/mach/notify.h
out/mach_headers/mach/semaphore.h       vs   osfmk/mach/semaphore.h
```

`-I$MIG_HEADERS` was the second entry in the include list, ahead of every source tree, so the
generated ones won. Moving it after all the trees — the order Apple's `INCFLAGS_GEN` has, where the
own component's source tree comes first — is **307 → 312 files** in the minimal configuration and
**348 → 353** in `RELEASE`. Five files, from three shadows.

This is experiment-117's `osfmk/libsa` lesson arriving from the other direction: a generated root is
not the tree, and it must not sit in front of it. It is the fifth time in this project that a broad
path in front of a narrow one has been the bug.

## What remains

90 failures in the minimal configuration, now mostly not headers at all: 24 missing headers, 18
unknown type names, 14 incomplete field types, 8 typedef redefinitions, 5 `__declspec` attribute
errors, and small tails. In `RELEASE`, 217.

The largest single remaining item is still `mach_ipc_debug.h` (9 files), and it is the clear next
step because it is a **fourth instance of this same idea, identified but not yet implemented**:

`osfmk/conf/files:40` and `bsd/conf/files:20` declare

```
OPTIONS/mach_ipc_debug		optional mach_ipc_debug
```

and there are **123 such lines** across the components' `conf/files`. `SETUP/config/mkheaders.c` is
the tool: `do_header()` (`:104-133`) checks whether `<name>.h` exists, and if not writes
`#define <option> <count>` into it and appends `#include <<name>.h>` to **`meta_features.h`** — the
header every component force-includes (`osfmk/conf/Makefile.template:19`). So `mach_ipc_debug.h`,
`mach_vm_debug.h`, `mach_cluster_stats.h`, `mach_ipc_test.h`, `kdebug.h`, `vm_cpm.h` and their
siblings are one-line generated files that this project has been shimming by hand or failing on.
`osfmk/ipc/ipc_hash.h:128` includes `<mach_ipc_debug.h>` **unguarded** for exactly that reason: the
include exists to define the option's macro, and the real content sits behind `#if MACH_IPC_DEBUG`
on the next line.

`libkern/version.h` (3 files) is the same category with a different generator:
`libkern/libkern/Makefile:79-87` builds it from `version.h.template` and `config/MasterVersion`,
which are both in the tarball.

Those two are the next stage. Everything else in the remaining list is a genuine gap — `loop.h`,
`pty.h`, `compat_43.h`, `sys/modctl.h`, `os/firehose_buffer_private.h` are in no component's
`EXPORT_MI_LIST` and match no `OPTIONS/` line.

## How to reproduce

```bash
./tools/gen_mach_headers.sh                       # 44 generated, 8 types-only, 0 failed

MANIFEST=out/xnu_arm_manifest_min.txt
XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  ./tools/xnu_config/list_sources.py STAGE90_BOOT --write $PWD/$MANIFEST

XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  MANIFEST=$PWD/$MANIFEST XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_min_obj \
  ./tools/build_xnu_arm_kernel.sh                 # 315 of 405
```

Regenerating the manifest after regenerating the headers is required, not optional: the manifest
lists the MIG outputs as files to compile, so a manifest built against a smaller MIG run understates
the work and misreports `absent from tarball`.
