# 1008 — the storm's fault IDENTITY on the direct channel: `far`/`type` for the abort that froze (design)

Status: DESIGN. Read-only: this file changes no source. Every claim below was read at the file:line
shown, and every address/constant was read **by value** from the tree and the linked image on
2026-10-14. Anything I could not verify is in **§8 (Could not verify)**. House style matches
`docs/1007-design.md`.

Rung 1007 (`records/1007-press-read.txt`, arm `armed-d13-88074ab3`, commit `3fced8d`) turned the
frontier from a WALK into a **STORM** and localised the ~24.4 s freeze **inside one `sleh_abort`**.
What it could not name is the storm's fault **address**: the storm rows are past the live channel's
cap (`ENTRY_LIVE_CAP 24576`, `entry_stubs.c:2403`), so `xnu_live_sleh_far` stops at abort #40
(`0x3ef2f000`, a kernel `copyin`), and the 1007 abort-span recorder publishes only `n`/`seq`/`gap`/
`call` on the direct channel — never `far`/`type`. **1008 extends the abort-span witness so the
`far` and the `type` of the storm's aborts — and specifically of the 18.7 s abort — reach the capture
on the DIRECT, uncapped channel.**

It is a pure MEASUREMENT: it changes no kernel behaviour, only what the abort witness publishes.

---

## 1. The frontier statement (what 1007 leaves open)

> The `~24.4 s` freeze is a **fault storm of ~2,097,152 `sleh_abort` invocations**
> (`xnu_live_abt_n == xnu_live_abt_seq == 0x200000` at every published row), not the 40 the capped
> live channel showed; and **one single `sleh_abort` invocation took 18.735 s**
> (`xnu_live_abt_maxcall = 0x1570e6a5`). The storm's fault ADDRESS is not in any capture — the live
> channel stopped at #40 and the direct channel omits `far`/`type`. **Nothing names the fault the
> storm is made of, nor the fault the 18.7 s abort handled.**

1008's question is exactly that: **which `far`, and which `type`, is the storm — and which is the
18.7 s abort.**

---

## 2. What 1007 could NOT see (the measurement gap 1008 closes)

### 2.1 The storm's `far` dies at the cap, and the direct channel never carried it

Two channels, by construction (`entry_stubs.c:2582` vs `:2689`):

| channel | writer | cap | carries `far`/`type`? |
|---|---|---|---|
| **live** (capped) | `entry_live_write` | `ENTRY_LIVE_CAP = 24576` records (`:2403`), then a `xnu_live_capped` marker (`:2592`) | **yes** — `xnu_live_sleh_far`/`_type` (`:1805-1807`), but only for `g_sleh_seq <= SLEH_LIVE_MAX` |
| **direct** (uncapped) | `entry_write_kv` | none — writes straight into `RAM_CONSOLE_BASE` (`:2692`), bounded only by the console size `0x00200000 - 12` (`:2695`) | **no** — 1007's recorder publishes `n`/`seq`/`gap`/`call` only (`:1947-1950`) |

The 1007 read is explicit (`records/1007-press-read.txt`): the last `xnu_live_sleh_seen` is **40**,
968 lines *before* the cap at :29127, and `sleh_far` stops there. So the storm's address is behind the
cap in one channel and simply not carried in the other. **The direct channel is the only one that saw
all 2.1 M aborts; 1008 puts `far`/`type` on it.**

### 2.2 The wrapper already holds `far_` and `type` — nothing new is read from the machine

`__wrap_sleh_abort(void *regs, int type)` (`src/entry/entry_trace.c:5672`) already reads the fault
pair for the sleh record:

```c
5674    uint32_t fsr, far_, thread, recover, redirected;
...
5687    if ((uint32_t)type == STAGE90_T_PREFETCH_ABT) {
5688        __asm__ volatile ("mrc p15, 0, %0, c5, c0, 1" : "=r"(fsr));   /* IFSR */
5689        __asm__ volatile ("mrc p15, 0, %0, c6, c0, 2" : "=r"(far_));   /* IFAR */
5690    } else {
5691        __asm__ volatile ("mrc p15, 0, %0, c5, c0, 0" : "=r"(fsr));    /* DFSR */
5692        __asm__ volatile ("mrc p15, 0, %0, c6, c0, 0" : "=r"(far_));   /* DFAR */
5693    }
```

