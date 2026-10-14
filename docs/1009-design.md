# 1009 — the faulting PC and the FSR: WHICH instruction the handler re-runs (design)

## 1. The frontier statement (what 1008 leaves open)

1008 (`records/1008-press-read.txt`, arm `armed-d13-0343ef45`) named the storm's fault **address and
type**: the powers-of-two ladder's `far` walks 12 distinct values for ordinals 1…0x200, then **collapses
onto the CONSTANT `far=0x0007b000`** (`type=4 = STAGE90_T_DATA_ABT`, a **data** abort) from ordinal
**0x400 through 0x200000** (~2.09 M aborts). The ~18.7 s handler call (`abt_maxcall=0x1570c1a7`) rides
it. So the storm is a **single-address RETRY LOOP**, not a demand walk.

1008 says **where**, not **what**. Two readings are missing, and the port's handler return path turns
on both (`external/xnu-hd2-darwin13/xnu/osfmk/arm/trap.c:510 sleh_abort`):

1. **Which instruction faults.** `0x7b000` is the fault *address*; the instruction that referenced it
   is `regs->pc` at the abort. Naming it names the loop.
2. **The DFSR.** `far` + `type` say the same access re-runs; the DFSR (`c5,c0,0`) carries the per-fault
   **WnR** (was it a read or a write) and the **fault-status code** (`0x7`/`0xf`/…) — whether the
   retries are the *same* fault or a cycling status. 1008 never published it.

And the 1008 reader could not decide REPEAT-vs-SPREAD **by value**: it counted the *distinct* `far`
values in the whole ladder (12) and reported `MIXED`, blind to the tail being 61× the walked prefix.
1009 publishes the **change-count** so the reader decides on the *tail*, not the set.

## 2. What the wrapper ALREADY holds (nothing new is read from the machine)

`__wrap_sleh_abort` (`src/entry/entry_trace.c`) already has, by the first line of its body:

- `regs` — the saved-state frame the abort vector built. Its `pc`/`cpsr` slots are exactly the ones the
  `redirected` check reads (`STAGE90_SS_PC` = 60, `STAGE90_SS_CPSR` = 64, `entry_saved_state.h:80-83`).
- `fsr` — read at `:5688` from `c5,c0,0` (or `c5,c0,1` for the prefetch class), the **DFSR**.
- `far_` — read at `:5695`, the fault address (1008 already forwards this).
- `type` — the abort class.

1009 forwards `regs->pc`, `fsr`, `regs->cpsr`, `far_` to a new recorder. **No new cp15 read, no new
link edge** — the same by-value rule 1008 follows (`docs/1008-design.md §4.3`), because `entry_stubs.c`
reads neither cp15 nor the frame.

## 3. The change (one new recorder, one new switch)

### 3.1 Arm switch: a NEW `STAGE90_XNU_ABT_PC`

Not reusing `STAGE90_XNU_ABT_FAR` keeps the OFF image byte-identical and 1008's arm reproducible
(the 1007/1008 precedent, §4.1 each). The guard is the **conjunction** `ABT_FAR && ABT_PC` — 1009 sits
after the 1008 recorder and reads nothing 1008 did not, so it is inert unless 1008 is also on.

### 3.2 The recorder (`src/entry/entry_stubs.c`, after 1008's `#endif`)

```c
void entry_note_abt_pc(uint32_t pc, uint32_t fsr, uint32_t cpsr, uint32_t far);
```

Two outputs, both bounded:

