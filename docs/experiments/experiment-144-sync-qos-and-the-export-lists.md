# Experiment 144 — why `sync_qos_count_t` is not a missing typedef, and what the export lists say

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: none — a bounded investigation with a negative result.

The four remaining `sync_qos_count_t` failures are the largest group left in `RELEASE`
(`kern_symfile.c`, `uipc_mbuf.c`, `audit_syscalls.c`, `ux_exception.c`). They looked like the last
three stages' work — a name used where no header supplies it — and they are not.

## The chain, and why the obvious fixes fail

```
bsd/kern/uipc_mbuf.c:96        -> iokit/IOKit/IOMapper.h:35 -> IOKit/IOTypes.h:36
  -> IOKit/system.h:40         -> osfmk/mach/mach_interface.h:35
  -> out/mach_headers/mach/exc_server.h:89   (`simport <kern/ipc_kobject.h>`)
  -> osfmk/kern/ipc_kobject.h:72 -> osfmk/ipc/ipc_kmsg.h:111
  -> sync_qos_count_t, ipc_kmsg_t   ... defined in osfmk/ipc/ipc_types.h:63-64
                                     ... under `#ifdef MACH_KERNEL_PRIVATE` (ipc_types.h:46)
```

Two answers were tried, and **both are measured failures**:

**Forcing `MACH_KERNEL_PRIVATE` for BSD files.** This is what experiment-118 removed, and the reason
is still there — it reaches `kern_types.h:192`'s include of `kern/misc_protos.h`, whose
`ffs(unsigned int)` collides with `bsd/libkern/libkern.h:145`'s `ffs(int)`. Measured on all three
files: the error simply moves to `ffs`. **That is the whole defect 118 fixed, re-created by giving a
BSD file the osfmk view.**

**Supplying the two typedefs narrowly.** `ipc_kmsg_t` is `struct ipc_kmsg *` and `sync_qos_count_t`
is `uint8_t`; a two-line force-include clears the error and reveals `ipc_table_index_t` from
`ipc_table.h:105`. Defining that reveals the next. **The closure grows as you satisfy it** — the same
thing experiment-103 measured for `cpu_data_internal.h`, and the reason a typedef shim is the wrong
shape here rather than merely unprincipled.

## What the export lists say

The header that actually needs the private view is `osfmk/ipc/ipc_kmsg.h`, and it is reached through
`osfmk/kern/ipc_kobject.h`. Apple's own component lists:

```
osfmk/kern/Makefile   EXPORT_FILES  ... does NOT contain ipc_kobject.h
osfmk/ipc/Makefile    EXPORT_ONLY_FILES = ipc_types.h
                      ... and NOT ipc_kmsg.h
```

**`ipc_kobject.h` and `ipc_kmsg.h` are not exported by any component.** In Apple's build no other
component can see them, so no BSD file reaches `ipc_kmsg.h` and the question does not arise. In this
build it does, because `-I$XNU/osfmk` puts the **whole source tree** on the include path — and
experiment-117 already measured what a whole tree in place of a selected list costs, from the other
direction.

`ipc_types.h` *is* exported, which sharpens rather than softens the point: the one header of the three
that Apple does publish is the one whose contents are guarded, and the two it does not publish are the
ones this build reaches.

**The hypothesis that would explain the rest, stated as one:** Apple's exported `mach/exc_server.h`
must not carry its `simport <kern/ipc_kobject.h>` line, or the same chain would break in Apple's build
too — since the simport's target is a header they do not export. Whether their export step strips
those lines, generates the header differently, or excludes `mach_interface.h` from what a BSD file can
reach is **not established here**, and it is the first thing a future attempt should check.

## Why this is worth recording rather than working around

It is the **third** instance of one limitation, and the three together say what the limitation is:

| | |
| --- | --- |
| experiment-139 | `osfmk/kern/ast.h` and `bsd/kern/ast.h` share an include guard — which one wins depends on the component |
| experiment-141 | `size_t` — XNU's own two headers disagree, and only under the ELF triple |
| experiment-144 | a BSD file reaches `ipc_kmsg.h`, which **no component exports** |

All three are the same sentence: **a flat include list cannot express "this component sees that
header's public half and not its private half."** Apple expresses it with `EXPORT_MI_LIST` per
directory and `INCFLAGS_IMPORT` per component (`MakeInc.def:463-469`). This project approximated it,
and the approximation now costs four files that no flag, typedef or include order can fix.

## State

Unchanged from experiment-142: `RELEASE` 602 of 615, `STAGE90_BOOT` 409 of 426, image 362 stubs,
boot path 113. The four `sync_qos_count_t` files are the largest remaining group and are recorded as
**bounded by the include model**, not as pending work.
