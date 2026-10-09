# 949 — the fault/copy path follows the tree (2026-10-09)

The ninth rung in the tree-pin series ([[mi4-913-ios7-rebase-decision]]). 948 made 488's assembly/configuration
check tree-aware; the D13 entry build advanced through 486/487/488 and stopped at **489**
(`tools/check_fault_recovery.py`), the check around the recovery address a fault-driven copy arms.

## Part 1 — a regression the silent-wrong-tree check was hiding

Before the D13 build could even reach 489, it had to get past 474's check,
`tools/check_saved_state_offsets.py` — and that check **passed for the wrong reason**. It hard-coded
`out/xnu_assym/…` (4570's generated `assym.s`), so on D13 it compared D13's image against **4570's**
thread layout and reported success. The D13 image's `__wrap_sleh_abort` was loading
`[thread, #664]` — 4570's `TH_RECOVER` — while D13's handler reads the word at **552**. A wrong offset
here does not stop anything: it reports a plausible number that is some other word of the thread.

This is `mi4-measurement-defects` one level down: the *check itself* was the artifact. The
`STAGE90_ACT_MAP`/`MAP_PMAP`/`TH_RECOVER`/`ACT_PCBDATA` offsets are also different in D13's `struct
thread` (956/44/552/580 vs 4570's 692/40/664/848), and the fault frame is
`abort_information_context_t` (76 bytes) not `arm_saved_state` (80).

Fix (Part 1):
- `src/entry/entry_saved_state.h` gains a **per-tree table** selected by `STAGE90_ENTRY_D13` (the same
  discriminator `entry_trace.c` uses), with the six frame offsets and the four `struct thread` offsets
  for each tree.
- `tools/check_saved_state_offsets.py` becomes tree-aware (`--tree`, `configure()`, the `_d13` assym
  root, `read_defines_for_branch()`). The 4570-only sub-claims (assym/PCB mirror, timebase 481, the
  `PSR_MODE_MASK` names) are published skips on D13 — D13's `proc_reg.h` has no `PSR_MODE_MASK`.
- `external/xnu-hd2-darwin13/xnu/osfmk/arm/genassym.c` gains `DECLARE("ACT_MAP", …)` and
  `DECLARE("ACT_PCBDATA", machine.uss)` (fork commit `38e8daa`) so the check has a second source.
- `build_entry.sh` threads `--tree "$XNU_TREE"` into the invocation.

## Part 2 — D13's fault path is a different arrangement

`check_fault_recovery.py` was rooted in 4570 in ways that are not just path differences. Its eight
claims describe **4570's** copy/recovery design, and D13's is structurally different:

| | 4570 | D13 |
|---|---|---|
| recovery label | shared `copyio_error` (+`copyinstr_error`) | **per-copy local** `.Lcopyin_fault`, `.Lcopyout_fault` (+ a shared string-copy label) |
| `sleh_abort` page-in | `arm_fast_fault` **then** `vm_fault` | `vm_fault` twice (thread map, then `kernel_map`) — **no `arm_fast_fault`** |
| recovery arm | `regs->pc = (recover & ~0x1)` after a *local* `recover`, masked | `arm_ctx->pc = thread->recover`; the C reads `thread->recover` **inline** |
| user half | `goto exception_return;` → `thread_exception_return()` | `ml_set_interrupts_enabled(TRUE); return;` — no `thread_exception_return` |
| `TH_RECOVER` | 664 | 552 |

The local labels are why the D13 check cannot look up a symbol: `objdump` gives `.Lcopyin_fault` **no
heading** (it prints `copy` instructions relative to `copyin`), so the recovery target is identified by
the address the arming `add rX, pc, #imm` computes and the exit property is "a `mov rX, #14` a few
instructions past that address". D13's `copyinstr`/`copyoutstr` arm a *shared* label at the end of
`copyoutstr` that preserves `r0` (the terminator count) and is **not** an EFAULT exit, so the EFAULT
check is `copyin`/`copyout` only while "armed at all" is all four.

Three bugs surfaced while writing this, each the same class:
- `body_of` took the *next table entry* as a function's end, but `copyin`/`copyinmsg` are two global
  symbols at **one address** (an assembler alias) — so the body was empty. Fix: the end is the next
  global at a **strictly greater** address.
- `STORE_RECOVER` and `ADR_PC` accepted only `r\d+`; D13's copies arm through `ip` and the string
  copies through `r5`. Widened to all general registers.
- A D13 selftest mutation that pointed an arm a few instructions up still found the *real* exit's
  `mov r0, #14` (the check passed on a broken image). Fixed by aiming the mutation 0x100 off, inside
  the body, and tightening the EFAULT-exit window.

`compare()` selects `D13_CLAIMS` on the tree; `selftest()` runs `D13_MUTATIONS` (11). The generic
4570 claims are byte-for-byte unchanged, and `body_of`'s fix cannot affect 4570 (4570's copies are
macros, not symbol aliases).

## State after this rung

489 passes on D13 (**11 mutations refused**), as do 490-498, 510-512, 513-522 (already skipped on D13
by 937). The build advances to a **new** wall, **950**: the resident arm's watchdog pet —
`entry_wdt_pet is not a defined (t) symbol in the linked image`. `make check` exits 0; 4570's
`check_saved_state_offsets.py` and its selftests are unchanged.

## Provenance

`src/entry/entry_saved_state.h`, `tools/check_saved_state_offsets.py`, `tools/check_fault_recovery.py`,
`src/entry/build_entry.sh`, and the fork's `osfmk/arm/genassym.c`. Host-side, reversible, **no press**.

- D13: `check_fault_recovery.py --tree external/xnu-hd2-darwin13/xnu` passes; `--selftest` → 11 refused.
  `check_saved_state_offsets.py --tree …` passes.
- D13 entry build: `/tmp/d13_entry_build40.log` (490 at line 1034; stops at 950's wall at line 1047).