# 1007 — where the ~24.4 s is spent on the non-idle abort path: an inter/ intra-abort span witness (design)

Status: DESIGN. Read-only: this file changes no source. Every claim below was read at the file:line
shown, and every address was read **by value** from the linked image. Anything I could not verify is
in **§7 (Could not verify)**.

Rung 1006 (`records/1006-press-read.txt`, arm `armed-d13-31bdebf3`, commit `4a5dc26`) settled the
frontier's *path*: the ~24.4 s `xnu_live_tick_sum_maxd` freeze (`0x1be87a47` = 468,220,487 counts =
24.386 s at 19.2 MHz) does **not** overlap the idle path — `__wrap_machine_idle` is **never entered**
on the recent D13 tail (its `entry_freeze_sample` fired 0×, and rung 512's `xnu_live_idle_*` is
absent in every capture since the boot got deep enough to exec real `launchd`). The frame that 1005's
stall sampler kept repeating (`pc=0x8001b48d  lr=0x8046b95c  cpsr=0x60000113`) was resolved **by
value** (`docs/1006-terminal-frame-analysis.md`): `lr` is `sleh_abort+0x468`, an *unconditional
forward branch* (`ea000058 b 8046bac4`), I-bit 0, A-bit 1 — **not** a fault-return self-loop. So the
freeze rides the **non-idle** path, and 1004 saw that path as a **WALK** (~40 aborts with advancing
`far`).

1007 is a **span witness on the abort handler**: it brackets each `sleh_abort` call with the
free-running generic timer and publishes, per abort, (a) the gap to the previous abort and (b) the
handler's own duration — so the capture says whether the 24.4 s sits **between** two aborts, **inside
one** abort, or **after the last** abort. It is a pure MEASUREMENT: it changes no kernel behaviour.

**A by-value defect in the inherited image is found and must be closed by this rung** (§2.3): the
1006 sampler calls `entry_counter()`, which in the linked image resolves to a **terminal generated
stand-in**, not the timer. It is dormant only because the idle path it rides is never entered; on the
abort path (which 1007 rides and which *is* entered ~40×) it would end the run. 1007 routes the clock
in as an argument instead, and the build refuses the stand-in.

---

## 1. The frontier statement

> After dyld first touches a shared-cache page, **the whole interrupt path freezes for one ~24.4 s
> interval** (tick = irq − 3 for the whole run; the pid-1 SVC path froze with it), then bursts and
> the machine stays alive. 1006 proved the CPU is **not idle** through it: the freeze is spent in
> **non-idle kernel work**. The terminal frame is the abort handler (`sleh_abort+0x468`) handing back
> to the interrupted context, and 1004 saw ~40 aborts walking forward. **No instrument says where the
> 24.4 s is spent** — between aborts, inside one, or after the last.

1007's question is exactly that partition.

---

## 2. What 1005/1006 could NOT see (the measurement gap 1007 closes)

### 2.1 Every published clock is gated by the thing it measures or by a path that is never entered

| publisher | driver | resolves the freeze LOCATION? |
|---|---|---|
| `xnu_live_tick_*` / `xnu_live_stall_*` | the timer IRQ | **NO** — it *is* what stops |
| `xnu_live_cadence_*` | the SVC path | **NO** — it froze with the tick |
| `xnu_live_freeze_*` (1006) | `__wrap_machine_idle` | **NO** — the wrapper is **never entered** (1006 read, by absence) |

The only clock that *advances across* the freeze is the free-running generic timer — but across the
freeze no *caller* runs except the non-idle path itself. So the witness must be **on that path**.

### 2.2 The non-idle path is the abort handler, and its two hooks already exist

- `void __wrap_sleh_abort(void *regs, int type)` — `src/entry/entry_trace.c:5661`, reached by
  `--wrap=sleh_abort` (`src/entry/build_entry.sh:197`), which rewrites XNU's own call in
  `osfmk/arm/locore.s` (`sleh_abort`, `osfmk/arm/trap.c:510`, is the once-per-abort second-level
  handler). The wrapper is a **returning** wrapper: it records the entry (`entry_note_sleh`, `:5685`),
  calls `__real_sleh_abort(regs, type)` (`:5687`), and records the return (`entry_note_sleh_back`,
  `:5703`).
