# Experiment 305 — `thread_act.c`, and the prediction is wrong by one call inside one object

**Step:** link `osfmk/kern/thread_act.c` (`osfmk_kern_thread_act.o`, manifest:597) — the object that
defines `thread_start`, the name 304 stopped on.

**Prediction:** `stub_hit=device_service_create` at `xnu_entry_stub_caller=0x8000e59c`, on the grounds
that with `thread_start` real the bootstrap thread's own straight line from `sched_startup` to
`clock_service_create` contains no instance of an undefined name — the two that *are* in it
(`btlog_create` behind the `zlog` boot-arg guard, `ipc_clock_init`/`ipc_clock_enable` behind
`clock_count`) are both gated shut, and `device_service_create` is an unconditional `bl` at
0x8000E598. The named falsifier was `compute_averages`, reachable only if the bootstrap thread were
switched away from at `sched_startup`'s `thread_block`.

**Result:** `stub_hit=sfi_thread_classify` at `xnu_entry_stub_caller=0x8009dc90` =
`thread_setrun + 0x3c`. **Neither the prediction nor its falsifier: the stop is one call further
along a chain the prediction's model could not see**, and the chain is inside a single object.

## 25 resolved, 5 added

`osfmk_kern_thread_act.o`: `.text` 0x1288, `.rodata.str1.1` 0x34, no `.data`, no `.bss`; **38
definitions and 38 references**. The object is where the name is this time — `thread_start` is in
this file — which is worth stating because the two preceding steps each cost a lookup:
`bsd_setthreadname` lives in `proc_info.c` and not `kern_prot.c` (304), `machine_load_context` in
`cswitch.s` (303).

**25 resolved**, all functions, no storage:

| group | names | why they were stubs |
|---|---|---|
| the stop and its neighbours | `thread_start`, `thread_start_in_assert_wait` | `thread.o`, `sched_prim.o` |
| termination | `thread_terminate`, `thread_terminate_internal` | **nine** linked objects reference `thread_terminate` (`thread.o`, `startup.o`, `thread_call.o`, `kern_exit.o`, …) — 303's own "next" named it as its falsifier |
| the state API | `thread_abort`, `thread_abort_safely`, `thread_hold`, `thread_release`, `thread_suspend`, `thread_resume`, `thread_get_state`, `thread_set_state_from_user`, `thread_depress_abort`, `thread_dup`, `thread_dup2`, `thread_info` | `task.o`/`thread.o`/`mach` surfaces that have been stubbed since those objects arrived |
| the AST setters | `thread_apc_ast`, `set_astledger`, `act_get_state`, `act_set_astbsd`, `act_set_astkevent`, `act_set_astmacf`, `act_set_kperf`, `act_set_io_telemetry_ast`, `act_set_state_from_user` | referenced through `ast.c`/`sched_prim.o` |

**5 added, all functions and no storage.** `nm -S --defined-only` over the built pool reports `T`
for every one, so the generator takes no size from a defining object and none costs one of the
64-byte storage slots: `extmod_statistics_incr_thread_set_state` (`extmod_statistics.o`),
`thread_affinity_dup` and `thread_affinity_terminate` (`affinity.o`),
`thread_depress_abort_internal` (`syscall_subr.o`), `thread_exception_return` (`locore.o`).

Counts: 767 → **747** undefined, 678 → **658** function stubs, 89 storage.

## The build, and every number in it landed

The baseline was measured rather than recalled: the step was built first with an **empty stand-in
object** in this object's place, and that build reproduces 304 exactly — 767 / 678 / 89, `.text`
0x11BB00, image 1264300, `__bss_start` 0x80134AC0, `__bss_end` 0x8016B8D8, headroom 1656616. So the
deltas below are this object's and nothing else's.

|  | predicted | measured |
|---|---|---|
| undefined / function / storage | 747 / 658 / 89 | 747 / 658 / 89 |
| `.text` | 0x11CABC + fill (band 0x11CAC0..0x11CB00) | **0x11CAE0** |
| `.data` | 0x80120000 | 0x80120000 |
| `__bss_start` | 0x80138AC0 | 0x80138AC0 |
| `__bss_end` | 0x8016F8D8 | 0x8016F8D8 |
| image | 0x138AAC = 1281452 | 1281452 |
| headroom | 1640232 | 1640232 |

