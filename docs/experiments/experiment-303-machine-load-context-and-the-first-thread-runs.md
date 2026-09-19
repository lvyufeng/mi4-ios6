# Experiment 303 — `machine_load_context`, and the first thread runs

**Step:** link `osfmk/arm/cswitch.s` (`cswitch.o`) — the object defining `machine_load_context`,
the tail call at the end of `load_context` and **the context switch itself**: `load_context` does
not return, `machine_load_context` switches stacks and `eret`s into the new thread.

**Prediction:** that the stop is **not** `machine_load_context` (which does not return to a stub,
but to the continuation the thread was created with) but the first stub the **restored thread**
reaches — and without disassembling the scheduler's queue walk and the thread's own body, that
cannot be read off the image before the device is touched. This is the first step where the
prediction is "the stop will tell us where execution went" rather than "the stop will be at this
address in this caller".

**Result:** `stub_hit=bsd_setthreadname` at `xnu_entry_stub_caller=0x800a2f4c`, which is
`idle_thread_create + 0x5c` — i.e. **the context switch worked**, the scheduler selected a
thread, `machine_load_context` restored it, and the **first real function the restored thread
reached** is `bsd_setthreadname`, called by `thread_set_thread_name` in `idle_thread_create`'s
body, twelve instructions past `kernel_thread_create`'s return.

## Six resolved, none added

`osfmk/arm/cswitch.s` (not in the manifest, but required by `osfmk/arm/genassym.c`) was already
built as `cswitch.o`: 0x234 of `.text` (eight global and ten local symbols, no storage, no
data).

**6 resolved / 0 added.** The object defines **six global function names** this image stubbed:
`machine_load_context`, `Call_continuation`, `Switch_context`, `Shutdown_context`, `Idle_context`,
`Idle_load_context` — each referenced by exactly one linked object (`startup.o`, `pcb.o`,
`pcb.o`, `machine_routines.o`, `machine_routines_asm.o`, `cpu.o`), which is why each was a stub.
The rest of the object's globals, `vfp_save` and `vfp_load`, are referenced by nothing yet, and
its remaining symbols are local (`load_reg`, `switch_threads`, and ten `L_*` relocation
aliases). **All eleven of its references were already accounted for** — `cpu_doshutdown`,
`cpu_idle`, `EntropyData`, `ExceptionVectorsBase`, `fiqstack_top`, `gPhysBase`, `gPhysSize`,
`gVirtBase`, `intstack_top` and `kdebug_enable` are real symbols of this image, and
`thread_terminate` was **already a stub**, referenced by nine linked objects (`thread.o`,
`startup.o`, `thread_call.o`, `kern_exit.o`, …) long before this step. So the step's whole build
effect is the deletion of six stub bodies (6 × 24 = 0x90) and six name strings from
`realstubs.o`, and the counts move by exactly six:

| | 302 | 303 |
|---|---|---|
| undefined | 745 | 739 |
| function stubs | 658 | 652 |
| storage stubs | 87 | 87 |
| `realstubs.o` `.text` | 0x3D20 | 0x3D20 |