- The real `entry_counter()` — `mrrc p15, 0, lo, hi, c14`, the 19.2 MHz generic timer — is a
  `static inline` in **this same file**: `src/entry/entry_trace.c:952`. So `__wrap_sleh_abort` can
  read the real timer with no new extern and no new link edge.

That is the whole reason the abort path is the right hook: **it is the one non-idle path that runs
during the tail, and the clock is already in its file.**

### 2.3 THE DEFECT (found by value): `entry_counter` in `entry_stubs.c` is a terminal stand-in

`entry_stubs.c` declares `extern uint32_t entry_counter(void)` (`:5344`) and 1006's sampler calls it
(`uint32_t now = entry_counter();`, `:5358`). `entry_stub.o` therefore carries an **undefined**
reference: `arm-none-eabi-nm out/stage90/xnu_arm_entry_stubs.o` → `U entry_counter`. In pass 1 that
name is undefined (the file that defines it, `entry_trace.c`, is linked only *after* pass 1 —
`build_entry.sh:29492`), so the stub generator emitted a **terminal stand-in**:

```
out/stage90/xnu_arm_entry_realstubs.c:38
void entry_counter(void) { entry_stub_hit("entry_counter", (uint32_t)(uintptr_t)__builtin_return_address(0)); }

the linked image (by value):
8049cce4 T entry_counter
8049cce4:  movw r0, #16460 ; movt r0, #32854     ; r0 = 0x8056404c = the "entry_counter" name string
8049cce8:  push {lr} ; mov r1, lr ; pop {lr}
8049ccf8:  b 8000a8f4 <entry_stub_hit>
```

and `entry_stub_hit` (`entry_stubs.c:8558`) records `xnu_live_stub_hit_seq/_name_ptr/_caller` and then
calls `entry_epilogue` (`:8636`, declared `__attribute__((noreturn, noinline))`, `:3768`) — it tears
the caches and the MMU down and **ends the run**.

**So any code that calls `entry_counter()` from `entry_stubs.c` terminates the run the instant it
executes.** 1006's sampler does exactly that — it survived only because `__wrap_machine_idle` is never
entered, so `entry_counter` was never called (the 1006 capture carries `xnu_live_stub_hit_seq` ×0;
`xnu_live_freeze_` ×0; `xnu_live_frzbase_` ×0). 1007 puts a witness on a path that runs ~40 times, so
it must not make this call, and the build must refuse the stand-in (a reader that cannot fail
certifies nothing — `mi4-a-claim-in-a-comment-is-not-a-check`).

The fix is the ring's established shape, not a new idea: read the clock **in the file that has it**
(`entry_trace.c`, where the `static inline` lives and which already passes clock values into recorders
— `entry_note_repair(caller, rb, entry_counter())`, `:1621`) and pass the **value** into the recorder
in `entry_stubs.c` (whose `entry_note_repair` signature is exactly `uint32_t caller, uint32_t before,
uint32_t after`, `:5858`).

---

## 3. Why this hook (and why the others are wrong or deferred)

**Chosen: bracket `sleh_abort` in `__wrap_sleh_abort` with the real timer.** Because (i) it is the one
non-idle path entered during the tail; (ii) its entry and exit hooks **already exist and already
carry the abort count** — this adds a clock read at each, nothing structural; (iii) the clock is in the
same file; (iv) 1004's WALK gives ~40 samples to ladder; (v) the terminal frame is on this exact path.

**Rejected — a `vm_fault` entry/exit bracket.** `vm_fault` (`osfmk/vm/vm_fault.c:2741`) is real and
central, but it is **not** in the `--wrap` set (`build_entry.sh:169-223`), so wrapping it needs either
a new `--wrap` (a link-layout change across the whole image) or an in-tree edit to `trap.c` — both
cost more than they answer, and `sleh_abort` **already brackets** the whole `arm_fast_fault` +
`vm_fault` sequence (`trap.c:613/658/668/742/754/830`), so the abort span is a superset of the
`vm_fault` span. A one-abort 24.4 s *inside* `vm_fault` shows as a 24.4 s `call` here too; splitting
it further is a second question that this rung need not spend itself on. **Deferred.**

**Rejected — a periodic probe that does not ride the tick.** There is no such caller. If the interrupt
path is frozen and the CPU is in the fault path, the *only* code that runs is the fault path itself,
so any "periodic" probe degenates to a probe **on the abort path** — i.e. the chosen hook, executed
once per abort rather than on a timer. (The `mrrc` clock "keeps running" but nobody reads it.)

