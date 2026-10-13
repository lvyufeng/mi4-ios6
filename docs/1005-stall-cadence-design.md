# 1005 — the STALL: a cadence-ratio witness (design)

Status: DESIGN. Read-only: this file changes no source. Every claim below was read at the
file:line shown. Anything I could not verify is flagged in **§9 (Could not verify)**.

Rung 1004's corrected press read (`records/1004-press-read.txt`) settled the frontier: the machine
boots, dyld walks `ImageLoader::link` faulting ~24 of its own pages forward (advance ×21, repeat ×0 —
VERDICT **WALK**), faults the first shared-cache page at `far=0x3e8dd000` inside a kernel `copyin`
(`pc=0x80495590` = `.Lcopyin_guts`) and **survives it**. Then the tick delivery collapses:
`xnu_live_tick_sum_maxd` reaches `0x1be8b709` (~23 s max inter-tick gap) while the machine is still
alive (`xnu_live_wdt_pets` climbs to 0x61, storage reads complete, pid 1 makes `mach_absolute_time`
syscalls with an advancing `lr`). The frontier is the STALL, not a hang in `sleh_abort`'s return path.

1004 named the tiebreaker itself: a **timer-independent cadence witness** — a monotonic counter
bumped from a synchronous path beside the per-tick counter, and the RATIO read at the end.

---

## 1. The frontier statement

> After dyld first touches a shared-cache page, **interrupt/tick delivery degrades toward a stop**.
> The frontier question is whether that is the tick source specifically (dead/wedged timer line ⇒
> `b1`), all interrupt delivery (a long IRQ-masked window ⇒ `b2`), or everything at once (a full
> stop) — and the current instruments cannot answer it because every cadence record rides the tick
> it is trying to measure.

The existing tick instrument is self-referential: `xnu_live_tick_irq` (pe_msm8974.c:509) and
`xnu_live_stall_*` (entry_stubs.c:2736-2742) are published **only from inside the timer IRQ** (the
powers-of-two gate at pe_msm8974.c:508). When the timer stops, the instrument stops with it — the
log cannot tell "the tick slowed" from "the channel stopped". 1005 adds a second, **synchronous**
observer that does not ride the tick.

---

## 2. Grounding (file:line, by value)

### 2.1 The tick publication (gated, not cap-exempt)

- The per-tick counter: `static unsigned int g_msm8974_tick_count;` — `src/platform/darwin13/pe_msm8974.c:192`.
- Incremented once per timer delivery: `++g_msm8974_tick_count;` — `pe_msm8974.c:507`.
- Published only on the powers-of-two gate: `if ((g_msm8974_tick_count & (g_msm8974_tick_count - 1u)) == 0u) entry_live_write("xnu_live_tick_irq", g_msm8974_tick_count);` — `pe_msm8974.c:508-509`.
- The stall sampler rides the same gate: `entry_note_stall_sample((const uint32_t *)context);` — `pe_msm8974.c:516`, itself guarded by the tick ladder; its recorder writes `xnu_live_stall_*` via `entry_write_kv` at `entry_stubs.c:2736-2742`.

So **both** the tick counter's publication **and** the sampler are timer-IRQ-driven. Neither can
witness a tick freeze from outside the tick.

### 2.2 The aggregate publication (cap-exempt, but still tick-gated in *content*)

`entry_tick_summary()` — `pe_msm8974.c:594-601` — writes `xnu_live_tick_sum_count` / `_anom` / `_maxd`
through the **DIRECT** channel (`entry_write_kv`, cap-exempt by construction; the tombstone's own
writer, `entry_stubs.c:2619`). It is called from two places, both outside the IRQ:

- `entry_note_exec_returned` — `entry_stubs.c:5215` (a tail call, per build_1004.sh:500).
- `entry_wdt_pet`'s sparse gate — `entry_trace.c:2245`, inside `if (entry_live_ready() != 0u && (calls & (calls - 1u)) == 0u)` at `entry_trace.c:2237`.