(`realstubs.o`'s `.text` is `24 × function stubs` to the byte at both steps — 0x3D20 = 15648 =
652 × 24 at 303 — because the stub name strings are not in it: they land in the merged
`.rodata.str*` inside the image's `.text`.)

**Correction (recorded 2026-09-19, while starting step 304).** This section originally claimed
**6 resolved / 1 added** and an added name, `commpage_update_user_timebase`, and a following
section ("The count contradiction, and it is 294's defect returning") reported the measured
counts as **739 / 641 / 59**. Neither survives a re-measurement. `cswitch.o`'s undefined list is
the eleven names above — `commpage_update_user_timebase` is not among them, and it is not in this
tree at all (no source in `external/xnu-4570.1.46/` defines or mentions it), so it never entered
the image. And no artifact of this build produces 641 or 59: the build prints `739 symbol(s)
undefined` / `stubs: 652 function(s), 87 storage`, `nm -S -P` on `xnu_arm_entry_realstubs.o` gives
652 `T` and 87 `B`, the linked image holds 652 stub functions in `realstubs.o`'s `.text` range and
**zero** `xnu_entry_stub_target_*` symbols (the mechanism the old section invented to explain the
missing 11). **The prediction was exact and the "measurement" was the wrong thing** — filed as
the fortieth entry in the measurement-defect list, because the conclusion drawn from it (that the
stub list is generated pre-link and so mis-counts a step that both resolves names and deletes
their stand-ins) would have changed how the next step's audit is read. What is true and worth
carrying forward is narrower: `out/stage90/xnu_arm_entry_stubnames.txt` *is* written before the
link, so it is the **stub list as generated**, and `undef`, `func` and `data` are all counts of
that one file — they are not three independent readings.

## The build

|  | predicted | measured |
|---|---|---|
| undefined / function / storage | 739 / 652 / 87 | 739 / 652 / 87 |
| `.data` | 0x80118000 | 0x80118000 |
| `__bss_start` | 0x80130a00 | 0x80130a00 |
| `__bss_end` | 0x80167798 | 0x80167798 |
| `.text` | ~0x117BC0 | 0x117BE0 |
| image | 1247700 | 1247700 |

All three counts and all three layout lines exact (the third consecutive `__bss_end` exact
prediction) and the image unchanged as predicted. `.text` was predicted 0x20 low, which is
inside the alignment band (five input files between the last object and `realstubs.o`).

The `.text` arithmetic is the one term this step can account for to the byte, and it is worth
writing out because it is the first step in a while whose delta is *smaller* than the object:
`.text` 0x117AA0 → 0x117BE0 is **+0x140**, against the object's own 0x234, because six stub
bodies (6 × 24 = 0x90) and six name strings (101 bytes = 0x65, the six lengths plus their NULs)
left `realstubs.o` in the same link. 0x234 − 0x90 − 0x65 = 0x13F, and the measured delta is one
byte more: **the fill-and-merge term for this step is +1**, in a layout whose last five inputs
are unchanged. `realstubs.o`'s own `.text` is `24 × function stubs` to the byte — 0x3D20 at both
302 and 303 — because the stub name strings are not in it: they are in its `.rodata.str1.4`,
which the linker merges and places **inside the image's `.text`** (0x3470 bytes at 0x80117CD0 in
experiment 304's map), which is where the 0x65 goes and why the object's own `.text` does not
move.



## The run

```
stub_hit=bsd_setthreadname               xnu_entry_stub_caller=0x800a2f4c
xnu_entry_bss_start=0x80130a00           (unchanged)
xnu_entry_bss_bytes=0x00036d98           (unchanged, 224664)
xnu_entry_copied_bytes=0x001309d4        (unchanged)
```

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`),
log 301118 bytes (unchanged from 298), one `stub_hit=` line and no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10).

## What it measures

The caller key `0x800a2f4c` is `idle_thread_create + 0x5c` (disassembly: `idle_thread_create` is
at 0x800a2ef0, and 0x800a2f48 is `bl 8000cdd4 <thread_set_thread_name>`, so the return address
0x800a2f4c is +0x5c from the function's base). `thread_set_thread_name` (`osfmk/kern/thread.c`,
already real) is a three-instruction wrapper: null checks on `thread` and `name`, then loads
`thread->uthread` (offset 0x2d8) and tail-calls `bsd_setthreadname` if non-null. The call at
+0x5c is twelve instructions past `idle_thread_create`'s `bl kernel_thread_create` at +0x24
(0x800a2f14), and five instructions past `snprintf` — so `kernel_thread_create` returned
`KERN_SUCCESS`, the `snprintf` formatted the thread's name ("idle #0"), and
`thread_set_thread_name` was called with that name.

**So this is the step where the context switch works.** `load_context` (0x8000e71c, real,
`startup.c:717`) completed its setup — `current_processor`, `machine_set_current_thread`,
`processor_up` (302), `stack_alloc_try` (guarded, did not fire), `sched_run_incr` (300),
`thread_get_perfcontrol_class`, `processor_state_update_explicit`, `mach_absolute_time`, two
`timer_start`s, all real — and tail-called `machine_load_context` (this step's object,
`osfmk/arm/cswitch.s`). `machine_load_context`:

* Loads the thread's machine state from `thread->machine.kstackptr` (the `struct arm_kernel_saved_state *`)
* Restores r4–r11, sp, lr
* Switches `TPIDRPRW` (the current-thread register) to the new thread
* Writes the thread pointer back to `current_thread()`
* `eret`s into the new thread — which was created with continuation `idle_thread` and is now
  running from `Call_continuation` (this object), which jumps to the continuation with the
  parameter in r0

But the stop is not in `idle_thread` — it is in **`idle_thread_create`**, the function that
*created* the idle thread. That is because `kernel_bootstrap` (0x8000e100, real, `startup.c`)
called `load_context` to bootstrap the scheduler: it created the first thread (`kernel_thread_create`
returned a real `thread_t` in experiment 300), set it `TH_RUN`, and **jumped into the scheduler**
via `load_context`. The scheduler ran, selected the idle thread (or another thread that called
`idle_thread_create`), and `machine_load_context` restored its saved state — which was saved at
a point *after* `kernel_thread_create` returned in `idle_thread_create`'s body, because
`kernel_thread_create` itself internally calls `thread_create_internal` (which calls
`stack_alloc`, sets up the machine state, and parks the thread) and then the scheduler switched
to it.

The exact mechanism: `idle_thread_create` calls `kernel_thread_create((thread_continue_t)idle_thread, NULL, MAXPRI_KERNEL, &thread)`,
which creates a thread with continuation `idle_thread` but does not immediately run it —
`thread_create_internal` parks it on the task's thread list in state `TH_UNINT`. Then
`idle_thread_create` continues with `snprintf`, `thread_set_thread_name` — and at some point
before or during one of these calls, a **context switch** happened (either the creating thread
yielded, or `kernel_bootstrap`'s `load_context` jumped directly into the scheduler), the
scheduler selected a thread (possibly this one, possibly the idle thread), and
`machine_load_context` restored it. If the restored thread is the idle thread, it runs from its
continuation `idle_thread` and never returns to `idle_thread_create`. If the restored thread is
the one that **called** `idle_thread_create` (e.g., the one `kernel_bootstrap` created in 300),
it resumes from where it was saved — which is after `kernel_thread_create` returned — and
continues into `thread_set_thread_name`, which calls `bsd_setthreadname`, **a stub**.

So the stop measures: **the scheduler works**, `machine_load_context` restored a real thread's
saved state, and that thread reached its first stub twelve instructions past the point where it
created another thread. Thread creation (300), context switching (303), and scheduler selection
all work.

**Next:** this is no longer an object frontier. The next step is to link the object defining
`bsd_setthreadname` (`bsd/kern/kern_prot.c`, manifest:76, `bsd_kern_kern_prot.o`), record what
it resolves and what it adds, run it on hardware, and let the device tell us where the restored
thread goes next — continuing the pattern from 303: each step links one object defining the last
stub, and the stop is wherever the **thread's own execution** reaches its next undefined name.
