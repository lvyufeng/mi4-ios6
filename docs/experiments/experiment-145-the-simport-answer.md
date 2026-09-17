# Experiment 145 — the `simport` question is answered, and the answer was in the Makefile

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `tools/gen_mach_headers.sh`, `tools/build_xnu_arm_kernel.sh`

Experiment-144 recorded a hypothesis and said it was the first thing a future attempt should check:

> Apple's exported `mach/exc_server.h` must not carry its `simport <kern/ipc_kobject.h>` line, or the
> same chain would break in Apple's build too.

**It is confirmed, and the mechanism is two rules in one Makefile.** `osfmk/mach/Makefile`:

```make
MIG_USHDRS = \
        ... exc_server.h mach_exc_server.h ...            # line 65-75, IS exported

${MIG_USHDRS} : %_server.h : %.defs                       # line 231-238
        $(_v)$(MIG) $(MIGFLAGS) \                         # <-- MIGFLAGS ONLY
                -server /dev/null -user /dev/null -header /dev/null -sheader $@ $<

MIG_KSHDRS = \
        ... exc_server.h mach_exc_server.h ...            # line 291+, NOT exported

${MIG_KSHDRS}: %_server.h : %.defs                        # line ~372-380
        $(_v)${MIG} ${MIGFLAGS} ${MIGKSFLAGS} \           # <-- and MIGKSFLAGS
                ... -server $*_server.c -sheader $*_server.h $<
```

`MIGKSFLAGS = -DMACH_KERNEL_PRIVATE -DKERNEL_SERVER=1` (`:247`). The `simport` lines in
`mach_types.defs:606-615` are behind `#if KERNEL_SERVER`. So:

| | built with | has `simport <kern/ipc_kobject.h>`? | exported? |
| --- | --- | --- | --- |
| the `MIG_USHDRS` `exc_server.h` | `MIGFLAGS` | **no** | **yes** — `EXPORT_MI_GEN_LIST = ${MIGINCLUDES}` = UUHDRS + USHDRS |
| the `MIG_KSHDRS` `exc_server.h` | `MIGFLAGS MIGKSFLAGS` | yes | **no** |

**Same filename, two directories, two different files.** `INCFLAGS_GEN` is
`-I$(SRCROOT)/$(COMPONENT) -I$(OBJROOT)/EXPORT_HDRS/$(COMPONENT)` (`MakeInc.def:465`) — the build
directory first, the export root second — and `INCFLAGS_LOCAL = -I.` (`:466`) is the build directory
too. So:

- an **osfmk** file sees the build-dir `exc_server.h` first: **with** the simports, which is what
  `vm_user.c`, `vm_map.c`, `mach_port.c` and `mk_timer.c` need (experiment-121);
- a **BSD** file sees `EXPORT_HDRS` of *other* components only (`INCFLAGS_IMPORT`, `MakeInc.def:463`)
  — the export variant — **without** them, so it never reaches `ipc_kmsg.h`, so `sync_qos_count_t`
  never arises.

**That is the whole of experiment-144's four files**, and it is the same shape as experiments
139, 140 and 141: **which header is right depends on the component**, and a flat include list cannot
say so. This is the fourth instance — and the first where the answer was a *positive* one rather than
a workaround, because Apple's mechanism is fully visible in the rules.

## What was implemented

`gen_mach_headers.sh` now writes **both** variants:

| directory | built with | for |
| --- | --- | --- |
| `out/mach_headers/mach/X_server.h` | `MIGFLAGS` only (the export variant, as Apple) | every component |
| `out/mach_headers/kserver/mach/X_server.h` | `MIGFLAGS MIGKSFLAGS` | osfmk, which sees the build dir first |

and the build places `kserver/` ahead of the export root **for osfmk files only** — the same
per-file mechanism experiments 139/140 built for `COMP_FIRST`.

## Measured

| | before (experiment-144) | after |
| --- | --- | --- |
| `RELEASE` | 602 of 615 | **606 of 615** |
| `STAGE90_BOOT` | 409 of 426 | **412 of 426** |
| stub symbols in the image | 362 | **277** |
| boot-path stubs | 113 | **89** |

All four of experiment-144's `sync_qos_count_t` files newly pass, **no regressions** — twelve files
including the largest group had been bounded by this one mechanism.

## And the measurement link caught a defect the change introduced

The first run after the change failed with

```
multiple definition of `iokit_server_routine'
  mach_headers/device_device_server.o  vs  mach_headers/kserver_device_device_server.o
```

**the manifest had moved the `_server.c` files and `build_xnu_arm_kernel.sh` never deleted the old
objects.** It cleared `*.log` (experiment-116's fix) and not `*.o`, so the link saw both generations.
Same defect class one level up: a count of two runs, this time caught by the linker rather than by a
tally. The build now clears `*.o` with the logs.

