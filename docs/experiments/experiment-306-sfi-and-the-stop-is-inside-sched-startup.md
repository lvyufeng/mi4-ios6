# Experiment 306 — `sfi.c`, and the stop is inside `sched_startup`'s own body

**Step:** link `osfmk/kern/sfi.c` (`osfmk_kern_sfi.o`, manifest:583) — the object that defines
`sfi_thread_classify`, the name 305 stopped on.

**Prediction:** `stub_hit=ast_on` at `xnu_entry_stub_caller=0x8009e61c` (= `thread_setrun + 0x9c8`,
the `bl ast_on` at 0x8009e618). This *replaced* 305's closing prediction of
`device_service_create` (0x8000e59c), on the grounds that `thread_go` calls
`thread_setrun(thread, SCHED_PREEMPT | SCHED_TAILQ)` — `options` is 5, not 0 — so the last arm of
the inlined `processor_setrun`'s preempt chain gives `preempt = AST_PREEMPT` for a `TH_MODE_FIXED`
kernel thread, `processor->state` is `PROCESSOR_RUNNING`, `thread->sched_pri` and
`processor->current_pri` are both 95, and `csw_check_locked` therefore returns `AST_PREEMPT`.

**Result:** `stub_hit=ast_off` at `xnu_entry_stub_caller=0x800a04c4` = **`thread_block_reason + 0x44`**
— the `bl ast_off` at 0x800a04c0, with `r0 = 31` (`AST_SCHEDULING`). **Neither the prediction nor
305's: the next stop is not in the scheduler's dispatch path at all, it is the first thing
`sched_startup`'s own `thread_block(THREAD_CONTINUE_NULL)` does** — three statements after the
thread was put on a run queue.

## 2 resolved, 0 added

`osfmk_kern_sfi.o`: **84 bytes of `.text` and nothing else** — no `.data`, no `.bss`, no
`.rodata`. 8 definitions, 1 reference. `CONFIG_SCHED_SFI` is 0 in this configuration,
`#if CONFIG_SCHED_SFI` wraps lines 53–1091, and only the `#else` arm compiles: six functions
returning `KERN_NOT_SUPPORTED` (`sfi_set_window`, `sfi_window_cancel`, `sfi_get_window`,
`sfi_set_class_offtime`, `sfi_class_offtime_cancel`, `sfi_get_class_offtime`), a void
`sfi_reevaluate`, and

```c
sfi_class_id_t sfi_thread_classify(thread_t thread)
{
	task_t task = thread->task;
	boolean_t is_kernel_thread = (task == kernel_task);
	if (is_kernel_thread) { return SFI_CLASS_KERNEL; }
	return SFI_CLASS_OPTED_OUT;
}
```

**2 resolved** (`sfi_thread_classify`, this stop, and `sfi_reevaluate`), **0 added** — its one
undefined name, `kernel_task`, is defined by the linked `task.o` — and the other six definitions are
names nothing in the image references yet. Counts: 747 → **745** undefined, 658 → **656** function
stubs, 89 storage unchanged. A step whose object is 0x54 bytes is the honest shape of this frontier:
the walk has arrived at the run-queue insertion, and what this kernel is missing there is small.

## The build, and `.text` did not move

Every number here was measured against a **baseline built in the same session**: the step built with
an empty stand-in object in this slot, which reproduces 305's stub set exactly (747 undefined,
658 + 89 stubs).

|  | predicted | measured |
|---|---|---|
| undefined / function / storage | 745 / 656 / 89 | 745 / 656 / 89 |
| `.text` | 0x11CAA0 (Δ = +0x54 − 0x30 − 0x24 = 0) | **0x11CAA0** |
| `.data` | 0x80120000 | 0x80120000 |
| `__bss_start` | 0x80138AC0 | 0x80138AC0 |
| `__bss_end` | 0x8016F8D8 | 0x8016F8D8 |
| image | 0x138AAC | 0x138AAC |
| headroom | 1640232 | 1640232 |

