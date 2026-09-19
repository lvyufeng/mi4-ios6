# Experiment 304 — `thread_start`, and the object is `proc_info.c` rather than `kern_prot.c`

**Step:** link `bsd/kern/proc_info.c` (`bsd_kern_proc_info.o`) — the object that defines
`bsd_setthreadname`, where the 303 run stopped.

**Prediction:** that the stop is **`thread_start`**, at `xnu_entry_stub_caller=0x8000a0a0` —
`kernel_thread_start_priority + 0x3C`, the `bl thread_start` at `0x8000A09C` — reached from
`sched_startup`'s call to `kernel_thread_start_priority`, i.e. the restored thread finishes
`idle_thread_create`, returns to `kernel_bootstrap_thread`, and runs two more of that thread's own
initialisation calls before the next name this image does not define.

**Result:** `stub_hit=thread_start` at `xnu_entry_stub_caller=0x8000a0a0`. Exact, address and
offset.

## The object is not where the name is, and this is the third time in the walk

Experiment 303's "next" said `bsd/kern/kern_prot.c` (manifest:53). `grep -rn bsd_setthreadname`
finds it in **`bsd/kern/proc_info.c:832`** (manifest:73), declared in `bsd/sys/bsdtask_info.h:122`.
`kern_prot.c` is the neighbouring BSD object, one name away, and it is where a reader would look —
which is exactly why the object is looked up and not remembered. The same mistake has now been
made with `mig_init` (in `osfmk/kern/ipc_kobject.c`, not `osfmk/ipc/mig*.c`, experiment 267),
`stackshot_init` (in `osfmk/kern/kern_stackshot.c`, not `stackshot.o`, 261), and here.

## Nine resolved, thirty-seven added

`bsd_kern_proc_info.o`: `.text` 0x3AD8, `__DATA, __data` 0xD8, `.rodata.str1.1` 0x29,
`.rodata` 0x48; **74 definitions and 112 references**.

**9 resolved**, all functions, no storage:

| name | referenced by (the reason it was a stub) |
|---|---|
| `bsd_setthreadname` | `thread.o` — `thread_set_thread_name` |
| `bsd_hasthreadname`, `get_return_to_kernel_offset_from_proc` | `thread.o` |
| `bsd_copythreadname`, `get_dispatchqueue_offset_from_proc` | `task.o` (and `thread.o` for the latter) |
| `bsd_getthreadname` | `bsd_kern.o` and `thread.o` |
| `bsd_threadcdir`, `get_dispatchqueue_serialno_offset_from_proc` | `bsd_kern.o` |
| `proc_pidpathinfo_internal` | `task_policy.o` |

Each is referenced by exactly one or two of the objects this image already links, which is why
adding this object retires all nine at once — and it is worth naming which ones, because
`thread_set_thread_name` (the call the 303 run stopped inside) is the only one of the nine on this
step's path.

**37 added — 35 functions and 2 storage**, the storage names sized from the objects that define
them as the generator requires: `dead_mountp` (`B`, 4) and `zombproc` (`B`, 4). The 35 functions
are the BSD surface this object is built out of: the vnode calls
(`vnode_lookup`, `vnode_getattr`, `vnode_getwithvid`, `vnode_vid`, `vn_stat`, `vn_getpath_fsenter`),
the context pair (`vfs_context_create`, `vfs_context_rele`), the credential and
policy calls (`kauth_getuid`, `suser`, `cansignal`, `mac_proc_check_proc_info`), the process
queries (`proc_pgrp`, `proc_puniqueid`, `proc_get_rusage`, `proc_get_originatorbgstate`,
`proc_find_zombref`, `proc_drop_zombref`, `pg_rele`, `current_uthread`), the fileport and
descriptor families (`fileport_invoke`, `fileport_walk`, `fp_getfpipe`, `fp_getfpsem`,
`fp_getfpshm`, `fp_getfsock`, `fp_getfvpandvid`, `fp_isguarded`) and the eight `fill_*` helpers
behind `proc_pidinfo`'s switch. **None of them is on this step's path at all**: the stop is in
`kernel_thread_start_priority`, and the object's own `proc_pidinfo` surface has no caller in this
image yet — which is the 251 shape (a step that brings more frontiers than it closes).