`_maxd`/`_count` are plain statics written only from the timer paths (single CPU, `pe_msm8974.c:203`),
so at read time `xnu_live_tick_sum_maxd` is the max gap seen so far and `xnu_live_tick_sum_count` is
every delivery — but the values are only sampled when a pet/exec-done happens to fire. This is the
hook 1005 rides for its own late snapshot.

### 2.3 The USVC (syscall) publication (cap-exempt, synchronous)

- Counter: `uint32_t g_usvc_n;` — `entry_stubs.c:5264`; incremented `n = ++g_usvc_n;` — `entry_stubs.c:5284`, **before** the `state == 0` / window tests (so it is monotone on every arrival).
- The recorder `entry_note_user_svc` writes via `entry_write_kv` (DIRECT/cap-exempt) — `entry_stubs.c:5287…5305`.
- The hook: `tools/patch_d13_usvc.py` inserts `bl _entry_note_user_svc` into `fleh_swi` in `osfmk/arm/traps_lo.s`, gated by `STAGE90_XNU_USVC` — documented at `entry_stubs.c:5231-5239`. This path is **synchronous** (it runs on the syscall, not on an interrupt), which is exactly the timer-independent observer 1005 needs.

The USVC path is the correct place to bump the sync counter: it is already compiled in (`USVC=1`,
`build_1004.sh:176/231`), already cap-exempt, and already keeps publishing to the log's end.

### 2.4 The IDT/IRQ path (any line)

`msm8974_handle_interrupt` — `pe_msm8974.c:457` — reads `GICC_IAR` (`:474`), special-cases the
timer INTID (`:481`), and for every other line EOI's without servicing (`:526`). There is **no**
all-lines counter today; one must be added to discriminate "the timer line specifically" from "all
interrupts stopped".

### 2.5 The abort counters (the second instrument)

- Globals: `uint32_t g_sleh_seq;` / `uint32_t g_sleh_back;` — `entry_stubs.c:1665-1666`.
- Increments: `g_sleh_seq++;` — `entry_stubs.c:1749`; `g_sleh_back++;` — `entry_stubs.c:1861`.
- Both are plain non-atomic read-modify-writes. `entry_note_sleh` / `entry_note_sleh_back` are called from `__wrap_sleh_abort` — `entry_trace.c:5659` / `:5677`.
- The machine is multi-CPU (`records/1004-press-read.txt`: "`sleh_back` as a death marker (non-atomic across CPUs)"). A cross-CPU lost update on `++` can make `g_sleh_back` read low and **masquerade as a non-return**. This is the MEASUREMENT defect class ([[mi4-measurement-defects]]).

### 2.6 The switch-threading precedent (STAGE90_XNU_ABORT_TRACE, 1004)

- OFF spellings (`mi4-off-option-two-spellings`): `#if defined(STAGE90_XNU_ABORT_TRACE) && STAGE90_XNU_ABORT_TRACE` at `entry_stubs.c:1709` with the `#else` branch the exact old tokens `entry_stubs.c:1713-1716`.
- build_entry.sh: validate `0|1` — `:1137-1142`; thread into `STUB_DEFINES` — `:1146`; readback in `ENTRY_ARM_KEYS` loop — `:1891`; key listed in `ENTRY_ARM_KEYS` — `:1662`.
- build_1004.sh: the switch-set list — `:64-72`; `EXTRA` entry — `:96-103`; `export` — `build_1004.sh:183` (STALL's export, same rule); `EXPORTS+=` for the entry build — `:244`; the arm-must-move guard — `:262`.
- The preflight gate's `ENTRY_CFG_KEYS` — `scripts/preflight_boot_check.sh:716-748` — must carry every switch the record names, or the **converse check** refuses the press (this is exactly what blocked 1004's first press preflight — `records/1004-press-read.txt`).

