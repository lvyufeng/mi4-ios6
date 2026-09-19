# Experiment 307 — `ast.c`, and the candidate is the first call `thread_invoke` makes

**Step:** link `osfmk/kern/ast.c` (`osfmk_kern_ast.o`, manifest:534) — the object that defines
`ast_off`, the name 306 stopped on.

**Candidate:** `stub_hit=mt_sched_update` at `xnu_entry_stub_caller=0x800a0c14` (= `thread_invoke +
0x74`, the `bl` at 0x800a0c10 — the **first** call `thread_invoke` makes, and the one the walker's
guarded-site list put first). Stated as a candidate with a falsifier list, because 306's closing
paragraph asked for that and 306 is the step where a hand-reading promoted to a prediction was
refuted.

**Result:** exactly that — `stub_hit=mt_sched_update` at `xnu_entry_stub_caller=0x800a0c14`. The
first step in this walk whose prediction came from the walker rather than from a reading of the
scheduler, and the first in a while that landed on the nose.

## 6 resolved, 2 added

`osfmk_kern_ast.o` is **0x440 bytes of `.text` and nothing else** — `arm-none-eabi-size -A` reports
`.text` 1088 plus `.comment`, `.note.GNU-stack` and `.ARM.attributes`: no `.data`, no `.bss`, no
`.rodata` section of its own — with **11 definitions and 22 references**.