---

## 4. The change (one new recorder, one new switch)

**Switch:** `STAGE90_XNU_ABT_SPAN` (0/1), threaded exactly like `STAGE90_XNU_CADENCE` (both files
consume it) and `STAGE90_XNU_FREEZE_WITNESS` (record echo shape). Verified edit points:

| layer | file:line | change |
|---|---|---|
| arm-key list | `src/entry/build_entry.sh:1696` | add `STAGE90_XNU_ABT_SPAN` to `ENTRY_ARM_KEYS` (after `STAGE90_XNU_FREEZE_WITNESS`) |
| trace define | `src/entry/build_entry.sh:310` | append `-DSTAGE90_XNU_ABT_SPAN="${STAGE90_XNU_ABT_SPAN:-0}"` to `STUB_DEFINES_TRACE` |
| stubs define | `src/entry/build_entry.sh:1178` | read `ABTSPAN=${STAGE90_XNU_ABT_SPAN:-0}`, validate 0\|1, `STUB_DEFINES+=(-DSTAGE90_XNU_ABT_SPAN="$ABTSPAN")` |
| case arm | `src/entry/build_entry.sh:1936` | `STAGE90_XNU_ABT_SPAN) _v=$ABTSPAN ;;` |
| record echo | `src/entry/build_entry.sh:39641` | `echo "STAGE90_XNU_ABT_SPAN=$ABTSPAN"` |
| press gate | `scripts/preflight_boot_check.sh:750` | add `STAGE90_XNU_ABT_SPAN` to `ENTRY_CFG_KEYS` |
| build driver | `scripts/build_1007.sh` | pin `EXPORTS+=("STAGE90_XNU_ABT_SPAN=1")` and the DELTA section (§6) |

**No `pe_msm8974.c` change, no `tools/build_xnu_arm_kernel.sh` edit, no new `--wrap`.** The switch
reaches exactly the two entry-toolchain objects (`entry_stubs.c` via `STUB_DEFINES`, `entry_trace.c`
via `STUB_DEFINES_TRACE`).

### 4.1 The recorder (in `src/entry/entry_stubs.c`, beside `entry_note_sleh_back`, after `:1890`)

It takes the two clock **values** as arguments — never calls `entry_counter()`:

```c
#if defined(STAGE90_XNU_ABT_SPAN) && STAGE90_XNU_ABT_SPAN
/*
 * 1007: the abort-span witness. Called from `__wrap_sleh_abort` (entry_trace.c) with the two clocks
 * it read around `__real_sleh_abort` using the file's own real `entry_counter()` inline. `t0` is the
 * clock at entry to the abort handler; `call` is `t1 - t0`, the handler's own duration. The recorder
 * derives `gap` from the previous abort's `t0`, so consecutive deltas answer "between two aborts" and
 * `call` answers "inside one abort". All records go through `entry_write_kv` (DIRECT, cap-exempt by
 * construction - the same channel `entry_tick_summary` uses, pe_msm8974.c:611), so the maxima at the
 * frontier survive the record cap.
 *
 * Plain statics, no allocation: this rides a handler that can re-enter itself (269's storm is a fault
 * inside a reporting path), so the whole body is bounded stores - the rule `entry_note_sleh` already
 * obeys (`entry_stubs.c:1596-1602`).
 */
static uint32_t g_abt_n;
static uint32_t g_abt_last_t;
static uint32_t g_abt_have;
static uint32_t g_abt_maxcall;
static uint32_t g_abt_maxgap;

void
entry_note_abt_span(uint32_t t0, uint32_t call)
{
    uint32_t gap, n;

    gap = (g_abt_have != 0u) ? (t0 - g_abt_last_t) : 0u;
    g_abt_last_t = t0;
    g_abt_have = 1u;
    n = ++g_abt_n;

    /* The latched maxima, written WHERE THEY CHANGE (the `g_sleh_armed` rule, entry_stubs.c:1774):
     * the ladder below can miss the one huge sample; the max cannot. */
    if (call > g_abt_maxcall) { g_abt_maxcall = call; entry_write_kv("xnu_live_abt_maxcall", call); }
    if (gap  > g_abt_maxgap)  { g_abt_maxgap  = gap;  entry_write_kv("xnu_live_abt_maxgap", gap);  }

    /* First four in full (fine near the head), then the powers-of-two ladder - the pet/cadence rule. */
    if (n <= 4u || (n & (n - 1u)) == 0u) {
        entry_write_kv("xnu_live_abt_n", n);
        entry_write_kv("xnu_live_abt_seq", g_sleh_seq);   /* pairs with the sleh entry record's _seq */
        entry_write_kv("xnu_live_abt_gap", gap);
        entry_write_kv("xnu_live_abt_call", call);
    }
}
#endif
```