---

## 3. Instrument 1 — the cadence-ratio witness

**Goal:** a monotone counter on the synchronous (syscall) path, cap-exempt, published beside the
tick counter and an all-IRQ counter, so that at read time we can ask: *does the syscall counter keep
climbing while the tick counter is frozen?*

### 3.1 Keys

`xnu_live_cadence_t` (sample ordinal = a monotone time proxy),
`xnu_live_cadence_sync` (USVC/syscall count),
`xnu_live_cadence_tick` (timer deliveries — read of `g_msm8974_tick_count`),
`xnu_live_cadence_irq` (ALL interrupt deliveries).

Four unique literals (the falsifier asserts exactly 4 — §6).

### 3.2 Exact code change — `src/platform/darwin13/pe_msm8974.c`

Add the all-lines counter and two accessors (guard exactly as ABORT_TRACE; OFF = byte-identical):

```c
/* 1005: the cadence witness's board half. g_msm8974_irq_count counts EVERY interrupt
 * delivery (all lines), beside g_msm8974_tick_count (the timer line only, :192). Read by
 * entry_stubs.c's summary through the two accessors below (a plain load; single CPU per
 * counter, so no atomics are needed and none are used). OFF: the guard compiles out. */
#if defined(STAGE90_XNU_CADENCE) && STAGE90_XNU_CADENCE
static uint32_t g_msm8974_irq_count;
uint32_t entry_board_tick_count(void) { return (uint32_t)g_msm8974_tick_count; }
uint32_t entry_board_irq_count(void)  { return g_msm8974_irq_count; }
#endif
```

Increment the all-lines counter once per delivery, in `msm8974_handle_interrupt`, immediately after
the `g_msm8974_gicc == 0` guard (so it counts real deliveries, before the spurious shortcut at `:475`):

```c
#if defined(STAGE90_XNU_CADENCE) && STAGE90_XNU_CADENCE
    ++g_msm8974_irq_count;
#endif
```

### 3.3 Exact code change — `src/entry/entry_stubs.c`

Declare the two board accessors and add the sample/summary recorders **beside `g_usvc_n`** (after
`entry_stubs.c:5264`):

```c
#if defined(STAGE90_XNU_CADENCE) && STAGE90_XNU_CADENCE
extern uint32_t entry_board_tick_count(void);
extern uint32_t entry_board_irq_count(void);
static uint32_t g_cadence_sync;
static uint32_t g_cadence_samples;

/* First CADENCE_FULL_N samples in full (fine near the frontier), then the powers-of-two ladder —
 * the USVC window's own rule (entry_stubs.c:5266), so a 23-minute run costs ~2 records/doubling. */
#define CADENCE_FULL_N 16u

__attribute__((noinline)) void
entry_cadence_sample(void)
{
    uint32_t s;
    if (entry_live_ready() == 0u)
        return;
    s = ++g_cadence_samples;
    if (s <= CADENCE_FULL_N || (s & (s - 1u)) == 0u) {
        entry_write_kv("xnu_live_cadence_t", s);
        entry_write_kv("xnu_live_cadence_sync", g_cadence_sync);
        entry_write_kv("xnu_live_cadence_tick", entry_board_tick_count());
        entry_write_kv("xnu_live_cadence_irq", entry_board_irq_count());
    }
}

/* The late snapshot: the last cadence numbers at a moment that is NOT a syscall — so a run whose
 * syscalls stop still gets one final (sync, tick, irq) triple from the pet/exec-done gate. */
void
entry_cadence_summary(void)
{
    if (entry_live_ready() == 0u)
        return;
    entry_write_kv("xnu_live_cadence_sync", g_cadence_sync);
    entry_write_kv("xnu_live_cadence_tick", entry_board_tick_count());
    entry_write_kv("xnu_live_cadence_irq", entry_board_irq_count());
}
#endif
```

