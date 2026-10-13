# 1005 — branch decision draft (per 1004's abort trace)

Status: DRAFT, ready the moment rung 1004's log lands. Read-only on source; this file is the only
artifact. Every file:line below was read by value this pass.

## The fork 1004 discriminates (from `records/1003-press-read.txt`)

1003 measured pid 1 stuck in the kernel's data-abort RETURN path (`lr=0x8046b92c` = `sleh_abort+0x468`,
one frame `sp=0xcfdbbf00` held for n=9..15) with `xnu_live_sleh_seen`=40. Two readings fit and 1004
(STAGE90_XNU_ABORT_TRACE, caps 8/16/64 → 512 at `src/entry/entry_stubs.c:1709-1717`, plus the new
`xnu_live_sleh_upc` at `:1836`) publishes the tail of EVERY abort (`far`, `upc`, `type`):

- **(a) LOOP** — the same shared-cache page re-faults: `vm_fault` returns KERN_SUCCESS, the user
  re-faults the SAME address. Candidate mechanism: the CS-validation path.
- **(b) WALK** — dyld pages the 301 MiB shared cache forward, one page per fault. Slow, not stuck.

The classifier that picks the branch at read time is `tools/check_1004_capture.py` `s_abort()`
(`:566`), detailed in the last section.

---

## Branch (a) — LOOP: the CS-validation loop

### Grounding (all read by value)
- `vm_fault_enter` predicate, `osfmk/vm/vm_fault.c:2438`: `(!m->cs_validated && (prot & VM_PROT_EXECUTE))`.
  Reaching it needs `code_signed` on the object — `VM_FAULT_NEED_CS_VALIDATION`, `vm_fault.c:2291`
  (`page->object->code_signed` `:2294`; field at `vm_object.h:319`).
- On that path `cs_invalid_page((addr64_t)vaddr)` is called at `vm_fault.c:2469`; its return is
  `reject_page`. `reject_page==0` (the tolerated case) sets `m->cs_tainted = TRUE` and
  `cs_enter_tainted_accepted++` at `vm_fault.c:2476-2484`. **`vm_fault_enter` is a `static`-scope
  function in `vm_fault.o`; both call sites (`:3260/:3272/:3890`) are same-object, so `--wrap=vm_fault_enter`
  would be link-invisible** ([[mi4-hooks-live-at-calls]], documented at `entry_trace.c:200-203`). So the
  hook cannot be on `vm_fault_enter`; it must be on its non-static callee.
- `cs_invalid_page` is `bsd/kern/kern_proc.c:2742`, non-static. Its three branches: `CS_KILL` →
  `psignal(p, SIGKILL)` `:2763-2773`; `CS_HARD` → `retval=1` `:2784`; else the tolerated branch
  `:2785-2798`, where `p->p_csflags & CS_VALID` is tested at `:2786`, cleared at `:2787`, and
  `retval=0` at `:2798`. (1002's single `cs_invalid_page` print was this branch — the frontier read
  in `docs/1003-dyld-stall-design-draft.md` calls it a red herring; the question 1005 sharpens is
  whether it fires PER-ITERATION in the tail, not once.)

### The rung
- **Switch:** `STAGE90_XNU_CS_TAINT_TRACE` (0/1, threaded into the kernel cc line like
  `STAGE90_XNU_ABORT_TRACE` is via `scripts/build_1004.sh:29-30/244`).
- **Single code change:** in `bsd/kern/kern_proc.c`'s `cs_invalid_page()`, inside the tolerated
  `else` branch at `:2785`, call a new `entry_note_cs_invalid(vaddr, p->p_csflags)` **before** the
  `&= ~CS_VALID` at `:2787` (so the read shows whether the bit was set). The recorder lives in
  `src/entry/entry_stubs.c` beside `entry_note_sleh` (`:1744`) and publishes, via `entry_live_write`:
- **Keys it publishes:** `xnu_live_cs_invalid_n` (bounded ordinal), `xnu_live_cs_invalid_vaddr`
  (the faulting VA), `xnu_live_cs_valid_was_set` (the CS_VALID bit pre-clear), `xnu_live_cs_invalid_flags`.
  Paired in the log with 1004's `xnu_live_sleh_upc` (`entry_stubs.c:1836`) on the same abort.
- **What a press proves:** if `xnu_live_cs_invalid_n` reaches 512 with a REPEATING `vaddr` (the same
  cache page), the tail is a CS-validation re-fault loop — each `vm_fault` calls `cs_invalid_page`,
  returns 0, and the mapper re-faults the same page. If `vaddr` strictly advances, even the CS-touched
  faults are a forward walk.
- **Falsifier:** `xnu_live_cs_invalid_n == 0` across the whole late window ⇒ the stall's aborts are
  ORDINARY demand faults (never the CS path) ⇒ branch (a) is dead and (b) stands. (Build refusal to
  add: the delta clause asserts the `entry_note_cs_invalid` call site + keys exist by value, else
  `build_1005.sh` exits non-zero — a comment is not a check.)