`g_sleh_seq` (`entry_stubs.c:1665`) is already the abort ordinal, incremented in `entry_note_sleh`
(`:1750/1752`); pairing `xnu_live_abt_seq` with the sleh entry record's `xnu_live_sleh_seq`/`_far`
gives the fault address beside the span without a second field.

### 4.2 The call site (in `src/entry/entry_trace.c`)

Declare it beside the two existing sleh decls (`:5655-5657`):

```c
#if defined(STAGE90_XNU_ABT_SPAN) && STAGE90_XNU_ABT_SPAN
extern void entry_note_abt_span(uint32_t t0, uint32_t call);
#endif
```

and bracket the real call in `__wrap_sleh_abort` (`:5661`), using the file's own inline clock:

```c
    uint32_t t0 = 0u;                                   /* add beside fsr, far_, thread, recover */

    t0 = entry_counter();                               /* before __real_sleh_abort, :5687 */
    __real_sleh_abort(regs, type);
    ...
    entry_note_sleh_back(redirected);                   /* :5703 */
#if defined(STAGE90_XNU_ABT_SPAN) && STAGE90_XNU_ABT_SPAN
    entry_note_abt_span(t0, entry_counter() - t0);
#endif
```

The two reads are unconditional statements in the ON arm; in the OFF arm the guard deletes both the
recorder and the call and the wrapper body is the exact 1006 token set.

### 4.3 The companion edit that closes §2.3 (mandatory)

`entry_stubs.c` must stop referencing `entry_counter` (or the recorder above is inert and any
`machine_idle` entry ends the run). The 1006 sampler's single reference is converted to the same
argument-passing shape: give the sampler the clock,

```c
/* entry_stubs.c: the 1006 sampler takes the clock instead of calling the stubbed symbol */
void entry_freeze_sample(uint32_t now);            /* was void entry_freeze_sample(void) */
```

and have its one caller in `entry_trace.c:2308` pass the value it can read itself:

```c
    entry_freeze_sample(entry_counter());           /* __wrap_machine_idle, where the inline lives */
```

Then `entry_stubs.o` has **no** `U entry_counter`, the stub is not generated, and `entry_stubs.c`
never reaches `entry_stub_hit`. This is a measurement-integrity fix, not a behaviour change; the
build refuses the stand-in (§6e).

---

## 5. The reading → cause classifier (by value)

Given the ON image's keys and the same capture's `xnu_live_tick_sum_maxd` (`M` ≈ 468.2e6 counts) and
`xnu_live_sleh_seen` (the abort count):

| reading | cause |
|---|---|
| **(A)** `xnu_live_abt_maxcall` and `xnu_live_abt_maxgap` both ≪ `M` (e.g. ≤ a few hundred-thousand counts), several aborts, no single spike | the abort path is honest but **fast** — the 24.4 s is **not** here; it is in a **non-abort** stretch (the `copyin`/SVC path or a non-fault kernel stretch past the frame). A **negative** result for this rung, and a positive pointer elsewhere. |
| **(B)** `xnu_live_abt_maxgap` ≈ `M` with `xnu_live_abt_maxcall` small | the freeze is **between two aborts** — one abort returned, the next was not entered for 24.4 s. Something outside `sleh_abort` blocked the thread between faults. |
| **(C)** `xnu_live_abt_maxcall` ≈ `M` | a **single** `sleh_abort` (its `arm_fast_fault`+`vm_fault` body) took 24.4 s. This is the strongest positive: the frontier is inside one fault. |
| **(D)** `xnu_live_abt_n`/`_seq` **plateau** (the last aborts are the same `_seq` as the sleh tail, `n` stops while the log runs on) and no `maxcall`/`maxgap` approaches `M` | the freeze is **after the last abort** — the abort path ended before it, so the frame's return-to-user stretch (or the SVC path) is where the CPU sat. The tail is then a *non*-abort question. |