Wire the sample to the syscall path — in `entry_note_user_svc`, right after the increment
(`entry_stubs.c:5284`):

```c
    n = ++g_usvc_n;
#if defined(STAGE90_XNU_CADENCE) && STAGE90_XNU_CADENCE
    g_cadence_sync = n;
    entry_cadence_sample();
#endif
```

Wire the summary to the exec-done point — in `entry_note_exec_returned`, after `entry_tick_summary();`
(`entry_stubs.c:5215`):

```c
#if defined(STAGE90_XNU_CADENCE) && STAGE90_XNU_CADENCE
    entry_cadence_summary();
#endif
```

### 3.4 Exact code change — `src/entry/entry_trace.c`

Declare the summary beside `entry_tick_summary` (`entry_trace.c:2136`) and call it inside the pet's
existing sparse gate, right after `entry_tick_summary();` (`entry_trace.c:2245`):

```c
#if defined(STAGE90_XNU_CADENCE) && STAGE90_XNU_CADENCE
    entry_cadence_summary();
#endif
```

`entry_cadence_summary` joins the same gate the tick summary already rides (`entry_trace.c:2237`), so
the cadence gets post-cap snapshots across the whole run without a new publish site.

---

## 4. Instrument 2 — the `sleh_seq`/`sleh_back` atomicity fix (a MEASUREMENT fix)

**Why:** `g_sleh_back++` (`entry_stubs.c:1861`) is a non-atomic read-modify-write on a field reached
from `__wrap_sleh_abort` (`entry_trace.c:5677`). On a MULTI-CPU box a lost update could make
`xnu_live_sleh_back` read below `xnu_live_sleh_seq` and **masquerade as a non-return**.

**Honest scope (corrected 2026-10-13 by the adversarial re-derivation):** the 1004 capture boots
**UNIPROCESSOR** (`real_ncpus: 1`, `pmap_asid_ncpus: 1`; the header's `cpu_count=4` is the DEVICE-TREE
value, not the booted count). So the *observed* missing back-note is NOT this race, and its cause is
not proven. This instrument is therefore **hygiene, not a diagnosis**: it removes a POTENTIAL
false-positive (and would matter the moment the box boots SMP), and it must NOT be cited as the
explanation for 1004's missing `_back`.

**Minimal edit (primary):** make both increments atomic, gated under the same switch:

```c
/* entry_stubs.c:1749 */
#if defined(STAGE90_XNU_CADENCE) && STAGE90_XNU_CADENCE
    __atomic_fetch_add(&g_sleh_seq, 1u, __ATOMIC_RELAXED);
#else
    g_sleh_seq++;
#endif

/* entry_stubs.c:1861 */
#if defined(STAGE90_XNU_CADENCE) && STAGE90_XNU_CADENCE
    __atomic_fetch_add(&g_sleh_back, 1u, __ATOMIC_RELAXED);
#else
    g_sleh_back++;
#endif
```

**Fallback (if the toolchain lowers `__atomic_fetch_add` to a libcall):** store the seq into the
`_back` record instead of counting it. Change the signature to `entry_note_sleh_back(uint32_t seq,
uint32_t redirected)`, capture `g_sleh_seq` into a local in `__wrap_sleh_abort` after the
`entry_note_sleh(...)` call (`entry_trace.c:5659`), and pass it through at `entry_trace.c:5677`; then
`xnu_live_sleh_at_back` (`entry_stubs.c:1880`) already carries the seq and the back count is *derived*
from distinct seq values rather than from a racy `++`. The build-refusal in §6 (b3) decides which path
was taken (ldrex/strex present ⇒ primary is safe).

Either edit is a pure measurement fix and changes no kernel behaviour. It is gated under
`STAGE90_XNU_CADENCE` only to keep the OFF arm byte-identical (it could equally be made permanent in a
follow-up).

---

## 5. Switch threading — `STAGE90_XNU_CADENCE`

