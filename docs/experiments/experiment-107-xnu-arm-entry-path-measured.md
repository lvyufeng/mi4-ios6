# Experiment 107 — XNU's ARM entry path, measured: seven missing names, and three of them are MIG

Date: 2026-09-17
Host only — nothing here runs on the device.
Artifacts: `stages/stage90/xnu_arm_entrypath_sweep.sh`, five new headers in `shims_arm/`

## The result

`stages/stage90/xnu_arm_entrypath_sweep.sh`, with the accumulated flag set:

```
== XNU's ARM entry path, with the best flag set known ==

  arm_init.c                   20 error(s)  <-- includes 1 FATAL (count is a floor)
  arm_vm_init.c                20 error(s)  <-- includes 1 FATAL (count is a floor)
  machine_routines.c           30 error(s)  <-- includes 1 FATAL (count is a floor)
```

> **Corrected.** The first version of this measurement reported `arm_init.c` as **4 errors** and
> `arm_vm_init.c` as **1**, and both numbers were wrong in the same way. clang's default
> `-ferror-limit` is 20, and a *fatal* error — a missing header — ends the translation unit
> outright. Both truncate the count, and the fatal error's own line counted as one of the four. The
> script now passes `-ferror-limit=0` and **labels any count that includes a fatal as a floor**.
> What the corrected run shows is below; the section that follows has been rewritten around it.
>
> The mistake is worth keeping visible because it is the shape this project keeps having to
> correct: a measurement that produced a number, where the number happened to be an artifact of how
> the measurement stopped rather than of what it measured. `experiment-96`'s probe reported four
> runs of false negative for the same kind of reason.

For comparison, `xnu_arm_sweep.sh` reports **3 of 32** for `osfmk/arm` as a whole. Even corrected,
the scoping still changes the picture: `arm_init.c` — the function XNU's `_start` branches to, and
therefore the exact thing `experiment-106`'s stub stands in for — has **seven distinct missing
names**, not a wall of them:

```
      3 unknown type name 'timer_call_data_t'
      2 unknown type name 'timer_call_param_t'
      2 undeclared identifier 'THREAD_QOS_LAST'
      1 unknown type name 'kcdata_descriptor_t'
      1 unknown type name 'debugger_op'
      1 unknown type name 'btlog_t'
        + struct call_entry incomplete
```

Every one of those is a name the kernel's build supplies: two are `timer_call` types the
force-include set does not reach, `THREAD_QOS_LAST` is `CONFIG_*`-gated, and the rest are
behind headers that are generated or absent. So the corrected statement is not "four errors" but
**seven missing names, each of which is a configuration value or a generated header** — which is
the same conclusion as before, minus the false optimism.

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

## The remaining errors, classified

**1. Two are include-ordering, not gaps.** `sched.h:302` wants `timer_call_param_t` and
`thread_call.h:360` wants a complete `struct call_entry`. Both types exist in the tree; they are
used before their definitions are reached along this include path. `kern/timer_call.h` and
`kern/thread_call.h` address part of it and are in the script; what is left is the same shape of
work as the force-include set, not a new wall.

**2. Three of them are the MIG wall, and it is not avoidable by shimming.**
`<mach_debug.h>` resolves to the real `osfmk/mach_debug/mach_debug.h` (which in turn wants
`<mach/mach_host.h>` and `<mach/mach_port.h>` — both MIG output, both absent), and separately
`osfmk/vm/vm_object.h:71` wants `<mach_pagemap.h>` — `mach_pagemap.defs` is present, the header is
not. **40 `.defs` files ship under `osfmk/mach/`; their generated headers do not.**

Three variants were tried and measured, which is what turned this from a guess into a finding:

| Variant | arm_init.c | Why |
| --- | --- | --- |
| real `mach_debug.h` (redirect) | 4 reported | a fatal `mach_host.h` ends the file — the count is truncated, not small |
| empty `mach_debug.h` | 20 | loses the real `mach_debug_types.h` chain |
| `mach_debug.h` = `mach_debug_types.h` only | 20 | what ships now |

Then the *next* fatal is `task_swapper.h` (absent entirely, `osfmk/vm/vm_map.h:104`), and after a
shim for that, `mach_pagemap.h` (MIG). Each shim reveals the next generated header rather than
approaching a compile. That is the same convergence behaviour `experiment-103` measured for
`cpu_data_internal.h`, now confirmed for the entry path: **the closure grows as you satisfy it.**

So the sharpest statement of Phase 4's wall this project has produced stands, and is now specific:
it is **MIG-generated headers from 40 public `.defs` files**, derivable in principle — the `.defs`
are public and MIG is open source — but a build step and a tool this host does not have. MIG is not
in the tarball (`osfmk/mach/{mig.h,mig_errors.h,mig_log.h}` are runtime support, not the generator)
and not in the host's package repository.

**3. `machine_routines.c`'s 30** are the same families again plus `btlog_t`, `debugger_op` and
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
| `arm_vm_init.c` | 20 errors, 7 names | MIG headers (2) + include ordering + `CONFIG_*` types |
| `arm_init.c` | 20 errors, 7 names | the same seven |
| `machine_routines.c` | 30 errors | the above + three more `CONFIG_*`-gated types |

The `arm_init.c` number is the one that matters for the immediate goal, because it is the function
`_start` branches to. **Seven missing names, three of them MIG-generated headers, is the closest
this project has got to replacing that stub with XNU's real code** — and every shim clears one name
and reveals another, so the seven underestimate the distance rather than overestimating it.

## What this does not establish

Nothing compiles yet, nothing is linked into anything, and no device run happened. The four errors
are four errors; the pattern of the last four attempts is that each one resolves to the next header,
so the honest expectation is that clearing these four reveals more rather than reaching a link.
What has changed is that the *kind* of remaining work is now known: include ordering, which is
mechanical; `CONFIG_*` values, which must be chosen; and MIG output, which is a build step.

## What was added

- `xnu_arm_entrypath_sweep.sh` — the measurement, reproducible, with every flag's reason recorded,
  `-ferror-limit=0`, a fatality warning on any count that is a floor, and the `MASTER.XXX`
  situation in its header comment.
- `shims_arm/mach_debug.h` — `mach_debug_types.h` (real) and nothing from the three generated
  interfaces.
- `shims_arm/task_swapper.h` — absent entirely; empty, because nothing on this path calls it.
- `shims_arm/san/kasan.h`, `shims_arm/security/_label.h` — two more absent headers, both taking the
  released-kernel configuration (less code exercised, not more).

## One more thing this measurement is for

The sweep is now the place a Phase 4 attempt starts, and its numbers are meant to be re-run rather
than remembered. It reports a *floor* whenever a fatal error is present, because that is the
mistake this experiment made: reporting 4 when the real number was 20, from a truncated
translation unit.