`far_` and `type` are **in registers at the exact point 1007 already calls its recorder** (`:5729`).
The class→pair selection (`476`) is already correct: `STAGE90_T_PREFETCH_ABT` (3) reads IFSR/IFAR,
everything else reads DFSR/DFAR (`entry_trace.c:5585-5596`). So 1008 reads **nothing new** — it
forwards two values the wrapper is already holding.

### 2.3 The argument-passing rule 1007 established, restated as a constraint on 1008

`entry_stubs.c` **cannot** call `entry_counter()` (`docs/1007-design.md` §2.3): the symbol is a
`static inline` in `entry_trace.c:952`, so a reference from `entry_stubs.c` resolves in pass 1 to a
**terminal stand-in** (`out/stage90/xnu_arm_entry_realstubs.c`: `void entry_counter(void)
{ entry_stub_hit(...) }`) that runs `noreturn entry_epilogue` and **ends the run**. 1008 obeys the
same rule — and 1008's recorder needs `far`/`type` **by value** for the same reason it needs `call`
by value: `entry_stubs.c` has no cp15 read for the fault pair and no reason to grow one, because the
wrapper already has both.

---

## 3. The rate-limiter decision (the record budget is the whole design)

Publishing `far`/`type` **per abort** is `~2,097,152 × (29 + 30) B ≈ 124 MB` into a **2,097,140-byte**
(`0x00200000 - 12`, `:2695`) console — ~59× over, and `entry_putc` **silently drops** every byte past
the max (`:2632-2636`), so the exact failure mode is "the records that mattered were never written."
So the trigger must be bounded. **Two triggers, both bounded:**

### (i) The maxcall latch — names the 18.7 s abort

1007 already writes `xnu_live_abt_maxcall` **where it changes** (`:1942`, the `g_sleh_armed` rule,
`:1774`). 1008 writes `xnu_live_abt_maxfar`/`_maxtype` **at the same instant** — the fault identity of
the abort that set a new maximum `call`. Since `maxcall` is a **running max**, the number of writes is
the number of distinct prefix-maxima, which is *small*: the 1007 read shows the maxima escalate in
lockstep and settle at `0x1570e6a5` (a handful of latches, ~10). To remove even that dependence, the
latch is **hard-bounded by construction**:

```c
#define ABT_FAR_LATCH_MAX 32u      /* <= 32 x 2 records ~ 1 KB; the observed count is ~10 */
...
if (call > g_abtfar_maxcall) {
    g_abtfar_maxcall = call;
    if (g_abtfar_pub < ABT_FAR_LATCH_MAX) { g_abtfar_pub++;
        entry_write_kv("xnu_live_abt_maxfar",  far);
        entry_write_kv("xnu_live_abt_maxtype", type); }
}
```

Without the cap, a pathological storm whose `call` rises monotonically would write 2.1 M latches — the
same unbounded hazard 1007's maxcall already carries, now made explicit and bounded.

### (ii) The powers-of-two ladder — the storm's address seen progressively