Everywhere the guard is spelled `#if defined(STAGE90_XNU_CADENCE) && STAGE90_XNU_CADENCE` (the
`mi4-off-option-two-spellings` rule; the ABORT_TRACE precedent at `entry_stubs.c:1709`). With the
macro absent/0, every `#else`/no-op branch is the current token set ⇒ **OFF is byte-identical**.

**New file `scripts/build_1005.sh`** = 1004's switch set verbatim (`build_1004.sh:64-103`, `:135-190`)
+ ONE new switch:

1. `EXTRA="$EXTRA -DSTAGE90_XNU_CADENCE=1"` — reaches `pe_msm8974.c` via `XNU_KERNEL_EXTRA_DEFINES`
   (the STALL path, `build_1004.sh:88-95`).
2. `export STAGE90_XNU_CADENCE=1` — before the pool/platform build, same rule STALL's export follows
   (`build_1004.sh:183`). (No assembly reads it, but the export keeps the three spellings one value.)
3. `EXPORTS+=("STAGE90_XNU_CADENCE=1")` — the entry build's env (`build_1004.sh:244`).
4. Keep the `STAGE90_HW_WATCHDOG_TIMEOUT_S 29u` payload constant (`build_1004.sh:43-44/269`).
5. Run `tools/check_1005_capture.py` clauses + the §6 falsifiers at the tail (`build_1004.sh:562-627`
   shape), including the negative control against the frozen 1004 elf
   (`out/stage90/frozen/armed-d13-582a19c8/…`, the pressed 1004 arm).

**`src/entry/build_entry.sh`**:

1. Validate: `CADENCE=${STAGE90_XNU_CADENCE:-0}` + `case 0|1` refusal (beside `:1137-1142`).
2. Stub compile: `[[ $ENTRY_TRACE -eq 1 ]] && STUB_DEFINES+=(-DSTAGE90_XNU_CADENCE="$CADENCE")`
   (beside `:1146`) ⇒ reaches `entry_stubs.c`.
3. Trace compile: append `-DSTAGE90_XNU_CADENCE="$CADENCE"` to `STUB_DEFINES_TRACE` (the vector at
   `:306`) ⇒ reaches `entry_trace.c` (its own command line, `:2332-2333`).
4. `ENTRY_ARM_KEYS`: add `STAGE90_XNU_CADENCE` (beside `:1661-1662`).
5. Readback: `STAGE90_XNU_CADENCE) _v=$CADENCE ;;` (beside `:1891`) so the record and the two
   compiled objects cannot be built from two spellings ([[mi4-one-value-two-definitions]]).

**`scripts/preflight_boot_check.sh`**: add `STAGE90_XNU_CADENCE` to `ENTRY_CFG_KEYS` (`:748`, after
`STAGE90_XNU_ABORT_TRACE`). **Required** — the converse check (`:1170-1172`) refuses a press whose
record names a key the gate does not print, which is exactly how 1004's first press preflight failed.

Because `build_1005.sh` is 1004's set verbatim + one switch, the entry bin sha8 must MOVE off the
pressed 1004 arm `582a19c8` (the arm-must-move guard, `build_1004.sh:262`).

---

## 6. Falsifiers (build-refusal clauses — `mi4-a-claim-in-a-comment-is-not-a-check`)

All in `build_1005.sh`, each an `exit 1` on failure.

**(a) Source side (staged shape).**
- `grep -c '^#if defined(STAGE90_XNU_CADENCE) && STAGE90_XNU_CADENCE$'` over the four touched files
  == the expected count (one block each in pe_msm8974.c accessors + pe handler incr + stubs summary +
  stubs USVC call + stubs exec-done call + trace pet call + 2 sleh = a pinned N; a drifted N a hand
  edit moved a guard). The two sleh edits each contribute one, so the count is checked per-file, not
  in aggregate.