---

## Branch (b) — WALK: is `far` advancing, and how fast

### Grounding (all read by value)
- The hook seam already exists and works: `__wrap_sleh_abort` at `src/entry/entry_trace.c:5635` reads
  the fault pair (`mrc p15,0,..,c6,c0,0` for DFAR `:5652`; `c6,c0,2` for IFAR `:5649`), calls
  `__real_sleh_abort(regs, type)` `:5661`, then `entry_note_sleh_back` `:5677`. `sleh_abort` **is**
  `--wrap`'d (`build_entry.sh:197`).
- Time source that already exists in the payload: `stage90_cntvct_read()` (`src/entry/entry_timebase.h:144`,
  the 19.2 MHz CNTVCT reader; used e.g. `entry_usb_dev.c:180`). It is NOT currently called on this path.
- `far` here is the **cp15 DFAR/IFAR read at the wrapper** (`entry_trace.c:5652`), which is a DIFFERENT
  source from `_far_frame` = `frame[STAGE90_SS_VADDR/4]` (`entry_stubs.c:1787`, offset in
  `entry_saved_state.h:85`). `_frame_ok` (`:1796`) only tests that the two AGREE; do not treat them as one.

### The rung
- **Switch:** `STAGE90_XNU_FAULT_RATE` (0/1).
- **Single code change:** in `src/entry/entry_trace.c`'s `__wrap_sleh_abort()` (`:5635`), bracket the
  `__real_sleh_abort` call (`:5661`) with `stage90_cntvct_read()` and publish the delta since the
  previous abort; the publish rides `entry_note_sleh` at `:5659`.
- **Keys it publishes:** `xnu_live_sleh_dt` (CNTVCT ticks since the prior abort; @19.2 MHz, 1 tick ≈ 52 ns),
  `xnu_live_sleh_dt_max` (max seen), and (widening 1004) `xnu_live_sleh_far`/`_upc` on every entry via
  the 512 caps at `entry_stubs.c:1710`.
- **What a press proves:** if `far` strictly advances with a BOUNDED, roughly-constant `dt`, dyld is
  walking the cache forward at a page-in cadence — benign slowness. If `dt` grows without bound (fast
  early, then a single aborts-spacing that swallows the run), the walk STOPS — evidence for (a)/a stall.
- **Falsifier:** `_dt == 0` on every record (the counter does not advance — a MEASUREMENT defect, not
  a reading; adjudicate `entry_timebase.h`) OR `far` non-monotone ⇒ the "walk" is not a walk. (Build
  refusal: assert `stage90_cntvct_read` appears in the wrapper's disassembly by value.)

---

## How 1004's log selects the branch

**Exact rule** (`tools/check_1004_capture.py`, `s_abort()`):
1. Build the ordered `far` list from the 512-cap records (each record opens at `xnu_live_sleh_seq`).
2. For record `i>0` (`:621-632`): if `far[i] ∈ {far[i-1..i-REPEAT_WINDOW]}` → **REPEAT** (`loop++`);
   elif `far[i] > far[i-1]` → **ADVANCE** (`walk++`); else `flat++`. `REPEAT_WINDOW = 4` (`:111`).
3. Verdict (`:648-653`): **WALK** if `walk > loop`; else **LOOP** if `loop > 0`; else **NO-WALK**
   (fewer than two comparable `far` values — the trace is too short to classify).
4. The frontier = the LAST record's `far`/`upc` (`:656-663`).

**Where it lives:** `tools/check_1004_capture.py`, the `s_abort()` function (`:566`), §[A]; the walk
loop is `:612-653`; the constants `REPEAT_WINDOW=4` (`:111`), `SLEH_OLD_MAX=8` (`:109`),
`ABT_PREFETCH=3`/`ABT_DATA=4` (`:113-114`).

**Caveat (UNVERIFIED, flag for 1005).** The rule compares raw `far` with strict `>`. Real dyld page-ins
have far values that differ WITHIN one 4-KiB page (1003's head: `0x2be89fe0`, `0x2be8c78f`, `0x2be8a084`),
so a same-page re-fault would read as ADVANCE, not REPEAT. A page-granular comparison (`far & ~0xfff`)
is likely needed to make "repeat" mean "the same page". This is NOT verified against a real 1004 log —
treat it as the first thing to reconcile when the log lands.

## Unresolved / UNVERIFIED
- Whether `far` even fires per-iteration on the tail: 1003 saw the campfire at `pc=0x8001b1fd` (kernel,
  post-return), and the wrapper's cp15 reads (`entry_trace.c:5649-5652`) read the register state AT the
  abort vector, so they should reflect the user fault — UNVERIFIED until a 1004 log shows `user=1` on
  the tail records as well as the head.
- `xnu_live_sleh_seen` was 40 in 1003; whether 1004's 512 cap captures the WHOLE tail or only widens the
  window is a property of the run, not the build.