`.text`: +0x1288 + 0x34 for the object, −25 stub bodies (0x258) and their 25 name strings (0x1AC),
+5 bodies (0x78) and their 5 strings (0x8C) = **+0xFBC**, so 0x11CABC before fill and 0x11CAE0
after: **the fill term is 0x24**, inside the band and against 0x12 (301), 0x55 (302) and 0x34 (304).
`realstubs.o` carries the change visibly — `.text` 0x3DB0 = 658 × 24 and `.rodata.str1.4` 0x3330.

`.data` **steps by 0x4000** for 304's reason: `.text` crosses 0x11C000 and the `.data` output
section is 16 KB aligned. Everything above it is 304's layout moved by that step and nothing else —
the file-backed data is unchanged because this object has none and its `.rodata.str1.1` is placed
inside `.text`, and the 0x36E18 of `.bss` is unchanged because 89 storage stand-ins is unchanged.
So the six numbers are one alignment step plus one object, and all six were right.

## The run

```
stub_hit=sfi_thread_classify             xnu_entry_stub_caller=0x8009dc90
xnu_entry_stub_caller_v=0x8009dc90       xnu_entry_stub_caller_a/e=0x8009dc90
xnu_entry_bss_start=0x80138ac0           xnu_entry_bss_end=0x8016f8d8
xnu_entry_bss_bytes=0x00036e18           xnu_entry_copied_bytes=0x00138aac
xnu_entry_entering_at=0x80000074         xnu_entry_kv_written=0x00000064
xnu_entry_kv_in_dram=0x00000088          xnu_entry_kv_dropped=0x00000000
```

`tools/host_resolve_entry_addr.sh 0x8009dc90` → `thread_setrun+0x3c`, with `caller-4 =
0x8009dc8c` and `0x8009dc8c` is `bl 801040fc <sfi_thread_classify>` in this image. Both on-device
layout pairs the run prints are the predicted ones (`bss_start`, `bss_end`) and the copied length is
the predicted image length.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, watchdog ARMED, no storage symbols in the
payload), log 301120 bytes, one `stub_hit=` line and no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`ro.build.version.release` = 10).

## What went wrong with the prediction, and it is the tool rather than the reading

The prediction came from `tools/entry_frontier.py`, which walks the linked objects' call graph. Run
from `thread_start` it reported **two** stops, both `ast_taken_kernel` behind
`ml_set_interrupts_enabled` — "no stop in `clear_wait`'s closure" — and that is what the falsifiers
were written against. The device measured a stop in that closure.

The reason is in the walker's own call extraction. It read the object files with `objdump -dr` and
followed calls written `bl 0 <name>` — the form a call to a symbol **the object does not define**
takes, where the relocation is unapplied. A call to a symbol **the same object defines** prints as

```
     920:	ebfffffe 	bl	94c <thread_unblock>
     938:	ebfffffe 	bl	c24 <thread_setrun>
			938: R_ARM_CALL	thread_setrun
```

— the resolved local address, not `0` — so it did not match and was not followed. Every edge
between two functions of one object was invisible, and the frontier the run measured is behind
exactly such edges:

```
sched_startup                  osfmk_kern_sched_prim.o
  -> kernel_thread_start_priority   osfmk_kern_thread.o        cross-object, followed
    -> thread_start                 osfmk_kern_thread_act.o    cross-object, followed
      -> clear_wait                 osfmk_kern_sched_prim.o    cross-object, followed
        -> clear_wait_internal      osfmk_kern_sched_prim.o    ** intra-object, invisible **
          -> thread_go (inlined)    osfmk_kern_sched_prim.o    ** intra-object, invisible **
            -> thread_setrun        osfmk_kern_sched_prim.o    ** intra-object, invisible **
              -> sfi_thread_classify   osfmk_kern_sfi.o        THE STOP
```