- `grep -c 'entry_cadence_sample()' src/entry/entry_stubs.c` == 2 (definition + the USVC call);
  `grep -c 'entry_cadence_summary()'` == 3 (definition + exec-done + pet).

**(b) Image side (by value).**
- `strings out/stage90/xnu_arm_entry.elf | grep -c 'xnu_live_cadence_'` == **4** (t/sync/tick/irq).
- `nm` shows `entry_board_tick_count`, `entry_board_irq_count`, `entry_cadence_sample`,
  `entry_cadence_summary` each as a **defined** `T`/`t` in the linked image (0 ⇒ an object did not
  rebuild or a generated stand-in was used).
- Call-site clause: `entry_note_user_svc`'s own disassembly contains **exactly one**
  `bl <entry_cadence_sample>`; `entry_note_exec_returned` and `entry_wdt_pet` each call
  `entry_cadence_summary` **exactly once** (the `(b|bl)` reader of build_1004.sh:500 — a tail call is
  a valid spelling).
- **(b3) atomicity:** `arm-none-eabi-objdump -d out/stage90/xnu_arm_entry_stub.o` and surface the
  `g_sleh_seq`/`g_sleh_back` increment sites — assert they carry `ldrex`/`strex` (or, if the fallback
  was taken, that `entry_note_sleh_back` takes two args and `xnu_live_sleh_at_back` is written). If
  the object shows `bl __atomic_fetch_add_4`, the primary edit is a libcall the freestanding link may
  not resolve ⇒ **fail the build** and take the fallback.

**(b3-verified, 2026-10-13).** Settled EMPIRICALLY with the build's own flag set
(`arm-none-eabi-gcc -mcpu=cortex-a15 -marm -O2`): `__atomic_fetch_add(&g,1u,__ATOMIC_RELAXED)`
emits INLINE `ldrex r2,[r3]; add; strex; cmp/bne` with **no undefined symbols** (`nm … | grep ' U '`
empty). The §4 PRIMARY edit is safe; the §4 fallback is **not needed**. (b3) stays in the build to
guard against a future flag change re-introducing a libcall.

**(c) OFF is byte-identical.**
- Rebuild with `STAGE90_XNU_CADENCE=0` (macro absent) and assert
  `sha256sum out/stage90/xnu_arm_entry.bin | cut -c1-8` == `582a19c8` (the pressed 1004 arm). A
  different value ⇒ a guard leaked or an `#else` is not the old token set.

**(d) Negative control (a reader that cannot fail certifies nothing).**
- The frozen 1004 elf (`out/stage90/frozen/armed-d13-582a19c8/xnu_arm_entry.elf`, or
  `out/stage90/captures/…`): `xnu_live_cadence_` literal count == **0** and no
  `entry_cadence_sample`/`entry_cadence_summary` symbol. If it already carries them, the reader cannot
  discriminate the two arms.

**(e) Record.**
- `awk -F= '$1=="STAGE90_XNU_CADENCE"{print $2}' out/stage90/xnu_arm_entry-config.txt` == `1` (the
  fresh record parks the arm with the new key).
- `ENTRY_CFG_KEYS` in `preflight_boot_check.sh` contains `STAGE90_XNU_CADENCE` (grep the file) — else
  the preflight converse check refuses the press.

---

## 7. Adjudicator — `tools/check_1005_capture.py`

Start from a copy of `tools/check_1004_capture.py` (keeps the `Capture` reader at `:219-300` and the
DEATH GATE `[0]`, `:381-414`). Add **Section [C]**:

1. **Read the samples**, ordered:
   `samples = cap.groups_of("xnu_live_cadence_t", "xnu_live_cadence_", ("sync","tick","irq"))` using
   the existing `groups_of` (`:267-278`), plus the `entry_cadence_summary` triples (which publish
   `_sync/_tick/_irq` with **no** preceding `_t` — read them as separate ordered `(ln, sync, tick,
   irq)` rows via `cap.all`).