Counts: 739 → **767** undefined, 652 → **678** function stubs, 87 → **89** storage.

## The build, and every number in it was predicted

The baseline was measured rather than recalled: the step was built first with an **empty stand-in
object** in this object's place, and that build reproduces 303 exactly (739 / 652 / 87, `.text`
0x117BE0, image 1247700). So the deltas below are this object's and nothing else's.

|  | predicted | measured |
|---|---|---|
| undefined / function / storage | 767 / 678 / 89 | 767 / 678 / 89 |
| `.text` | 0x11BB00 | 0x11BB00 |
| `.data` | 0x8011C000 | 0x8011C000 |
| `__bss_start` | 0x80134AC0 | 0x80134AC0 |
| `__bss_end` | 0x8016B8D8 | 0x8016B8D8 |
| image | 1264300 | 1264300 |

**All six numbers exact, and the two interesting ones are interesting for a reason.**

`.text`: the delta is 0x11BB00 − 0x117BE0 = **0x3F20**, and the object's own contribution is
0x3AD8 (its `.text`) + 0x29 + 0x48 (its two read-only sections, which the script places inside
`.text`) = **0x3B49**; the stub set moves by −0xD8 (nine retired bodies) − 0xE7 (their nine name
strings) + 0x348 (35 new bodies) + 0x21A (their strings) = **+0x3A3**; 0x3B49 + 0x3A3 = **0x3EEC**,
against a measured 0x3F20, so **the fill term is 0x34** — the first step whose fill term is
predicted and confirmed rather than back-solved, because both sides of it (the object's sections
and the stub deltas) are known before the link.

`.data` **steps by 0x4000** rather than by the object's 0xD8: the `.data` output section is 16 KB
aligned, and `.text` crossed 0x118000 for the first time, so `__bss_start` moves by 0x40C0 (16576 bytes) and the
image by 16600 while the object contributes 216 bytes of data. That is the layout arithmetic
working, not a surprise — but it is the reason a step this small moves the image this much.

## The run

```
stub_hit=thread_start                    xnu_entry_stub_caller=0x8000a0a0
xnu_entry_bss_start=0x80134ac0           xnu_entry_bss_bytes=0x00036e18
xnu_entry_copied_bytes=0x00134aac        xnu_entry_entering_at=0x80000074
xnu_entry_kv_written=0x0000005d          xnu_entry_kv_in_dram=0x00000081
xnu_entry_kv_dropped=0x00000000
```

`tools/host_resolve_entry_addr.sh 0x8000a0a0` → `kernel_thread_start_priority+0x3c`, with
`caller-4 = 0x8000a09c`, and `0x8000a09c` is `bl 801034ac <thread_start>` in this image. So the
caller key names the call, and it is an ordinary `bl` — **the caller-key idiom is back** after
303's excursion into a thread whose first stop was in its own body.

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`),
log 301113 bytes, one `stub_hit=` line and no `exception:` line. The two KV counters differ by
0x24, which is the epilogue's own `entry_kv("xnu_entry_stub_caller_e", …)` record: `kv_written` is
read at the epilogue's entry and `kv_in_dram` after that record is appended.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`ro.build.version.release` = 10).

## What it measures

**The first thread executes a whole XNU initialisation function and gets four calls further.**
Between the 303 stop and this one, all of this ran on the hardware:

```
bsd_setthreadname          ran in full and returned
  kalloc_canblock(64)      real (250); ut->pth_name was NULL, so the allocate arm was taken
  bzero, OSCompareAndSwapPtr, strncpy, kernel_debug_string_simple   all real
thread_set_thread_name     returned to idle_thread_create+0x5c
idle_thread_create         ml_set_interrupts_enabled, lck_spin_lock/unlock, the six field stores
  thread_deallocate        RETURNED EARLY - see below
kernel_bootstrap_thread    kernel_debug_string_early("sched_startup"), no stop in its closure
sched_startup              two arm_usimple_lock_init, then kernel_thread_start_priority
kernel_thread_start_priority  kernel_thread_create, lck_mtx_lock, and the stop
```

**The finding worth keeping is `thread_deallocate`'s early return.** `idle_thread_create` ends
with `thread_deallocate(thread)`, and `thread_deallocate` is a function with a *real* `panic` in
its middle:

```c
	if (!(thread->state & TH_TERMINATE2))
		panic("thread_deallocate: thread not properly terminated\n");