`sched_startup -> kernel_thread_start_priority` was visible in 304 only because those two functions
are in *different* objects (`sched_prim.o` and `thread.o`), which is why 304's prediction of the next
stop was exact and this one was not. It has been invisible for as long as the tool has existed; it
mattered here because the last four hops of the chain happen to live in one file.

**The tool is fixed in this step** (`tools/entry_frontier.py`): the pattern now accepts any target
address and skips local labels (`foo+0x10`, `.LBB1_2`), which are branches inside a function rather
than calls. Re-run against the same object set, the walk from `kernel_bootstrap_thread` now reports
`sfi_thread_classify` — and 371 stub stops instead of 133, because following intra-object edges also
walks the error paths that only run on a panic. That is the trade the tool's own docstring already
describes ("an error path that only runs on a failure the device does not take is walked anyway"),
and it is why the **run** is the measurement and the walk is a candidate list.

## What it measures

**`thread_start`'s body runs, and the first thread this kernel starts is woken and put on a run
queue.** Between 304's stop and this one:

```
thread_start               clear_wait(thread, THREAD_AWAKENED)   <- the new name, and it runs
clear_wait                 splsched, thread_lock, clear_wait_internal, thread_unlock, splx
clear_wait_internal        waitq is NULL, so no waitq_pull; (state & (TH_WAIT|TH_TERMINATE)) == TH_WAIT
thread_go (inlined)        thread_unblock, then thread_setrun(thread, ...)
thread_setrun              SCHED() dispatch table, then bl sfi_thread_classify   <- the stop
```

The stop is `thread_setrun`'s **first** call: `thread->sfi_class = sfi_thread_classify(thread)`. So
the run measures that a thread created by `kernel_thread_start_priority` reaches the run queue, and
that the first thing standing between this kernel and a scheduled thread is the SFI classification
in `sfi.c` — assembled here in its `!CONFIG_SCHED_SFI` arm, where `sfi_thread_classify` is eight
instructions comparing `thread->task` with `kernel_task`.

The falsifiers the prediction named, and where each stands now:

* `compute_averages` (`sched_timeshare_maintenance_continue`'s first undefined call, a stub) — **not
  reached**, which is the reading it was written to test: `sched_startup`'s `thread_block` does not
  put a `TH_RUN`-only thread off the processor, so `sched_init_thread` never ran its body.
* `device_service_create` — **not reached yet**. It is not refuted by this run: it is the same
  prediction, one undefined name further along, and the next run tests it.
* `btlog_create` and the `ipc_clock_*` pair — the gates are as predicted (boot-args carry no `zlog`;
  `clock_count` is still one of the 89 storage stand-ins), so neither is a candidate.

## Next

**`osfmk/kern/sfi.c`** (manifest:583, `osfmk_kern_sfi.o`) — it resolves **2** names
(`sfi_thread_classify`, this stop, and `sfi_reevaluate`), adds **0** (its single reference,
`kernel_task`, is real), so 747 → **745** undefined and 658 → **656** function stubs. The object is
**0x54 bytes**: `CONFIG_SCHED_SFI` is 0 in this configuration, `#if CONFIG_SCHED_SFI` wraps
everything up to line 1092, and only the `#else` arm compiles — eight functions that return
`KERN_NOT_SUPPORTED` and one `sfi_thread_classify` that classifies the thread by whether its task is
`kernel_task`. **A step whose object is 0x54 bytes is the honest shape of this frontier**: the walk
has arrived at the scheduler's run-queue insertion, and what this kernel is missing there is small.

The prediction for its stop: **`device_service_create` at 0x8000e59c**, unchanged, with the two
branches inside `thread_setrun`'s tail named as the alternatives — `ast_on` (0x8009e5d0 and
0x8009e618, reached only when `preempt != AST_NONE`) and `PE_cpu_signal_deferred` (behind
`machine_signal_idle` at 0x8009e6d4, reached only when the processor is `PROCESSOR_IDLE`). The
processor here is running the bootstrap thread at MAXPRI_KERNEL, and the thread being set running is
at the same priority, so both readings are that neither gate opens.