`realstubs.o` carries the change as usual — `.text` 0x3D80 = 656 × 24 — and the objects' `.text`
stops at 0x11CAA0 while `.data` is still 16 KB-aligned at 0x80120000, so the padding in front of it
absorbs the shift and everything from `.data` up is 305's layout unchanged.

**`.text` is unchanged, and the arithmetic is why:** +0x54 for this object, −2 stub bodies (0x30),
−2 stub name strings (`sfi_thread_classify` = 20 bytes and `sfi_reevaluate` = 16, each 4-aligned in
`.rodata.str1.4`, which this link places *inside* `.text`) = **exactly zero**.

That equality is also a defect of the 305 record, and it is worth stating plainly: **the same stub
set rebuilt here measures `.text` 0x11CAA0, 0x40 below the 0x11CAE0 that 305's ledger and
experiment doc record**, and a configuration whose delta is exactly zero cannot have differed from
it. The 305 doc's own image figure has the same shape of problem in the other direction (it writes
`0x138AAC = 1281452`; 0x138AAC is 1280684, which is what the device reported and what this build
reports). A recorded build number that no longer rebuilds is a reading, not a result — the baseline
for a delta has to be built in the same session as the delta.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ast_off
 xnu_entry_stub_caller=0x800a04c4
 xnu_entry_stub_caller_v=0x800a04c4       xnu_entry_stub_caller_a=0x800a04c4
 xnu_entry_stub_caller_e=0x800a04c4       xnu_entry_stub_caller_digits=0x0000002b
 xnu_entry_bss_start=0x80138ac0           xnu_entry_bss_end=0x8016f8d8
 xnu_entry_bss_bytes=0x00036e18           xnu_entry_copied_bytes=0x00138aac
 xnu_entry_entering_at=0x80000074
 xnu_entry_kv_written=0x00000058          xnu_entry_kv_in_dram=0x0000007c
 xnu_entry_kv_dropped=0x00000000
```

`tools/host_resolve_entry_addr.sh 0x800a04c4` → `thread_block_reason+0x44`, and `caller-4 =
0x800a04c0` is `mov r0, #31` / `bl 80101720 <ast_off>` in the image the device ran. In
`sched_prim.c`:

```c
	processor = current_processor();
	if (reason & AST_YIELD)
		processor->first_timeslice = FALSE;
	/* We're handling all scheduling AST's */
	ast_off(AST_SCHEDULING);
```

and `thread_block(continuation)` is a macro for `thread_block_reason(continuation, NULL, AST_NONE)`,
so this is `sched_startup`'s `thread_block(THREAD_CONTINUE_NULL)` — the call `sched_startup` makes
after `kernel_thread_start_priority` returns, `thread_deallocate` runs and `assert_thread_magic`
passes.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, watchdog ARMED), log 301108 bytes, one
`stub_hit=` line and no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`ro.build.version.release` = 10).

## What it measures, and what it does not

**It measures that the whole thread-start path returns.** For the run to stop at
`thread_block_reason` inside `sched_startup`, control had to come back through `thread_setrun` →
`thread_go` (inlined in `clear_wait_internal`) → `clear_wait` → `thread_start` →
`kernel_thread_start_priority` → `thread_deallocate`. Combined with 305, the reading is: the first
thread this kernel starts is **put on a run queue** — the enqueue in the inlined `processor_setrun`
is unconditional on the non-RT arm, and 95 < `BASEPRI_RTQUEUES` (97) — and then **the bootstrap
thread keeps the processor**; the enqueue did not preempt it.

**It does not say which gate closed**, and the prediction named only one of the three. What is
established by reading the compiled inlined `processor_setrun` is that `preempt` *is* `AST_PREEMPT`
(`mov r1, #5` at both `thread_setrun` call sites; `sched_multiq_initial_thread_sched_mode` returns
`TH_MODE_FIXED` for `kernel_task`, so the timeshare arm at 0x8009DFC8 is skipped and the net is
`ubfx r2, r6, #2, #1` at 0x8009E044 = bit 2 of `options`). The survivors are:

* **(a)** `processor->state` is 2 or 3 — the preempt jump table sends those straight to the exit
  (0x8009E61C) with no AST;
* **(b)** `processor->state == PROCESSOR_DISPATCHING` (5) — that arm (0x8009E0EC) tests
  `processor->next_thread == THREAD_NULL` and then `current_pri < sched_pri`, and returns without an
  AST either way;
* **(c)** the state is `PROCESSOR_RUNNING` and `csw_check_locked` returns `AST_NONE`, which needs
  `MAX(main_entryq->highq, bound_runq->highq) < processor->current_pri`, or
  `processor->first_timeslice == TRUE` — which flips the comparison in
  `sched_multiq_processor_csw_check` from `>=` to `>`, and at equal priority 95 > 95 is false.

`state`, `highq`, `current_pri` and `first_timeslice` are four values this instrument does not print.
**The run measures the branch and not the variable** — settling it needs a *probe* (a checkpoint
that prints those four at `thread_setrun`'s entry), not another object, and the frontier does not
need it: the object the run named is `ast.c`.

## Why both readings missed it

The chain the device took is

```
kernel_bootstrap_thread        startup.c
  -> sched_startup             sched_prim.o
    -> thread_block            macro for thread_block_reason      ** intra-object **
      -> thread_block_reason   sched_prim.o
        -> ast_off             ast.o                              THE STOP
```

— the same shape as 305's miss (`sched_startup -> thread_block_reason` is an *intra-object* edge,
invisible to the walker before 305 fixed it). But the walker is not the culprit this time: **the
fixed walker does reach `ast_off`.** What it does not do is report it as the frontier, and the
reason is its visited set. `seen` is global and first-visit-wins, so a chain through
`panic -> panic_trap_to_debugger -> kdbg_dump_trace_to_file -> ... -> thread_block ->
thread_block_reason` claims `thread_block_reason` before the legitimate chain gets there; `ast_off`
is then attributed to that panic chain, and lands **40th** in the list (`device_service_create` is
313th, and reached as a *direct* call of `kernel_bootstrap_thread`).

Pruning the panic subtree was tried against the same object set and is **not enough**: 567 stops
become 243 and `ast_off` moves to 9th, but the list is then led by `ast_taken_kernel`, `ast_on`,
`PE_cpu_signal_deferred`, … — branches the tool's own docstring says it walks without modelling. So
the tool is unchanged in this step, and the honest conclusion is the docstring's own line applied
one level up: *the answer is a candidate, and the run is what decides*. **The tool chose the object
(it had `ast_off` in its list); the run named the call; the hand-reading that replaced the tool's
ordering with a derivation from `preempt`/`has_higher` is what was wrong.** The next step should use
the walk as a candidate list and say so, rather than promote one candidate to a prediction.

## Next

**`osfmk/kern/ast.c`** (`osfmk_kern_ast.o`, manifest:534) — the object that defines `ast_off`, the
name this run stopped on. `.text` **0x440**, 11 definitions, 22 references; it resolves **6**
(`ast_check`, `ast_context`, `ast_off`, `ast_on`, `ast_propagate`, `ast_taken_kernel`) and adds
**2** (`bsd_ast`, `kperf_kpc_thread_ast`), so 745 → **743** undefined and 656 → **654** function
stubs.

The prediction, stated as a candidate rather than a claim: with `ast_off` real, `thread_block_reason`
runs on into `thread_lock`, `thread_select` and `thread_invoke` — all real code in `sched_prim.o`
except what they call through the dispatch table — so the next stop is likely one of the names those
reach that this image still stubs. The walk after this step reports, in its own (panic-ordered)
list, the candidates `PE_cpu_signal_deferred`, `ast_taken_kernel`'s neighbours, and — most
interesting for the frontier — whatever `thread_select`/`thread_invoke` ask for that no linked
object provides; the run will name it.