- **The ladder rows** — 1008's trigger set VERBATIM (`n <= 4 || n a power of two`), so the tail
  (0x400…0x200000) is already sampled. Publishes `xnu_live_abt_pcn` (the ordinal, the twin of 1008's
  `_farn`), `_pc` (the faulting instruction), `_fsr`, `_abtcpsr` (the mode), `_pcfar` (pairs with
  1008's `_far` at the SAME ordinal, independent of record order across two recorders).
- **The change-counts** — `xnu_live_abt_nfar` / `_nfsr`, written WHERE THEY CHANGE (the `g_sleh_armed`
  rule): the number of **changes** in `far` / `fsr` so far. Bounded by the distinct-value count
  (≤ ~12 for `far`, a handful for `fsr`), **not** by the abort count — the whole budget point. Held at
  1/1 across the tail IS the REPEAT statement 1008's set reader missed.

### 3.3 The call site (`src/entry/entry_trace.c`, `__wrap_sleh_abort`)

After 1008's call, guarded `#if defined(ABT_FAR) && ABT_FAR && defined(ABT_PC) && ABT_PC`:

```c
entry_note_abt_pc(((const uint32_t *)(uintptr_t)regs)[STAGE90_SS_PC / 4], fsr,
                  ((const uint32_t *)(uintptr_t)regs)[STAGE90_SS_CPSR / 4], far_);
```

### 3.4 The new key literals (7)

`xnu_live_abt_pcn`, `xnu_live_abt_pc`, `xnu_live_abt_fsr`, `xnu_live_abt_abtcpsr`,
`xnu_live_abt_pcfar`, `xnu_live_abt_nfar`, `xnu_live_abt_nfsr`. The image's total `xnu_live_abt_`
literal count moves **11 → 18** (1008's §4.4 clause).

### 3.5 Bookkeeping sites for the NEW switch

`scripts/build_entry.sh` (the `-D` to BOTH `STUB_DEFINES` and `STUB_DEFINES_TRACE`; refuse
`ABT_PC=1` with `ABT_FAR=0`), the arm record, `scripts/preflight_boot_check.sh` (artifact-grounded
config key), `scripts/park_1009.sh`, `scripts/press_1009.sh`, `tools/check_1009_capture.py`.

## 4. Build refusals (a comment is not a check) — `scripts/build_1009.sh`

(a) the guard is one-per-region in `entry_stubs.c` (recorder) and `entry_trace.c` (extern + call);
(b) the recorder is a DEFINED `T`/`t` symbol, no `U entry_counter`;
(c) the literal count is **18** (7 new), by value via `strings`;
(d) the 1008 delta is KEPT and unchanged (11-literal clause, 1008 guards one-per-region);
(e) the FROZEN 1008 elf (`armed-d13-0343ef45`) is the NEGATIVE CONTROL: it carries the 11 literals and
    **no** `entry_note_abt_pc` symbol, proving the 1009 edit is not already present;
(f) `STAGE90_XNU_ABT_PC=0` twin (with `ABT_FAR=1`) leaves exactly 1008's 11 literals — the OFF image
    is byte-identical → 1008's arm reproducible.

## 5. What the capture must show to be DECISIVE (the classifier)

- `xnu_live_abt_pcn`/`_pc`/`_fsr`/`_abtcpsr`/`_pcfar` at ordinals 1…0x200000.
- `xnu_live_abt_nfar == 1` and `xnu_live_abt_nfsr == 1` across the tail ⇒ **REPEAT** by value (one
  faulting address, one status). `nfsr > 1` ⇒ a **cycling status** (the retries are not identical);
  `nfar > 1` ⇒ the tail spreads (1008's reading was wrong).
- The `_pc` at the tail ordinals is the **faulting instruction** — its symbol names the loop.
- `_abtcpsr` is the mode: kernel (`0x…13` SVC / `0x…93` / `0x…d3` etc.) vs user. `0x7b000` is below the
  `0x80000000` split, so a **user/identity** reference is possible; the cpsr settles it.

## 6. Falsifiers (what reading would REFUTE the storm interpretation)

- `nfar > 1` across the tail ⇒ the "constant far" was a sampling artifact; the storm spreads.
- `nfsr > 1` ⇒ the retries differ in status; not one fault re-run.
- `_pc` at the tail is **not** constant ⇒ different instructions reference `0x7b000` (a demand pattern).
- `_abtcpsr` shows a mode change across the tail ⇒ the loop crosses the user/kernel boundary.

## 7. Could not verify (honest limits)

- The recorder re-reads `regs->pc` **after** `entry_note_sleh`/`_sleh_back` but **before**
  `__real_sleh_abort` — the frame is the *pre-handler* one, which is what "the faulting instruction"
  means. If the vector re-enters, the value is the re-entry's; the `type`/`far` pair (1008) would show
  it.
- `fsr` for the data class is the DFAR/DFSR pair; whether the port's `type` maps 1:1 to the
  fault-status code is read from the capture, not asserted here.

## 8. Citations

- `records/1008-press-read.txt` — the REPEAT-at-`0x7b000` verdict + the `MIXED` weakness.
- `docs/1008-design.md §3` (the budget rule), §4.1 (separate switch), §4.3 (by-value), §4.4 (keys).
- `src/entry/entry_trace.c:5675-5742` — the wrapper, the pair reads, the 1008 call.
- `src/entry/entry_stubs.c:1970-2000` — the 1008 recorder (`entry_note_abt_far`).
- `src/entry/entry_saved_state.h:80-83` — `STAGE90_SS_PC`/`_LR`/`_SP`/`_CPSR`.
- `external/xnu-hd2-darwin13/xnu/osfmk/arm/trap.c:510/705` — `sleh_abort`, the recovery arm.