# Experiment 107 — XNU's ARM entry path is four errors from compiling, and the four are named

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `stages/stage90/xnu_arm_entrypath_sweep.sh`, four new headers in `shims_arm/`

## The result

`stages/stage90/xnu_arm_entrypath_sweep.sh`, with the accumulated flag set:

```
== XNU's ARM entry path, with the best flag set known ==

  arm_init.c                   4 error(s)
  arm_vm_init.c                1 error(s)
  machine_routines.c          12 error(s)
```

For comparison, `xnu_arm_sweep.sh` reports **3 of 32** for `osfmk/arm` as a whole. The entry path
is not representative of that: `arm_vm_init.c` is *one include* from compiling, and `arm_init.c` —
the function XNU's `_start` branches to, and therefore the exact thing `experiment-106`'s stub
stands in for — is four errors away.

That is a materially different position from "the build configuration is absent". It is also the
first measurement of this that is *scoped*: the 3-of-32 number is dominated by files nowhere near
the entry path, and measuring the entry path specifically changes the conclusion.

## The flag set, and the one line that mattered most

Every flag is in the script with its reason. Two are worth repeating here:

- **XNU's include paths must come before `shims/`.** With shims first,
  `shims/mach/machine/vm_types.h` shadows `osfmk/mach/arm/vm_types.h` — which is where `natural_t`
  is defined — and `arm_init.c` opens with two dozen "unknown type name 'natural_t'". That single
  ordering change took it from a wall of type errors to ten real ones. `experiment-103` had found
  this for the sweep; it is worth stating that it is *the* first thing to get right.
- **The force-included header set is not optional.** `osfmk/arm/simple_lock.h` defines
  `decl_simple_lock_data` and is **included by nothing in the tree**; `osfmk/kern/queue.h` and
  `kern/ast.h` carry `mpqueue_head_t` and `ast_t`, which `cpu_data_internal.h` uses without
  including them. With those three force-included plus `MACH_KERNEL_PRIVATE`, the ten remaining
  errors vanish and the next thing the compiler says is a *configuration* `#error` rather than a
  type error. That is the boundary between "missing code" and "missing configuration", and it is
  now located precisely.

## The remaining four, classified

They are three distinct kinds, and the third is the interesting one.

**1. Two are include-ordering, not gaps.** `sched.h:302` wants `timer_call_param_t` and
`thread_call.h:360` wants a complete `struct call_entry`. Both types exist in the tree; they are
used before their definitions are reached along this include path. `kern/timer_call.h` and
`kern/thread_call.h` address part of it and are in the script; what is left is the same shape of
work as the force-include set, not a new wall.

**2. `mach_debug.h` is a path shim — and behind it is a generated-header wall.**
`osfmk/arm/arm_vm_init.c:29` and `osfmk/ipc/ipc_port.h:78` both include `<mach_debug.h>`. The real
header exists at `osfmk/mach_debug/mach_debug.h`; it resolves as `<mach_debug.h>` only because the
kernel build adds that directory to its include path. A redirect shim is the right fix and is now
in `shims_arm/`.

But the redirect leads somewhere informative. `mach_debug.h:44` then wants `<mach/mach_host.h>`,
and **no such file exists anywhere in the tarball.** It is generated: `osfmk/mach/mach_host.defs`
is present, and MIG turns it into the header during the build. So this is not a header that can be
stubbed honestly — it is a wrapper around the MIG-generated interface set, and the missing piece is
a *build step*, not a file.

That is the sharpest statement of Phase 4's wall this project has produced. "The build
configuration is absent" was true and vague; this says one of its concrete components is
**MIG-generated headers**, derivable in principle — the `.defs` are public and MIG is open source —
but requiring a tool and a step this host does not have.

**3. `machine_routines.c`'s twelve** are the same families again plus `btlog_t`, `debugger_op` and
`THREAD_QOS_LAST`, which are `CONFIG_*`-gated. It is the file behind `ml_get_timebase` and
`ml_io_map`, so it matters — but it is also the one that reaches furthest into the scheduler, which
is where the configuration wall is thickest.

## Why the scheduler `#error` is worth quoting

With the force-includes in place, the next thing the compiler said was not a type error:

```
osfmk/kern/sched_prim.h:574: #error Enable at least one scheduler algorithm in osfmk/conf/MASTER.XXX
```

**`osfmk/conf/MASTER.XXX` does not exist in the tarball.** The tarball ships `osfmk/conf/files.*`
and `Makefile.*`, but no `MASTER.*`; the build system generates that file, and the scheduler
algorithm is selected there. So the value has to be chosen rather than read. `CONFIG_SCHED_MULTIQ`
is the plausible one for a Darwin-14 iOS ARM kernel, it is what the measurement uses, and it is
recorded as *chosen, not sourced* — the same handling `kDbgIdTopLevelHeader` got in
`experiment-102`, and for the same reason.

The four algorithms the tree contains (`sched_traditional`, `sched_proto`, `sched_grrr`,
`sched_multiq`) all compile to within a couple of errors of each other, so the choice does not
change the measurement. It changes what a *working* kernel would do, which is why it is flagged
rather than silently picked.

## What this changes

Phase 4's plan in the roadmap says the ARM layer's gap is "8 headers for the first layer, times an
unknown number of layers". This replaces that with something checkable:

| File | Distance | Kind of gap |
| --- | --- | --- |
| `arm_vm_init.c` | 1 include | a redirect to a **MIG-generated** header set |
| `arm_init.c` | 4 errors | include ordering (2) + the same MIG wall (1) + 1 cascade |
| `machine_routines.c` | 12 errors | the above + three `CONFIG_*`-gated types |

The `arm_init.c` number is the one that matters for the immediate goal, because it is the function
`_start` branches to. **Four errors is the closest this project has ever been to replacing that
stub with XNU's real code** — and the blocker in front of it is now a named build step (MIG) rather
than an adjective.

## What this does not establish

Nothing compiles yet, nothing is linked into anything, and no device run happened. The four errors
are four errors; the pattern of the last four attempts is that each one resolves to the next header,
so the honest expectation is that clearing these four reveals more rather than reaching a link.
What has changed is that the *kind* of remaining work is now known: include ordering, which is
mechanical; `CONFIG_*` values, which must be chosen; and MIG output, which is a build step.

## What was added

- `xnu_arm_entrypath_sweep.sh` — the measurement, reproducible, with every flag's reason recorded
  and the `MASTER.XXX` situation in its header comment.
- `shims_arm/mach_debug.h` — a redirect to the real `osfmk/mach_debug/mach_debug.h`.
- `shims_arm/san/kasan.h`, `shims_arm/security/_label.h` — two more absent headers, both taking the
  released-kernel configuration (less code exercised, not more).