**6 resolved**: `ast_check`, `ast_context`, `ast_off` (306's stop), `ast_on`, `ast_propagate`,
`ast_taken_kernel`. **2 added**: `bsd_ast`, `kperf_kpc_thread_ast`, both `R_ARM_CALL` references, so
both function stubs. Counts: 745 → **741** undefined, 656 → **652** function stubs, 89 storage
unchanged.

Four of its definitions (`ast_consume`, `ast_dtrace_on`, `ast_peek`, `ast_taken_user`) and its local
`thread_preempted` are names nothing in this image references yet, so 7 of its 11 functions link
unreferenced. That is the same shape 306's 0x54-byte object had, from the other side: a file whose
remaining callers arrive later in the walk.

What the stop at the other end of that was is worth quoting, because it is the scale of the whole
step:

```c
void
ast_off(ast_t reasons)
{
	ast_t *pending_ast = ast_pending();

	*pending_ast &= ~reasons;
}
```

306's stop was a two-statement function that clears a bit in the pending-AST word.

## The count slip, which was inherited

The block written before this build said **743** undefined and **654** function stubs. The arithmetic
is 745 − 6 + 2 = **741** and 656 − 6 + 2 = **652**, which is what the build reports. The wrong
numbers were copied from 306's own "Next" paragraph, which carries the same slip — so the error was
inherited rather than made here, and it is the *second* time a number in this walk has been copied
forward instead of recomputed (the first is 305's `.text` 0x11CAE0, which does not rebuild). The
lesson is the same both times: a prediction is an input to the next step only after it has been
recomputed from the current state.

## The build, and the fill term pays 0x14

The baseline was built in this session, with an **empty stand-in object** in this slot: it reproduces
306 exactly — 745 / 656 / 89, `.text` 0x11CAA0, image 0x138AAC, bss 0x80138AC0..0x8016F8D8,
headroom 1640232 — so the deltas are this object's and nothing else's.

|  | predicted | measured |
|---|---|---|
| undefined / function / storage | 741 / 652 / 89 | 741 / 652 / 89 |
| `.text` | 0x11CE54..0x11CEA0 | **0x11CE40** |
| `.data` | 0x80120000 | 0x80120000 |
| `__bss_start` | 0x80138AC0 | 0x80138AC0 |
| `__bss_end` | 0x8016F8D8 | 0x8016F8D8 |
| image | 0x138AAC | 0x138AAC |
| headroom | 1640232 | 1640232 |

`.text` moves **+0x3A0**, and the component sum is **+0x3B4**:

```
  this object's .text                        +0x440
  6 stub bodies retired                      -0x090
  6 name strings retired                     -0x04C   ast_check 12, ast_context 12, ast_off 8,
                                                     ast_on 8, ast_propagate 16, ast_taken_kernel 20
  2 stub bodies created                      +0x030
  2 name strings created                     +0x020   bsd_ast 8, kperf_kpc_thread_ast 24
                                             ------
                                             +0x3B4
```

The **0x14** between that and the measured 0x3A0 is the fill term. The stub object's string section
shrank by 0x2C, and the ~160 objects whose `.rodata.str1.1` follows it inside the `.text` *output*
section shift down with it, so their own internal padding changes too — `Δ.text = Σ(inputs) +
Σ(aligned fills)`, which 301 found and 302, 304 and 305 each paid.

The string sizes were checked against the objects rather than assumed, because the first prediction
was 0x14 high for a silly reason (`kperf_kpc_thread_ast` is 20 characters, not 19 — it is `kperf` +
`kpc` + `thread` + `ast`):

* `nm --defined-only xnu_arm_entry_realstubs.o` gives the 652 `T` symbols whose 4-aligned `(len+1)`
  sum to **0x32E0**, which is exactly that object's own `.rodata.str1.4` size.
* The 656-name set, reconstructed by adding the 6 retired names and removing the 2 new ones, sums to
  **0x330C** — the number the 306 map records for the same section.

So the stub set, the string set and the baseline all agree, and 0x14 is fill rather than a missing
input.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=mt_sched_update
 xnu_entry_stub_caller=0x800a0c14
 xnu_entry_stub_caller_a=0x800a0c14       xnu_entry_stub_caller_e=0x800a0c14
 xnu_entry_abort_entries=0x00000000
 xnu_entry_bss_start=0x80138ac0           xnu_entry_bss_end=0x8016f8d8
 xnu_entry_bss_bytes=0x00036e18           xnu_entry_copied_bytes=0x00138aac
 xnu_entry_entering_at=0x80000074         xnu_entry_failures=0x00000000
```

`tools/host_resolve_entry_addr.sh 0x800a0c14` → `thread_invoke+0x74`, `caller-4 = 0x800a0c10` is
`bl 80103810 <mt_sched_update>` in the image the device ran. In `sched_prim.c`:

```c
#if MONOTONIC
	mt_sched_update(self);
#endif /* MONOTONIC */
```

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, watchdog ARMED, no storage symbols in the
payload), log 301116 bytes, one `stub_hit=` line, no `exception:` line, `xnu_entry_abort_entries` 0.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`ro.build.version.release` = 10). The on-device `xnu_entry_bss_start`/`bss_end`/`bss_bytes`/
`copied_bytes`/`entering_at` values are the build's numbers read back off the phone.

## What it measures, and what it does not

**It measures that `thread_select` returned and that the bootstrap thread's own blocking call ran to
the point of a context switch.** `thread_invoke` is reached only from `thread_block_reason`'s
`do { thread_lock(self); thread_select(...); thread_unlock(self); } while (!thread_invoke(...))`
loop, so a stop *inside* `thread_invoke` says four things at once: `ast_off` returned, the
`thread_select` behind it returned a thread, `mt_sched_update` is the first call that function makes
(not the second, so the inlined `sched_timeshare_consider_maintenance` in front of it either
declined on a deadline that had not passed or ran its `thread_wakeup_prim` tail without stopping),
and the enqueue 305 and 306 watched go by produced a thread that `thread_select` was willing to
dispatch.

**It does not say which thread** — `thread_select` has an `idle` arm reachable when
`!processor->is_recommended`, and the run cannot tell whether the thread that is about to be
switched to is the one `sched_startup` created or the processor's idle thread. That is a data
question, and it becomes the next step's.

**It also does not say whether `ast_context`'s four call sites in `thread_invoke` were reached**,
since this step retires them: the run stopped before them. What it does say is that they are no
longer stubs.

## The scan, and the tool that replaces it

The candidate list for this step was first produced by hand, and the hand scan had a wrong
predicate: it called any `bl` target at or above the stub text's start a stub. The linker keeps
placing **real** code above that boundary — in the 306 image the stub text runs
0x80101690..0x80105410 and libgcc's `__aeabi_ldivmod` sits at 0x80105410 — so the rule was wrong,
and merely not yet wrong on the three functions it was pointed at.

`tools/stub_calls_in_function.py` is this step's answer to that: stub-ness is decided **by name,
against the stub object's own symbol table** (`nm --defined-only`, type `T`), never by address, and
each function's report carries its indirect-call count, because an indirect call is the one edge the
scan cannot follow and the count is what turns "no stub calls" into a statement with a scope. On the
306 image it reproduces the hand scan exactly, and adds the two numbers that matter here:
`thread_select` has **30** indirect calls and no direct stub call, and `thread_invoke` has **0**
indirect calls and eight sites, four of which this step retires.

The tool also made the visible part of the derivation mechanical:

* `thread_select`'s indirect calls are `SCHED(f)` = `sched_multiq_dispatch.f`, a 0xAC-byte `.rodata`
  struct at 0x801149B0 (it is *not* a stub — `nm` reports it `T` because the link script places
  `.rodata` inside the `.text` output section, which is its own small trap for an address-based
  rule). Every pointer in it resolves to a function real in this image, and exactly one of them,
  `sched_timeshare_maintenance_continue`, has a stub in its body (`compute_averages`, caller
  0x800A3178) — and `thread_select` does not call it: that entry is the *event* `thread_wakeup` is
  handed, not a call site.
* `thread_invoke`'s remaining stub sites after this step are the three `kperf_on_cpu_internal` calls
  at 0x800A10B0, 0x800A1194 and 0x800A11A8, all on paths taken only when `self->continuation !=
  NULL` or when `thread == self` — and `self` here is the bootstrap thread, blocked by
  `sched_startup`'s `thread_block(THREAD_CONTINUE_NULL)`, so `continuation` is NULL.

**And the walker named this run.** `xnu_entry_callwalk.py --root thread_block_reason` on the 306
image answers "first stub on the straight-line path: ast_off" — the name the device printed in 306.
It was never wrong about that step; the hand-reading that replaced it was. Run from `thread_invoke`
it finds no stub on the straight line (the `bne` at 0x800A0BF0 ends it) and lists
`thread_invoke+0x70 -> mt_sched_update  STUB` first among the guarded sites, which is the candidate
this run then confirmed.

## Next

**`osfmk/kern/kern_monotonic.c`** (`osfmk_kern_kern_monotonic.o`, manifest:558) — the object that
defines `mt_sched_update`. `.text` **2900 bytes**, `.bss` **24**, 23 definitions and 12 references.
It resolves **7** (`mt_sched_update`, this stop, plus `mt_fixed_counts`, `mt_fixed_task_counts`,
`mt_perfcontrol`, `mt_stackshot_task`, `mt_stackshot_thread`, `mt_terminate_update`) and adds **2**
(`mt_core_snap`, `mt_cur_cpu`), so 741 → **736** undefined and 652 → **647** function stubs; its 24
bytes of `.bss` move `__bss_end`.

**The prediction is that this step stops *outside* the object it links.** `mt_sched_update` is

```c
void
mt_sched_update(thread_t thread)
{
	bool updated = mt_update_thread(thread);
	if (!updated) {
		return;
	}
	...
}
```

and `mt_update_thread` opens with `if (!mt_core_supported) return false;` — where `mt_core_supported`
is one of the 89 storage stand-ins (`8016ef80 B`), and a stand-in is never initialized, so it reads
0 and the call returns at its first statement. Nothing in the new object runs past its first line.

The candidates are therefore what comes after: the first dispatch of the thread `sched_startup`
created, whose continuation is `sched_init_thread` (real, and call-free) running
`SCHED(maintenance_continuation)` = `sched_timeshare_maintenance_continue`, which reaches
**`compute_averages` at caller 0x800A3178** — the name 305 named as a falsifier, arriving three steps
later by a different road because `thread_wakeup`'s event and a thread's continuation turned out to be
the same pointer. The alternative is the `idle` arm of `thread_select`, which returns the processor's
idle thread instead — a data question, and the run will decide.
