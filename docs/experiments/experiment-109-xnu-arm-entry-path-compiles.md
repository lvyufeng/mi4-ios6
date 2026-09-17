# Experiment 109 — XNU's ARM entry path compiles

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `stages/stage90/xnu_arm_entrypath_sweep.sh`, `xnu_arm_sweep.sh` (flags aligned)

## The result

```
== XNU's ARM entry path, with the best flag set known ==

  arm_init.c                   0 error(s)
  arm_vm_init.c                0 error(s)
  machine_routines.c           0 error(s)

== osfmk/arm as a whole ==
osfmk/arm: 8 of 32 compile
```

`arm_init.c` is the function XNU's `_start` branches to — the exact thing Stage90's entry image
stubs. Two turns ago it was 35 errors, and the turn before that it was reported as 4 (a truncated
count). It now compiles, along with `arm_vm_init.c` and `machine_routines.c`, which are the two
that follow it in a real boot.

**Compiling is not linking and not running**, and the rest of this log is explicit about the gap.
But `arm_init.c` compiling was the named blocker for replacing the stub, and it is gone.

## What it took: values, not tools

`experiment-108` removed the MIG wall — 23 headers generated from the `.defs` Apple publishes. What
remained was, as predicted there, *choosing values*. Four did nearly all of the work:

| Value | Effect |
| --- | --- |
| `-DXNU_KERNEL_PRIVATE=1` | **35 → 10.** It gates `vm_tag_t`, used nine times across this file's include graph |
| `-DCONFIG_SCHED_TIMESHARE_CORE=1 -DCONFIG_SCHED_TRADITIONAL=1` | resolves `struct run_queue` — defined only under `TIMESHARE_CORE` or `PROTO` (sched.h:201), while `MULTIQ` expects a `kern/sched_multiq.h` that is not in the tarball |
| `-ffreestanding` | fixes `enum memory_order`: without it clang uses its *hosted* `<stdatomic.h>`, which defines `memory_order` as macros rather than the `enum memory_order` XNU's ARM atomics name |
| `-DPRIVATE=1` | gates `kqueue_id_t` in `bsd/sys/event.h` |

Note the second row: **the source itself narrows the choice.** `sched_prim.h:574`'s `#error` names
`MASTER.XXX` — the file the build generates and the tarball does not ship — but the definition of
`struct run_queue` and the absence of `sched_multiq.h` together leave two of the four schedulers
viable. So `MASTER.XXX` being absent is not the dead end it looked like; the errors pick between
the alternatives.

The force-include set grew by three, each for a reason recorded next to it:

- `sys/_types/_u_int.h` — `sched.h` uses `u_int` without including `sys/types.h`, and the whole of
  `sys/types.h` collides with `kern_types.h` (both define `clock_t`, differently). The single
  fragment is the smallest thing that resolves it.
- `mach/task_policy.h`, `mach/thread_policy.h` — the QoS `*_policy` structs are members of `task_t`
  and `thread_t` but are declared in `mach/`, which neither `kern/task.h` nor `kern/thread.h`
  includes.

## What this does and does not mean

**Does:** the file the entry point calls, and the two that follow it, now compile against the real
4570 tree with a flag set that is reproducible and documented. The `-ferror-limit=0` and the
absence of any fatal error mean these are **complete** zeroes, not truncated ones — the failure mode
that produced the "4 errors" claim two turns ago.

**Does not:** link, and does not run. Compiling three translation units is not a kernel: they
reference thousands of symbols that are not in any object this project builds, and the objects would
need to link against the rest of XNU before any of it could execute. The layer as a whole is 8 of
32, and the 24 that do not compile include the pmap, the scheduler and the interrupt path — the
things a kernel actually needs to boot.

So the honest statement is: **the entry path now compiles; XNU does not yet build, and does not
run.** What changed is that the specific blocker for replacing the `arm_init` stub is a link
problem rather than a compile problem, and a link problem is the next thing that can be measured.

## Also worth recording

The two sweep scripts now share one flag set, so `xnu_arm_sweep.sh`'s layer-wide number (8 of 32,
up from 3) and the entry-path numbers are comparable. They were not before, and two scripts
reporting different numbers for the same tree is its own kind of wrong.