```

The idle thread's state is `TH_RUN | TH_IDLE` (0x84) and `TH_TERMINATE2` is 0x20, so the panic
would fire if execution reached that test — and behind it are `kpc_thread_destroy`,
`ipc_thread_terminate`, `proc_thread_qos_deallocate` and `uthread_zone_free`, all of which would be
frontiers of their own. It does not reach the test: threads are created with
**`thread_template.ref_count = 2`** (`osfmk/kern/thread.c:244`), and the first thing
`thread_deallocate` does after the null check is

```
8000976c:  ... ldrex/strex on [r0, #124]      @ &thread->ref_count
80009770:  cmp r1, #1
80009774:  popne {r4, r5, fp, pc}             @ old ref_count != 1 -> return
```

so the decrement lands on 1 and it returns. Predicted from the source before the run, confirmed by
the stop being four calls *later*: had it not returned, the run would have ended in
`thread_deallocate`'s tail, not in `kernel_thread_start_priority`. **An idle thread is referenced
by the run queue, and that is what keeps `thread_deallocate` from tearing it down here** — a
five-line reading of the source that would otherwise have been a mis-predicted step.

The falsifiers that had to be closed, and how each was:

* `kpc_counterbuf_free` behind `kpc_thread_destroy` — closed by its own body
  (`ldr r0, [r0, #0x2f8]; cmp r0, #0; bxeq lr`, a tail branch to the free only when the thread has
  a kpc buffer) plus 299's finding that `kpc_thread_create` returns early because
  `kpc_threads_counting` is 0.
* `ast_taken_kernel` behind `_enable_preemption` — closed by the CPSR test at 0x80012968 (the AST
  path needs interrupts *enabled*) and by the `ml_set_interrupts_enabled(0)` two instructions
  before the lock in `idle_thread_create`. This is the same branch 302 closed for `splx`, from the
  other side.
* `trace_backtrace` / `btlog_add_entry` inside `zfree` — behind the global at 0x8013A46C, and
  closed by history rather than by reading: experiments 243–247 executed `zfree`'s mandatory path
  on this device with that flag zero.
* anything inside `kalloc_canblock` — `tools/entry_frontier.py`, rooted at `bsd_setthreadname` over
  the exact object list taken from the map, reports no stop before the three above.

## The record corrected in this step

Experiment 303's build section is corrected here, and the correction is recorded in
`docs/experiments/experiment-303-…md`, in its README row and in this build's own comment block. It
claimed **6 resolved / 1 added** with an added name `commpage_update_user_timebase`, and reported
measured counts **739 / 641 / 59** explained by `xnu_entry_stub_target_*` storage slots embedded in
the function stand-ins. Re-measured: `cswitch.o`'s six resolved names are
`machine_load_context`, `Call_continuation`, `Switch_context`, `Shutdown_context`, `Idle_context`,
`Idle_load_context`; all eleven of its references were real or already stubbed
(`thread_terminate` is referenced by nine linked objects);
`commpage_update_user_timebase` is referenced by nothing and **does not exist anywhere in
`external/xnu-4570.1.46/`**; the build prints 739 / 652 / 87 and `nm` on
`xnu_arm_entry_realstubs.o` gives 652 `T` + 87 `B`, with no `xnu_entry_stub_target_*` symbol in the
image. The prediction was exact and the measurement was the wrong thing — the fortieth entry in
this project's measurement-defect list.

## Next

**`osfmk/kern/thread_act.c`** (manifest:597, `osfmk_kern_thread_act.o`) — `.text` 0x1288, 38
definitions, 38 references. Measured against this image it resolves **25** names, including both
`thread_start` (this stop) and `thread_terminate` (the name 303 named as its falsifier), and adds
**5**: `extmod_statistics_incr_thread_set_state`, `thread_affinity_dup`,
`thread_affinity_terminate`, `thread_depress_abort_internal`, `thread_exception_return`.

The prediction is not a lookup this time: `thread_start`'s body must be read in the image the run
used, and `thread_start_in_assert_wait` is the other candidate in the same object for a thread
being started this way.