1007 publishes a row at `n <= 4u || (n & (n-1u)) == 0u` (`:1946`). The far recorder publishes
`far`/`type` on **the same rows** (its own `n` advances once per abort, so the row set is identical).
Over this run that is **23 rows**: `{1,2,3,4} ∪ {2^k, k=0..21}` — 22 powers of two, plus `3` from the
`n<=4` clause. (The 1007 read counts 23 published rows — the ladder length is fixed by the run
reaching `n = 2^21 = 0x200000`, not by the storm's size.)

### The budget, exactly

| publisher | records per event | events | records | bytes (K+13, `:2724-2730`) |
|---|---|---|---|---|
| ladder `far`/`type`/`farn` | 3 | 23 rows | 69 | 23 × (29+30+30) ≈ **2.0 KB** |
| `maxfar`/`maxtype` latch | 2 | ≤ 32 | ≤ 64 | ≤ 32 × (31+32) ≈ **2.0 KB** |
| **1008 total** | | | **≤ 133** | **≈ 4 KB** |

Against a 2,097,140-byte console that is **~0.2 %** — three orders of magnitude of headroom, and the
1007 span channel it rides alongside is itself a few KB. **The design is bounded, not tuned.**

---

## 4. The change (one new recorder, one new switch)

### 4.1 Arm switch: **add a NEW `STAGE90_XNU_ABT_FAR` — do NOT reuse `STAGE90_XNU_ABT_SPAN`**

**Recommendation: NEW switch.** Reason, and it is the deciding one: **1007's arm must remain
reproducible**, and 1007's `scripts/build_1007.sh` pins the *source shape* and the *literal count* of
1007's tree by value:

- `build_1007.sh` — `strings "$elf" | grep -c 'xnu_live_abt_'` == **6**.
- `build_1007.sh` — `grep -cF 'entry_note_abt_span(uint32_t t0, uint32_t call)'` == **1**.
- `build_1007.sh` — `grep -cF 'entry_note_abt_span(t0, entry_counter() - t0);'` == **1**.
- `build_1007.sh` — the `defined(STAGE90_XNU_ABT_SPAN) && STAGE90_XNU_ABT_SPAN` guard count == 1
  (stubs) / **4** (trace).

If 1008 **reused** `ABT_SPAN` and added five `xnu_live_abt_*` literals under it, then an
`ABT_SPAN=1` build (1007's own arm) carries **11**, the literal-count clause fails, and 1007's build
script no longer describes 1007's tree. Reuse is therefore **excluded** — the requirement "1007's arm
must be reproducible" is exactly the argument for a new switch.

With the new switch, `scripts/build_1007.sh` **keeps working unmodified**: it sets neither
`STAGE90_XNU_ABT_FAR` nor cross-checks it, so `build_entry.sh` reads `${STAGE90_XNU_ABT_FAR:-0}` = 0,
the five new literals compile out, and the 1007 arm is rebuilt byte-for-byte (6 literals, the span
signature and ladder call **untouched**). 1007 stays reproducible **by running its own script**. The
five new literals live behind the `ABT_FAR` guard; the 1007 six are untouched.

### 4.2 The recorder (in `src/entry/entry_stubs.c`, **after** the span recorder's `#endif` at `:1953`)

It is a **separate function**, not an added parameter to `entry_note_abt_span` — because a changed
signature would break 1007's `grep -cF 'entry_note_abt_span(uint32_t t0, uint32_t call)'` clause
(§4.1). Its own `n` advances once per abort, so its ladder rows coincide with the span recorder's; its
own running `maxcall` coincides with the span's (see §8.1 for the residual).

```c
/*
 * ------------------------------------------------------- 1008: the abort-fault identity
 *
 * 1007 localised the ~24.4 s freeze INSIDE one `sleh_abort` (18.7 s) riding a STORM of ~2.1 M
 * aborts (records/1007-press-read.txt). The storm's fault ADDRESS is in no capture: the live
 * channel stopped at #40, and 1007's direct-channel recorder publishes only n/seq/gap/call. 1008
 * forwards the `far`/`type` the wrapper ALREADY read (entry_trace.c:5687-5693) to the DIRECT
 * (cap-exempt) channel, on a bounded trigger set - the record budget is the whole design
 * (docs/1008-design.md §3): a per-abort publish is ~124 MB into a 2 MB console.
 *
 * **BY VALUE, the §4.3 rule.** `far`/`type` enter as arguments because `entry_stubs.c` cannot read
 * cp15's fault pair, exactly as 1007 passes the clock (docs/1007-design.md §2.3). `call` is passed
 * too, so this recorder re-derives the running max that the span recorder derives (:1942) - the
 * latching rule is the SAME rule, applied to the same sequence of calls.
 */
#if defined(STAGE90_XNU_ABT_FAR) && STAGE90_XNU_ABT_FAR
void entry_write_kv(const char *key, uint32_t value);   /* identically to :1920 */

static uint32_t g_abtfar_n;
static uint32_t g_abtfar_maxcall;
static uint32_t g_abtfar_pub;
#define ABT_FAR_LATCH_MAX 32u

void
entry_note_abt_far(uint32_t call, uint32_t far, uint32_t type)
{
    uint32_t n = ++g_abtfar_n;

    /* (i) The maxcall latch - the fault identity of the abort whose HANDLER took the longest. */
    if (call > g_abtfar_maxcall) {
        g_abtfar_maxcall = call;
        if (g_abtfar_pub < ABT_FAR_LATCH_MAX) {
            g_abtfar_pub++;
            entry_write_kv("xnu_live_abt_maxfar",  far);
            entry_write_kv("xnu_live_abt_maxtype", type);
        }
    }

    /* (ii) The storm's address, progressively - the SAME ladder the span recorder uses (:1946). */
    if (n <= 4u || (n & (n - 1u)) == 0u) {
        entry_write_kv("xnu_live_abt_farn", n);
        entry_write_kv("xnu_live_abt_far",  far);
        entry_write_kv("xnu_live_abt_type", type);
    }
}
#endif
```

Plain statics, no allocation, bounded stores — the rule the span recorder already obeys
(`docs/1007-design.md` §4.1), because this rides a handler that re-enters itself.

### 4.3 The call site (in `src/entry/entry_trace.c`, `__wrap_sleh_abort`)

Declare it beside the span extern (`:5666-5668`), as a **separate guard** so the span guard count
(§4.1) is not disturbed:

```c
#if defined(STAGE90_XNU_ABT_FAR) && STAGE90_XNU_ABT_FAR
extern void entry_note_abt_far(uint32_t call, uint32_t far, uint32_t type);
#endif
```

and add the call **after** the span call at `:5729-5731`, again under its own guard:

```c
    entry_note_sleh_back(redirected);
#if defined(STAGE90_XNU_ABT_SPAN) && STAGE90_XNU_ABT_SPAN
    entry_note_abt_span(t0, entry_counter() - t0);
#endif
#if defined(STAGE90_XNU_ABT_FAR) && STAGE90_XNU_ABT_FAR
    entry_note_abt_far(entry_counter() - t0, far_, (uint32_t)type);
#endif
```

- `far_` and `type` are already locals/parameters (`:5674`, `:5672`).
- `entry_counter() - t0` reads the clock **a second time**; this is deliberate — hoisting `call` into
  a local would delete the pinned text `entry_note_abt_span(t0, entry_counter() - t0);` and break
  1007's reproducibility (§4.1). The two reads differ by tens of cycles; the max-change points are
  unaffected (§8.1).
- The far call is the last statement, so the compiler will likely emit a **tail `b`**, not `bl` — the
  1007 build measured exactly this for the span call. The build clause must accept `b` **or** `bl`.
- `t0` and its declaration are under the **`ABT_SPAN`** guard. Since the arm sets **both** switches 1,
  the far call can use `t0`. `build_entry.sh` must **refuse** `ABT_FAR=1` with `ABT_SPAN=0` (§4.5) —
  otherwise `t0` does not exist and the OFF-arm compile fails under `-Werror` (`build_entry.sh:2089`).

### 4.4 The exact new key literals

Five new `xnu_live_abt_*` literals, all behind the `ABT_FAR` guard:

| key | where written | reading |
|---|---|---|
| `xnu_live_abt_far` | ladder rows | the fault address of abort `farn` |
| `xnu_live_abt_type` | ladder rows | 3 = `T_PREFETCH_ABT`, 4 = `T_DATA_ABT` |
| `xnu_live_abt_farn` | ladder rows | the ordinal — pairs to `xnu_live_abt_n` by **key equality** |
| `xnu_live_abt_maxfar` | maxcall latch | fault address of the longest-handler abort |
| `xnu_live_abt_maxtype` | maxcall latch | its class |

**Literal count:** the 1008 image (`ABT_SPAN=1` **and** `ABT_FAR=1`) carries **11** `xnu_live_abt_`
literals. The 1007 image (`ABT_FAR=0`) carries **6**. So the count clause goes **6 → 11 in
`build_1008.sh`**; **`build_1007.sh` stays 6** (its arm is `ABT_FAR=0`). `grep -c` counts matching
**lines**, and `strings` emits one literal per line, so the five are five lines.

### 4.5 Bookkeeping sites for the NEW switch (every one)

| layer | file:line (anchor) | change |
|---|---|---|
| trace define | `src/entry/build_entry.sh:310` (`STUB_DEFINES_TRACE`) | append `-DSTAGE90_XNU_ABT_FAR="${STAGE90_XNU_ABT_FAR:-0}"` |
| stubs define + validate | `src/entry/build_entry.sh` after `:1191` (the `ABTSPAN` block) | `ABTFAR=${STAGE90_XNU_ABT_FAR:-0}`; `case 0|1`; `[[ $ENTRY_TRACE -eq 1 ]] && STUB_DEFINES+=(-DSTAGE90_XNU_ABT_FAR="$ABTFAR")`; **refuse `ABTFAR=1` with `ABTSPAN=0`** |
| arm-key list | `src/entry/build_entry.sh` after `:1710` (`ENTRY_ARM_KEYS`) | add `STAGE90_XNU_ABT_FAR` (count is automatic, `:1990`) |
| case arm | `src/entry/build_entry.sh` after `:1951` (the `ABT_SPAN)` arm) | `STAGE90_XNU_ABT_FAR)   _v=$ABTFAR ;;` |
| record echo | `src/entry/build_entry.sh` after `:39658` | `echo "STAGE90_XNU_ABT_FAR=$ABTFAR"` |
| press gate | `scripts/preflight_boot_check.sh` after `:751` (`ENTRY_CFG_KEYS`) | add `STAGE90_XNU_ABT_FAR` (else the converse check refuses the press) |
| build driver | `scripts/build_1008.sh` (new) | pin `EXPORTS+=("STAGE90_XNU_ABT_FAR=1")`; the 1008 DELTA (§5) |

No `pe_msm8974.c` change, no `tools/build_xnu_arm_kernel.sh` edit, no new `--wrap`. The switch reaches
exactly the two entry-toolchain objects (`entry_stubs.c` via `STUB_DEFINES`, `entry_trace.c` via
`STUB_DEFINES_TRACE`).

---

## 5. Build refusals (a comment is not a check) — `scripts/build_1008.sh`

Mirroring `scripts/build_1007.sh`'s DELTA section and its frozen-negative-control shape. **All `echo`
strings must escape backticks** (`` \` ``) or `make check`'s `check_backtick_messages` refuses — the
fault fixed in `3fced8d`. **All symbol greps go by name+params, never a `void ` prefix**
(`[[mi4-mutation-anchors-are-code-spans]]`): `grep -cF 'entry_note_abt_far(uint32_t call, uint32_t
far, uint32_t type)'`.

- **(a) Source shape, per file** — the `ABT_FAR` guard count == **1** in `src/entry/entry_stubs.c`
  (the recorder) and == **2** in `src/entry/entry_trace.c` (the extern decl + the call). The recorder
  **defined once**: `grep -cF 'entry_note_abt_far(uint32_t call, uint32_t far, uint32_t type)'` == 1.
  The call **once**: `grep -cF 'entry_note_abt_far(entry_counter() - t0, far_, (uint32_t)type);'` == 1.
- **(b) Image keys** — `strings "$elf" | grep -c 'xnu_live_abt_'` == **11**; `nm` shows
  `entry_note_abt_far` a **DEFINED** `T`/`t` (0 ⇒ an object did not rebuild or a stand-in was used).
- **(c) Call site** — `nm` locates `__wrap_sleh_abort`; its disassembly contains **exactly one**
  `(b|bl) <entry_note_abt_far>`, and image-wide exactly one. Accept **`b` or `bl`** — the last
  statement is a tail call and a `bl`-only reader false-refuses (the 1007 measured fix). The
  next-symbol `awk` must have **no early `exit`** (998's SIGPIPE/141 under `set -o pipefail`).
- **(d) OFF control, by value** — a twin rebuild with `STAGE90_XNU_ABT_FAR=0` (`ABT_SPAN=1`):
  `xnu_live_abt_` == **6** (= the 1007 literal set) and `entry_note_abt_far` symbol == 0. This clause
  IS the reproducibility guarantee — it proves the 1008 edits compile out and leave exactly 1007's key
  set. Skipped by default behind `STAGE90_1008_OFF_CONTROL=1`, same `out/stage90` swap-and-restore
  hazard and window rule as 1007 (`[[mi4-out-stage90-is-shared-live-state]]`).
- **(e) 1007 NOT REGRESSED** — the frozen 1007 elf carries `xnu_live_abt_` == **6** and a
  `T/t entry_note_abt_span` symbol, and carries **no** `xnu_live_abt_far`/`_type`/`_farn`/`_maxfar`/
  `_maxtype` literal and **no** `entry_note_abt_far` symbol — the negative control proves the 1008
  change is not in the 1007 arm.
- **(f) THE REPRODUCIBILITY CLAUSE** — run the *unchanged* `build_1007.sh` source-shape clauses against
  the current tree with `ABT_FAR` unset: the span recorder-def == 1, the span call == 1, and the
  ABT_SPAN guard count == 1 / **4**. If any drifts, the 1008 edit reached a 1007-pinned span region and
  1007 is no longer reproducible — refuse.
- **(g) Record + gate** — `awk -F= '$1=="STAGE90_XNU_ABT_FAR"{print $2}' "$REC"` == 1; and
  `grep -q STAGE90_XNU_ABT_FAR scripts/preflight_boot_check.sh`.
- **(h) Arm must move** — the entry bin sha8 moves off `88074ab3` (the pressed 1007 arm).

**Press-side reader:** `tools/check_1008_capture.py` (extension of `tools/check_1007_capture.py`):
presence + ladder of `xnu_live_abt_farn`/`_far`/`_type`, the `_maxfar`/`_maxtype` latch beside
`_maxcall`, the self-consistency `farn == n` at every paired row, and the storm-shape classification
(§6). Measurement only — the run is **expected** to still freeze; the storm's *identity* is the
finding.

---

## 6. What the capture must show to be DECISIVE (and the classifier)

1. **The 18.7 s abort's identity** — `xnu_live_abt_maxcall` (=`0x1570e6a5`) is immediately followed in
   the log by `xnu_live_abt_maxfar` and `xnu_live_abt_maxtype` (adjacency pairs them). Together they
   name the fault the longest handler serviced.
2. **The storm's address, progressively** — the ladder rows `xnu_live_abt_farn`/`_far`/`_type`.
   **The reader pairs by key equality, never by position**: each far row carries `farn == n`, the span
   row with the same `n` carries `gap`/`call`, and both pair to `xnu_live_abt_seq` (= `g_sleh_seq`) —
   so a single `(n, farn, seq, far, type, gap, call)` tuple reconstructs one abort across both
   recorders.
3. **The class** — `type` 3 (`T_PREFETCH_ABT`) vs 4 (`T_DATA_ABT`, `osfmk/arm/trap.h:70`).

| reading | cause |
|---|---|
| **(S) SPREAD** — the 23 ladder `far` values are **distinct** and march forward, and `maxfar` is one | a **fault storm of distinct addresses** — the boot is demand-faulting page by page; the 18.7 s abort is one page of it. |
| **(R) REPEAT** — the ladder `far` values **repeat the same one or two addresses**, `maxfar == far[0]` | a **retry loop / single-address thrash**, **not** a spreading storm. This reading REFUTES the storm interpretation (§7). |
| **(U) USER** — `type == 4` and `far < 0x80000000` | a **user** fault, contradicting the 1007 live tail (`sleh_far = 0x3ef2f000`, kernel `copyin`, `user=0`). Relocates the frontier to the user thread. |
| **(K) KERNEL** — `type == 4` and `far >= 0x80000000` | **kernel** data aborts (a `copyin`/`copyout` or a bad kernel pointer) — consistent with 1004/1007. |
| **(P) PREFETCH** — `type == 3` on the ladder rows | **instruction-fetch** faults — a different mechanism (a bad vector / a stale I-cache line walked by the retry). |

`(S)` vs `(R)` is the whole of 1008: it separates "the storm is ~2.1 M different faults" from "the
storm is one fault retried ~2.1 M times," which the count alone cannot.

---

## 7. Falsifiers (what reading would REFUTE the storm interpretation)

- **All identical `far`** (`maxfar == the first ladder far == every ladder far`): the ~2.1 M aborts
  are **one fault retried**, not 2.1 M distinct faults. The strongest refutation; the 1008 keys are
  built to test it.
- **`far == 0` on every storm row with `type == 4`**: a degenerate re-entry on a zeroed fault
  register, not a page walk — a mechanism, not a storm.
- **`xnu_live_abt_farn` GAPS from `xnu_live_abt_n` at a paired row**: the far recorder did not run
  once per abort — an **instrument** failure, not a kernel reading (why `farn` is published at all).
- **The far rows entirely ABSENT while `xnu_live_abt_n` is present**: the `ABT_FAR` half did not reach
  `entry_trace.c`, or the storm ended before a ladder row.
- **`xnu_live_abt_maxfar`/`_maxtype` absent while `_maxcall` is present**: the latch trigger never
  fired on the far recorder's own read — the two clock reads (§4.3) diverged by more than the storm's
  `call` spread (`[[mi4-measurement-defects]]`); treat as an instrument defect.
- **`type` outside `{3,4}`**: the wrapper's class dispatch (`:5687`) is reading neither class.

---

## 8. Could not verify (honest limits)

1. **The far recorder reads the clock a second time.** `entry_note_abt_far(entry_counter() - t0, …)`
   (§4.3) is a *second* `entry_counter()` read after the span call's, so the two recorders see `call`
   values differing by the span recorder's own runtime (tens of cycles). The **max-change points**
   coincide as long as no two consecutive aborts' calls fall within that delta of a common maximum. In
   the flat storm (`call ≈ 0`) the maxima are dominated by the few large samples, so the sets agree;
   the residual is stated so the reading is not over-claimed. This is the price of not changing
   `entry_note_abt_span`'s pinned call text.
2. **Where the far recorder's `ABT_FAR` guard count lands** (§5a: 1 stubs / 2 trace) is a pin to be
   *measured* on the first build, not derived — the caveat `docs/1007-design.md` §7.5 records for its
   own count.
3. **Whether `ABT_FAR=1` must also imply `ABT_SPAN=1`.** The design **refuses** `ABT_FAR=1,
   ABT_SPAN=0` (§4.5) because the far call uses `t0`, which the span guard owns; a later implementation
   giving the far recorder its own clock local would drop the dependency.
4. **I did not run any build.** Every clause above is a design. The literals' exact byte lengths are
   arithmetic from `entry_putc`'s format, not a measured console dump.
5. **The storm's true `far` range is unknown** — that is 1008's question.

## 9. Citations

- `records/1007-press-read.txt`; `docs/1007-design.md` (§4 the recorder + argument-passing rule, §5 the
  A/B/C/D classifier, §6 the refusals).
- Recorder + direct channel: `src/entry/entry_stubs.c:1914-1953`, `:1920`/`:2689` (`entry_write_kv`
  decl + def), `:2632-2636` (`entry_putc` drop), `:2695` (`max = 0x00200000 - 12`), `:2403`
  (`ENTRY_LIVE_CAP`), `:2582` (`entry_live_write`), `:1805-1807`, `:1665`/`:1710`, `:1942` (maxcall
  latch), `:1946` (ladder), `:1774` (`g_sleh_armed` rule).
- Wrapper: `src/entry/entry_trace.c:952`, `:5661-5732`, `:5687-5693`, `:5674`, `:5666-5668`/`:5729-5731`.
- The §2.3 stand-in: `out/stage90/xnu_arm_entry_realstubs.c`.
- Threading: `src/entry/build_entry.sh:310`, `:1185-1191`, `:1668-1710`, `:1951`, `:39657-39658`,
  `:1990`, `:2089`; `scripts/preflight_boot_check.sh:716-751`, `:1173`; `scripts/build_1007.sh` (DELTA
  template + the reproducibility pins + the tail-call fix + the arm-must-move refusal).
- XNU: `osfmk/arm/trap.c:274` (`sleh_abort`), `osfmk/arm/trap.h:70` (`T_DATA_ABT` = 4; 3 = prefetch).