`(B)` and `(C)` are separated by *which* key pegs; `(A)` and `(D)` are separated by whether `M` is
*matched* somewhere and whether the abort count *stopped*. The paired sleh entry record
(`xnu_live_sleh_seq`/`_far`, published for all aborts under 1004's `ABORT_TRACE` caps) names the fault
address of the pegging sample.

**Falsifiers (what makes the rung wrong):**

- **The abort path never runs in the tail** (the freeze is entirely after the last abort): the keys
  are present but small and `n` plateaus — this is reading `(D)`, not a null. If instead the keys are
  **entirely absent**, the wrapper did not run or the channel was capped — the instrument, not the
  kernel, failed (§6d is the build's guard against this).
- **`entry_counter` wrap** (`2^32 / 19.2 MHz` = 223.7 s, `entry_trace.c:948-950`): adjacent aborts are
  far closer than one wrap and the freeze (24.4 s) is 9× smaller than a wrap, so a single
  `t0 - last` is unambiguous; a reader that sees a delta > `2^31` must treat it as a wrap, not a
  reading.
- **The stand-in fires** (§2.3) — prevented by the argument-passing design and §6e. If `xnu_live_stub_hit_seq`
  is non-zero in the capture, the run ended inside the abort path and the span keys are the last thing
  before it.

---

## 6. Build refusals (a comment is not a check) — `scripts/build_1007.sh`

Mirroring `scripts/build_1006.sh`'s DELTA section (`:804-883`) and its frozen-negative-control shape:

- **(a) Source shape, per file** — the `defined()`-first guard count:
  `grep -cE '^#if defined\(STAGE90_XNU_ABT_SPAN\) && STAGE90_XNU_ABT_SPAN$'` == 1 in
  `src/entry/entry_stubs.c`, == **4** in `src/entry/entry_trace.c` — the design first said 2 ("decl +
  call site"), but by value the `t0` **declaration** and its **assignment** can be separated by other
  statements, and an *unguarded* `uint32_t t0;` is an unused-local to `-Wall -Wextra -Werror`
  (`build_entry.sh:2089`) in the OFF arm. So each of `t0`'s decl, `t0`'s assignment, the `entry_note_abt_span`
  decl, and the note call carries its own guard (4 total); the recorder is defined once
  (`entry_note_abt_span(uint32_t t0, uint32_t call)` ×1); the call is one line.
- **(b) Image keys** — `strings "$elf" | grep -c 'xnu_live_abt_'` == **6**
  (n / seq / gap / call / maxcall / maxgap); `nm` shows `entry_note_abt_span` a **DEFINED** `T`/`t`
  (0 ⇒ an object did not rebuild or a stand-in was used).
- **(c) Call site** — `nm` locates `__wrap_sleh_abort`; its disassembly contains **exactly one**
  `bl <entry_note_abt_span>`, and image-wide exactly one.
- **(d) OFF control, by value** — a rebuild with `STAGE90_XNU_ABT_SPAN=0` carries `xnu_live_abt_` ×0
  and no `entry_note_abt_span` symbol (the switch truly compiles out). *(Not a "== pressed predecessor
  sha8" claim: 1007 also carries the §4.3 fix, so the OFF image is not any pressed arm; the honest
  OFF control is the symbol-level one.)*
- **(e) THE LANDMINE CLAUSE** — `out/stage90/xnu_arm_entry_realstubs.c` must contain **no**
  `^void entry_counter(void)` line, and `nm out/stage90/xnu_arm_entry_stubs.o` must show **no**
  `U entry_counter`. This forces §4.3 and refuses the terminal stand-in by value. (Verified present
  today: `realstubs.c:38`, and `entry_stubs.o` reads `U entry_counter`.)
- **(f) Negative control against the frozen 1006 elf** (`out/stage90/frozen/armed-d13-31bdebf3/…`, the
  PRESSED 1006 arm): `xnu_live_abt_` ×0 **and** no `entry_note_abt_span` symbol — a reader that cannot
  fail certifies nothing. (By value the 1006 elf *does* contain the `entry_counter` stand-in at
  `8049cce4`, so this control also demonstrates the defect 1007 closes.)
- **(g) Record + gate** — `awk -F= '$1=="STAGE90_XNU_ABT_SPAN"' "$REC"` == 1; and
  `grep -q STAGE90_XNU_ABT_SPAN scripts/preflight_boot_check.sh` (else the converse check at
  `preflight_boot_check.sh:1172` refuses the press).
- **(h) Arm must move** — the entry bin sha8 moves off `31bdebf3` (the pressed 1006 arm).

**Press-side reader:** `tools/check_1007_capture.py` (extension of `tools/check_1006_capture.py`):
presence + ladder of `xnu_live_abt_*`, the two maxima, the `_seq` tail, and the classification
`(A)/(B)/(C)/(D)` against `xnu_live_tick_sum_maxd`; plus `xnu_live_stub_hit_seq` == 0 (the §2.3
guard). Measurement only — the run is **expected** to still freeze; the freeze's LOCATION is the
finding.

---

## 7. Could not verify (honest limits)

1. **What the witness cannot see.** It brackets only `sleh_abort`. Time spent in the SVC/`copyin`
   path, in `thread_block`, or on the return-to-user stretch is invisible to it; reading `(D)` is the
   limit where the abort path is *not* the answer. A separate rung would bracket the SVC path.
2. **One sample per abort.** A freeze that ends and a second that begins within one abort is one
   reading; acceptable, the frontier is a single event.
3. **`entry_counter` is a low-32-bit clock** wrapping every 223.7 s (`entry_trace.c:948-950`); safe for
   adjacent aborts (the freeze is 24.4 s), but stated so the reading is not over-claimed.
4. **Whether `sleh_abort`'s exit hook is reached on the pegging abort.** If the exit sample is absent
   while the entry record is present, that is itself the reading ("the handler did not return") — but
   it collides with 1004's `sleh_back` gap, so the report must name the two keys together.
5. **The exact guard count `N` in §6a** is a pin to be *measured* on the first build, not derived here.
6. **I did not run any build.** Every clause above is a design, not a measured result. The one by-value
   reading I did take — the `entry_counter` stand-in at `8049cce4` — is from the current linked
   `out/stage90/xnu_arm_entry.elf` and the 1006 frozen elf.

## 8. Citations

- `records/1006-press-read.txt`; `docs/1006-terminal-frame-analysis.md` (`sleh_abort+0x468` = a
  forward branch; I-bit 0); `docs/1006-interrupt-freeze-witness-design.md`; `records/1005-press-read.txt`.
- Wrapper + clock: `src/entry/entry_trace.c:952` (`static inline entry_counter`), `:5661`
  (`__wrap_sleh_abort`), `:5685/:5687/:5703` (entry / real call / back), `:1621` (`entry_note_repair`
  passes `entry_counter()`), `:2308` (`entry_freeze_sample` call site).
- Recorder + abort record: `src/entry/entry_stubs.c:1665-1666` (`g_sleh_seq`/`g_sleh_back`), `:1744`
  (`entry_note_sleh`), `:1863` (`entry_note_sleh_back`), `:5344/:5358` (1006 sampler's `entry_counter`
  extern + call), `:5858` (`entry_note_repair` arg-passing signature), `:8558/:8636` (`entry_stub_hit`
  → `entry_epilogue`).
- The stand-in: `out/stage90/xnu_arm_entry_realstubs.c:38`; `xnu_arm_entry.elf` `8049cce4 T
  entry_counter`; `xnu_arm_entry_stubs.o` `U entry_counter`.
- XNU path: `external/xnu-hd2-darwin13/xnu/osfmk/arm/trap.c:510` (`sleh_abort`), `:613/658/668/742/754/830`
  (`vm_fault` call sites); `osfmk/vm/vm_fault.c:2741` (`vm_fault`).
- Threading: `src/entry/build_entry.sh:197/169-223` (`--wrap` set), `:310` (`STUB_DEFINES_TRACE`),
  `:1178` (FREEZE validate), `:1696` (arm keys), `:1936` (case), `:39641` (echo), `:29492`
  (`trace.o` added after pass 1); `scripts/preflight_boot_check.sh:750` (`ENTRY_CFG_KEYS`), `:1172`
  (converse check); `scripts/build_1006.sh:804-883` (DELTA + negative-control template).