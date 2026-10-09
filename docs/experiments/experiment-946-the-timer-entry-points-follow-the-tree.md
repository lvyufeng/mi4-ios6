# 946 — the timer entry points follow the tree (2026-10-09)

The sixth rung in the tree-pin series ([[mi4-913-ios7-rebase-decision]]). 945 made 482's GIC routing
check tree-aware; the D13 build advanced to **484's timer census** (`tools/check_timer_sources.py`) and
stopped: *the image defines no `timer_call_enter_with_leeway`, `timer_call_quantum_timer_enter`*.

## The wall

    FAIL: the timer census this step builds is not the census this image has:
      the image defines no timer_call_enter_with_leeway, timer_call_quantum_timer_enter, so the wrapper
      for it calls the generator's stand-in

484/503 wraps **three** timer entry points and gives each a source number: `timer_call_enter` (1, the
etimer queue), `timer_call_enter_with_leeway` (2, the deadline a parked thread is woken by) and
`timer_call_quantum_timer_enter` (3, the quantum metronome). D13 declares **neither** of the last two.

## Finding — the family is a different shape in each tree, and one member *swaps sides*

The names are not a subset issue; the entry points differ by tree:

| | 4570 | Darwin 13 |
|---|---|---|
| etimer queue | `timer_call_enter` | `timer_call_enter` |
| wait timer (`thread->wait_timer`) | `timer_call_enter_with_leeway` (`waitq_assert_wait64_leeway`) | `timer_call_enter` (`wait_queue.c:1217`) |
| quantum metronome | `timer_call_quantum_timer_enter` | **`timer_call_enter1`** (`priority.c:189`, `sched_prim.c:1966/2523`) |

`grep -rl` is **0** for both 4570 names in the D13 tree — D13's `timer_call.h` declares only
`timer_call_enter` and `timer_call_enter1`. The critical inversion is **`timer_call_enter1`**: on 4570
it is the *unwrapped* member (484's first draft wrote its wrapper, the linker found no branch to it —
4570's only callers are `sfi.c` and `dtrace_glue.c`, neither compiled here — and the reachability check
refused the image, which is written up in `entry_trace.c`), while on D13 it is the **quantum metronome's
only entry point** and must be wrapped. On D13, source 2 is never emitted: one entry point serves both
the etimer queue and the wait timer, so the *number* cannot separate them there.

So the census's meaning shifts with the tree — source 1 and source 3 keep their mechanism (etimer,
metronome), and source 2's deadline rule is inert (nothing arms through a leeway entry). The build's
`TRACE_LDFLAGS` literal now carries all three timer names (so the check can read the tree's set out of
the source), and **each tree removes the name it does not have**: D13 via `D13_ONLY_WRAPS` for the leeway
and quantum pair, 4570 via the `else` branch for `enter1`. `entry_trace.c` guards the wrapper bodies with
`#if !STAGE90_ENTRY_D13` / the `#else` for `__wrap_timer_call_enter1`.

## A latent break in an earlier rung, surfaced here

484's selftest mutation `wrap_moved_to_pass_one` anchored on the literal
`PASS1_LDFLAGS=(--wrap=PE_init_platform --wrap=fiq_context_init)`. Rung 934 made `fiq_context_init`
tree-gated through `${FIQ_CTX_WRAP[@]}` and 935 threaded the expansion into that line — **and neither
updated this anchor**. No run noticed: the D13 build never reached 484 (this rung is the first time it
does), and the 4570 `--selftest` was not rerun between 935 and now. It surfaced the first time 484 ran
on D13. The anchor now carries the expansion; with it corrected, the **4570 selftest refuses all 48
mutations again** (it had been silently reporting a `_bump` error rather than a result).

## The check is now tree-aware

Same shape as 942/943/945: `--tree` (default 4570; D13 detected by D13's `osfmk/sys/types.h`), and
`configure()` sets the module's `TIMER_CALL_H`/`SCHED_H`, `OBJECT_POOL` (D13's pool is
`out/xnu_kernel_obj_d13`) and the three per-tree tables `SOURCES`/`WRAPPED`/`UNWRAPPED`. Claim 7
(`claim_unwrapped_member`) inverts on D13: instead of reading the pool for `enter1`'s absence, it asserts
the two 4570 names are still absent from D13's header and `enter1` is still declared. The selftest
publishes its D13 skips (`D13_SKIPPED_MUTATIONS`, 14 names): the mutations anchored on a 4570-only name,
the three source-number mutations whose first `(3u,` literal is inside the 4570-only block D13 compiles
out, and the pool/omission claims. On D13: **34 refused, 14 skipped**.

## State after this rung

484/503 pass on D13. The build advances to **485's boot-completion claim**
(`tools/check_boot_completion.py`): the image defines no `kdebug_free_early_buf` and
`kernel_bootstrap_thread`'s text never transfers to `__wrap_kdebug_free_early_buf` — a 4570 boot-tail
name, the same class again. The next rung, 947.

## Provenance

`tools/check_timer_sources.py`, `src/entry/build_entry.sh`, `src/entry/entry_trace.c`. Host-side,
reversible, **no press**.

- D13 entry build: `/tmp/d13_entry_build35.log` (484 passes; stops at 485's boot-completion check).
- 4570-neutral: `check_timer_sources.py --selftest` (no `--tree` ⇒ 4570 default) against
  `out/stage90/frozen/armed-storage-054f8269/xnu_arm_entry.elf` refuses all 48 mutations.
- D13: `--selftest` against the rebuilt D13 elf refuses 34, publishes 14 skips.
- D13 facts (read here): `grep -rl` = 0 for `timer_call_enter_with_leeway` /
  `timer_call_quantum_timer_enter` in the D13 tree; D13 `timer_call.h` declares only `timer_call_enter`
  and `timer_call_enter1`; D13 `wait_queue.c:1217` arms the wait timer through `timer_call_enter`;
  `priority.c:189` / `sched_prim.c:1966,2523` arm the quantum through `timer_call_enter1`.
- `make check` exits 0.