2. **Per-sample deltas** over the sampled window: `dt = tick[-1] - tick[0]`,
   `ds = sync[-1] - sync[0]`, `di = irq[-1] - irq[0]`. Headline ratio `r = ds / max(dt,1)`.
3. **Verdict rule** (all three counters are monotone; `tick` frozen means delivery stopped):
   - `dt <= 2` **and** `ds >= 8` ⇒ **`(b1)` DEGRADED/DEAD TIMER — synchronous (syscall) progress
     continues while the tick stops.** Refine by `irq`:
     - `di <= 2` ⇒ **all interrupts stopped** while syscalls continue ⇒ a long **IRQ-masked** window
       (`(b2)`, a critical section).
     - `di` climbing ⇒ the **timer line specifically** (GIC line wedged / timer device) ⇒ `(b1)`.
   - `dt <= 2` **and** `ds <= 2` ⇒ **FULL STOP** — nothing is progressing (toward death).
   - `dt` climbing ⇒ **no stall in the sampled window**; read the tail / the post-exec `_summary`
     triples (the stall is past the last cadence sample).
4. **Frontier line**: print the last sample's `(ln, t, sync, tick, irq)` and `r`; the ratio is the
   reading, the deltas are the evidence.
5. **Death gate**: re-use `[0]` verbatim — a cadence verdict is absence-based (a frozen `tick`), so it
   counts only if the boot reached the print site.

The ratio is only meaningful because the `_summary` triples ride the pet gate (`entry_trace.c:2245`):
they give a `(sync, tick, irq)` snapshot at an instant that is **not** a syscall, so "sync froze" is
distinguishable from "the log ended".

---

## 8. What this does NOT do

- **No kernel behaviour change.** Both instruments are pure measurement; the frontier cannot move
  because of them.
- **Does not date the stall to a CNTVCT instant.** The counters carry no timestamp; the time proxies
  are the sample ordinal `_t` and the wdt-pet log line numbers.
- **Does not instrument `sleh_abort`'s return path** (1004: no evidence of a hang there) and does not
  treat `sleh_back` as a death marker — the atomicity fix only removes a measurement false-positive.
- **Does not close the cause** of the tick freeze beyond partitioning it into
  timer-line-vs-all-IRQs-vs-full-stop; it narrows, it does not prove which GIC line or device.
- **Adds no atomics to the tick/IRQ counters** — those are per-CPU plain statics read at one moment,
  so `++` is correct and an atomic there would be cargo-cult.
- **Does not re-measure the abort trace** — 1004's caps/witness stay as they are (1004 kept verbatim).

---

## 9. Could not verify (flagged)

- **Whether GCC lowers `__atomic_fetch_add` to `ldrex/strex` or a `__atomic_fetch_add_4` libcall** —
  **RESOLVED 2026-10-13** (see §6 (b3-verified)): with `-mcpu=cortex-a15 -marm -O2` it is INLINE
  `ldrex/strex`, no libcall, no undefined symbol. The §4 primary edit is safe.
- **The exact disassembly shape** of the USVC call site in the linked image (I read the source:
  `n = ++g_usvc_n;` at `entry_stubs.c:5284`); the call-site falsifier (§6b) reads it by value at build
  time rather than asserting a spelling here.
- **That `pe_msm8974.o` is guaranteed in pass 1's `LINK_OBJS` alongside `entry_stubs.o`** — the two
  accessors crossed from `entry_stubs.c` to `pe_msm8974.c` are the same direction the existing
  `entry_tick_summary` call already uses (`entry_stubs.c:5215` references a `pe_msm8974.c` symbol), so
  the link is known good for this shape; the build's `nm` clause (§6b) confirms no generated stand-in.
- The exact `N` in the guard-count falsifier (§6a) is a pin to be *measured* on the first build, not a
  number I derived, because it depends on how many separate regions each edit lands in.
- I did not run any build; every clause above is a design, not a measured result.