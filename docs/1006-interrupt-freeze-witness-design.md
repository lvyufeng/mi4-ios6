# 1006 — the ~24.4 s whole-interrupt freeze: a freeze-detecting witness (design)

Status: DESIGN. Read-only: this file changes no source. Every claim below was read at the file:line
shown. Anything I could not verify is in **§9 (Could not verify)**.

Rung 1005 (`records/1005-press-read.txt`, arm `ff937225`, commit `bd0fe7c`) settled the STALL:
the machine's frontier is a **whole-interrupt-path freeze**. The all-interrupts counter
(`entry_board_irq_count`) and the timer-tick counter move in exact lockstep (`tick = irq − 3` for
the entire run), so there is no window in which the timer line alone is wedged; the pid-1
synchronous SVC path froze with them in the terminal window; and the tick delivery stalls for a
single **~24.4 s** interval (`xnu_live_tick_sum_maxd = 0x1be8bc01` = 468,237,313 / 19.2 MHz =
24.39 s), after which it BURSTS (count 135 → 479) while `maxd` stays pegged. The machine stays
alive (watchdog pet feeds, storage serves, `No errors detected`) but makes no forward progress.

1006 is a **timer-independent freeze-detecting witness**: a small state machine, ridden from the
IDLE path (the only path the freeze cannot stop — see §3), that reports whether the free-running
generic timer advanced while it was away, so the ~24.4 s freeze's MASK and GIC state is caught by
value at the first instant after it ends.

---

## 1. The frontier statement

> After dyld first touches a shared-cache page, **all interrupt delivery (and the synchronous SVC
> path) stops for a single ~24.4 s interval**, then resumes with a catch-up burst. The frontier
> question is WHY: is the CPU holding a long IRQ-masked window (I=1), did the GIC distributor
> re-mask the timer line, or did an ISR wedge? **No instrument in the image can answer it**, because
> every witness is tick-driven or syscall-driven and therefore stops with the thing it measures.

1005's own verdict: `(b2) a whole-interrupt-path freeze`, not a timer-line-specific wedge.

---

## 2. What 1005 could NOT see (the measurement gap 1006 closes)

**Every published counter in the image is driven by one of exactly three clocks, and all three are
either frozen by the freeze or gated in a way that hides it.**

| publisher | driver | resolves the freeze? |
|---|---|---|
| `xnu_live_tick_*` / `xnu_live_stall_*` / `entry_timer_edge_probe` | the timer IRQ | **NO** — it IS what stops; its own instrument stops with it |
| `xnu_live_cadence_t/_sync/_tick/_irq` (sample) | the SVC/syscall path (pid 1) | **NO** — 1005 measured the SVC path froze with the tick |
| `xnu_live_wdt_pet_calls` | the idle path (`entry_wdt_pet(entry_counter())`, `entry_trace.c:2392`) | **NO** — see below |

**The idle-path pet does NOT resolve the freeze either, and the reason is a gating defect in the
reading, not a wall-clock measurement.** `xnu_live_wdt_pet_calls` IS the free-running counter
(`entry_counter()` = `mrrc p15, 0, lo, hi, c14`, the 19.2 MHz generic timer, `entry_trace.c:952-958`),
but it is published only on the FEED ladder (`g_wdt_pets` at ordinals 1,2,3,4,8,16,32,64) — a
*latched, gated* event, not a per-pass sample. The 8 published `calls` values are strictly increasing
and their endpoints span `0xa8aa − 0x250 = 42586` counts = **2.218 ms**. It is tempting to conclude
"the idle loop ran for only 2.2 ms, so it was not running during the 24.4 s freeze." **That
conclusion is NOT supported by the bytes, and 1006 must not rest on it** — this is the
[[mi4-measurement-defects]] (m997) shape exactly:

- `entry_counter()` is a **low-32-bit** counter that wraps every `2^32 / 19.2 MHz ≈ 223.7 s`
  (`entry_trace.c:948-949`, verbatim: *"at 19.2 MHz it wraps every 3.7 minutes"* — the 3.7 is minutes
  in the comment, i.e. the same 223.7 s). Over the ~23-minute run it wraps ~6 times, so **a raw
  `calls` difference is not a wall-clock difference** unless the endpoints are known to be less than
  one wrap apart — and the file publishes only the 8 ladder endpoints, not the 64 passes.
- `xnu_live_tick_sum_maxd` is a **latched maximum** ("the worst elapsed ever seen"), so its first
  pegged appearance at `@:29123` does **not** place the freeze there — it is republished inside the
  terminal cluster after a freeze that may have ended anywhere between the last healthy summary
  (`@:28857`, `maxd=0x04ba1ff0` ≈ 4.13 s) and that line. **The capture carries no timestamp that
  ties the ~24.4 s gap to any feed.** (Checked by value in the 1005 capture: the 8 `pet_calls`
  ladder values, monotone, endpoints 42586 apart; no other non-zero `wdt_pet_*` anywhere.)

So the honest statement of the gap is: **no published key lets the capture say whether the freeze
overlapped the idle path, and no key can be sampled from inside the gap.** The only clock that
*advances across* the gap is the free-running generic timer (`mrrc … c14`), which the freeze cannot
stop (it is not interrupt-gated) — so it is the right yardstick, but it must be **read at the gap's
edges by a publisher that is not itself gated**. That is what 1006 builds, and its first reading
*settles* the open question rather than assuming it.

Two consequences the 1006 rung must respect:
- The freeze's peg first appears at `@:29123`; the last healthy summary is `@:28857`
  (`0x04ba1ff0`). The gap lies in that window. The machine that comes out of it has the tick
  BURSTING (135 → 479) and the SVC path frozen (`sync` held at 20).
- Because the capture cannot place the freeze relative to the idle path, the 1006 witness is
  designed so that **both** outcomes are readings: a large wfi dwell catches the freeze on the idle
  path; the *absence* of any large dwell proves the freeze was spent in non-idle kernel code (the
  `sleh_abort` fault-return loop of §6). Neither outcome is assumed.

---

## 3. Why the witness rides the IDLE path

The freeze stops the tick IRQ and the SVC path, so the witness cannot ride either. The idle path is
the one place that is **entered on every pass without being gated by a tick or a syscall**:
`__wrap_machine_idle` (`entry_trace.c:2279`) is reached whenever the scheduler has nothing to run,
and `machine_idle` (`osfmk/arm/machine_routines_asm.s:61-79`) is the machine's own last resort.

The mechanism that makes the idle path a *clock*: the wrapper is entered **before**
`__real_machine_idle`, and there is exactly one real idle call between two wrapper entries, so the
`entry_counter()` delta `d` between consecutive entries is the wall time of one full idle pass —
including a `wfi` that slept. Three consequences, each a reading:

- If the freeze is a **`wfi` that never woke** (§6), the next wrapper entry is one freeze later and
  `d ≈ 24.4 s`. The real function masks I+F around its `wfi` (`cpsid if / wfi / cpsie if`), but the
  CPU still wakes on ANY pending interrupt regardless of the mask — so a `wfi` that slept 24 s means
  the GIC stopped *signalling* the CPU, not merely that the mask was up. The GIC reads at the edge
  (§4) then say which: line disabled, or `CTLR` off, or priority-masked.
- If the freeze is a stretch of **non-idle kernel code** (the `sleh_abort` return loop of §6), the
  machine also re-enters idle one freeze later, and `d ≈ 24.4 s` again — with the CPU having been
  *running*, not sleeping.
- If the machine **never re-enters idle**, the witness is silent. That is not a null result: it is
  the reading that the freeze is a terminal non-blocking loop (§9.4).

So `d` catches the freeze on the idle path and off it alike; the GIC state at the edge (line
enabled/pending/active) is what separates "sleeping with the CPU un-signalled" from "running with the
line pending". This is the reading 1005 could not produce — a witness that does not ride the tick,
does not ride a syscall, and does not stop with either.

---

## 4. The change (one new sampler, one switch)

**Switch:** `STAGE90_XNU_FREEZE_WITNESS` (0/1), threaded like `STAGE90_XNU_CADENCE` but **smaller**,
because nothing outside the two entry toolchain objects needs the define. Verified edit points:

| layer | file:line | change |
|---|---|---|
| arm-key list | `src/entry/build_entry.sh:1641` | add `STAGE90_XNU_FREEZE_WITNESS` to `ENTRY_ARM_KEYS` |
| trace define | `src/entry/build_entry.sh:310` | add `-DSTAGE90_XNU_FREEZE_WITNESS="${STAGE90_XNU_FREEZE_WITNESS:-0}"` to `STUB_DEFINES_TRACE` |
| stubs define | `src/entry/build_entry.sh:1158-1164` | read `FREEZE=${STAGE90_XNU_FREEZE_WITNESS:-0}`, validate 0\|1, `STUB_DEFINES+=(-DSTAGE90_XNU_FREEZE_WITNESS="$FREEZE")` |
| record echo | `src/entry/build_entry.sh:39615` | `echo "STAGE90_XNU_FREEZE_WITNESS=$FREEZE"` |
| case arm | `src/entry/build_entry.sh:1916` | `STAGE90_XNU_FREEZE_WITNESS) _v=$FREEZE ;;` |
| press gate | `scripts/preflight_boot_check.sh:749` | add to `ENTRY_CFG_KEYS` (the converse check at `:1171` refuses a record key the gate does not know) |
| build driver | `scripts/build_1006.sh` | pin `EXPORTS+=("STAGE90_XNU_FREEZE_WITNESS=1")` and record it |

**No `pe_msm8974.c` change and no `tools/build_xnu_arm_kernel.sh` edit.** (Recon measured that
`STAGE90_XNU_CADENCE` has NO gated `DEFINES+=(...)` line in the kernel script — it rides only
`$EXTRA`/`XNU_KERNEL_EXTRA_DEFINES` and never reaches a pool object. FREEZE needs the define in
exactly the two entry-toolchain objects, so it rides `STUB_DEFINES`/`STUB_DEFINES_TRACE` and the env
var — the same machinery, one fewer layer.)

**The sampler**, `entry_freeze_sample()`, lives in `src/entry/entry_stubs.c` (which carries the
recorders) and is **called at the TOP of `__wrap_machine_idle`** (`entry_trace.c:2279`, before
`entry_note_idle` and `__real_machine_idle`) — i.e. once per idle pass. It holds one static
`uint32_t g_freeze_last` (the previous pass's `entry_counter()`). On each call:

1. `now = entry_counter()` (the free-running generic timer, `mrrc`).
2. `d = now - g_freeze_last`; `g_freeze_last = now`; `c = ++g_idle_passes`.
3. **Baseline (always):** on the first four calls (`c <= 4`) publish `xnu_live_idle_n`/`xnu_live_idle_d`
   — the *unfiltered* idle-pass deltas, so the run measures its own idle period and the §7 threshold
   can be judged against a reading, not an assumption.
4. **Freeze (gated on `d > MSM8974_FREEZE_MIN`, ladder `n <= 4` then powers of two):** publish
   - `xnu_live_freeze_n` (ordinal), `xnu_live_freeze_d` (`d`),
   - `xnu_live_freeze_cpsr` (CPSR here; the I-bit is `(cpsr >> 7) & 1`),
   - and the GIC state through **one helper** (see below):
     - `xnu_live_freeze_ien`  = `GICD_ISENABLER0` (0x100) bit 20 — the timer line enabled?
     - `xnu_live_freeze_pend` = `GICD_ISPENDR0` (0x200) bit 20 — the line pending?
     - `xnu_live_freeze_ctlr` = `GICD_CTLR` (0x000) — the global distributor enable.

   All three are **already named and routing-guarded** in `entry_gic.h` (`:31`, `:34`, `:36`) and in
   `check_gic_routing.py`'s `OFFSET_MAP`, so v1 introduces **no new GIC constant**. The
   `GICD_ISACTIVER0` (0x300) "line still ACTIVE" read is **deferred**: it discriminates only the
   ISR-acked-but-never-EOI'd candidate, which 1005 already retires (the handler EOIs *before*
   servicing — `pe_msm8974.c:507` writes `EOIR` before `rtclock_intr` at `:509`), and adding it would
   mean an unguarded header name plus a `src/gic.c` payload entry the recon measured is not needed
   elsewhere.

**The GIC reads go through a new one-function helper in `src/entry/entry_irq.c`**, declared in
`entry_gic.h` and declared `extern` in `entry_stubs.c` the way that file already declares `proc_pid`
/ `current_proc` (`entry_stubs.c:5329-5330`). Reasons, all measured:
- `entry_gic.h:101-102` states the four `gicd_*`/`gicc_*` accessors are *"the only place the GIC is
  addressed"*; a second file doing volatile pointer arithmetic is this project's most repeated defect.
- `gicd_read` (`entry_irq.c:107`) already adds `g_irq_gic_dist_va`, which the board PE sets
  (`pe_msm8974.c:326`) precisely because *"once XNU owns the page tables the entry image's raw
  0xf9000000 window is gone"*. So `gicd_read` is correct **both** before the map (raw window, VA
  offset 0) and after (mapped VA) — the raw constant alone would fault post-remap. This is a real
  correctness reason, not tidiness.
- The helper returns via out-parameters (`uint32_t *`) so `entry_stubs.c` needs no GIC header include
  (`entry_stubs.c` includes only `entry_saved_state.h`, `entry_slot_capture.h`, `entry_reset.h` —
  recon-verified).
- `GICD_ISACTIVER0` (0x300) is not in `entry_gic.h` today. It is added there **and** to
  `tools/check_gic_routing.py`'s `OFFSET_MAP` (`:136-160`) paired with a payload name in `src/gic.c`,
  so the value is transcription-guarded. (Recon measured the checker's `claim_offsets` iterates
  `OFFSET_MAP` only — a header name with no map entry is unchecked, so adding one is safe but
  unguarded; the map entry is what makes it a check.)

**Keys are `xnu_live_freeze_*`, cap-exempt (`entry_write_kv`, direct), bounded** by the powers-of-two
ladder (`n <= 4` in full, then `n & (n-1) == 0`) so a long freeze cannot flood the console — the pet
feed ladder's own rule (`entry_trace.c:2269-2270`).

---

## 5. How the witness separates the survivors (by value)

At the first idle pass after a >threshold gap, the three register reads plus the CPSR I-bit classify
the freeze that just ended:

| reading | cause |
|---|---|
| `freeze_d` large, `cpsr` I-bit **0**, `ien` **1**, `pend` 1 | the line was enabled and re-asserted while the CPU was busy/unbriefable — a **non-IRQ** long block or fault loop (§6), not a mask |
| `freeze_d` large, `cpsr` I-bit **1** | an **IRQ-masked window held across the gap** — candidate (i); the state at the far side of a mask is only conclusive if it recurs |
| `freeze_d` large, `cpsr` I-bit 0, `ien` **0** | the distributor's timer line was disabled across the gap — candidate (ii), the GIC re-mask (the 998 signature, caught live) |
| `freeze_d` large, `ien` 1, `pend` 1, `cpsr` I-bit 0 | enabled, pending, and the CPU not taking it — a **long non-IRQ critical path / fault loop** (the §6 hypothesis) |
| `freeze_d` large, `ctlr` **0** | the whole distributor was disabled across the gap — a global re-mask, a distinct sub-case of (ii) |

**What this rung can prove that 1005 could not:** not just "the interrupt path froze" but the MASK
and GIC ENABLE/PENDING/ACTIVE state at the freeze's edge. It is a pure MEASUREMENT (no kernel
behaviour change), so the run is EXPECTED to still freeze; the freeze's classification IS the 1006
finding.

---

## 6. The candidate the terminal frame favours

Unlike §2's retracted arithmetic, the terminal **stall frame** is read straight from the capture and
is a real datum. The 1003 stall sampler fires 15×, and the last 9 samples all hold:

```
pc=0x8001b32d   cpsr=0x60000113   lr=0x8046b95c   (a kernel frame on pid 1's stack)
```

`pc=0x8001b32d` is `enable_interrupts+5` — the instruction AFTER `cpsie i` (`machine_routines_asm.s`
`enable_interrupts`). `cpsr=0x60000113` decoded **by value**: `I=(0x113>>7)&1 = 0` (interrupts
ENABLED), `A=(0x113>>8)&1 = 1` (async abort masked), mode `0x13` = SVC. So the machine is repeatedly
caught *just after enabling interrupts*, in the `sleh_abort` region (`lr=0x8046b95c`). **The recon
lane's earlier reading that this CPSR has I=1 was an arithmetic error; REFUTED here by value.**

The natural hypothesis is a **fault-return loop** — the CPU re-entering `sleh_abort`'s return path,
enabling interrupts and immediately faulting again, so it never reaches the preemption point where
the pending timer would be taken. That is consistent with everything: the line stays ENABLED and
PENDING (nobody masked it), the tick instrument stops (the IRQ never runs), yet the CPU is busy
(not idle). **The 1006 witness's `ien`/`pend`/`act` triple at the gap edge is exactly what confirms
or rejects this**: `ien=1, pend=1` with a large `d` means the line was ready and the CPU did not take
it (a non-idle critical/fault loop); `ien=0` or `pend=0` means the line itself was taken down.

> **OWED (see §9.1):** the exact instruction at `0x8046b95c` is asserted from the symbol
> (`sleh_abort+0x468`) and the `enable_interrupts+5` pc, not yet disassembled. Until
> `sleh_abort`'s body is read around that offset, this section is a *hypothesis*, and the design does
> not depend on it — the witness reports the register state and lets the capture decide.

---

## 7. Thresholds, and the honest limits

- `MSM8974_FREEZE_MIN`: the idle path is entered between blocks, so `d` is normally the wall time of
  one idle pass (short). A freeze threshold must sit well above that. I do **not** have a measured
  idle-pass period to size it against (the `calls` deltas of §2 are the *pet-feeding* period, not the
  idle period — another thing 1006's own `d` series will measure directly). So the threshold is a
  deliberately low, published **ordinal**: a first cut at **19,200** counts (1 ms at 19.2 MHz). The
  record publishes the constant, and — crucially — the witness publishes **`n=1` in full regardless
  of threshold** (the first idle pass), so the run yields the true idle-pass baseline `d` and the
  threshold can be judged against it rather than assumed. A freeze is tens of millions of counts, so
  any threshold in the thousands is unambiguous; the exact value is not load-bearing.
- The witness reports ONE freeze shape per gap. A freeze that ends and a second that begins within
  one idle pass are indistinguishable — acceptable, because the frontier is a SINGLE ~24 s event.
- The sampler reads GIC state at the FIRST pass AFTER the freeze. If the freeze's cause CLEARS itself
  the instant it ends (e.g. a mask released exactly at the end), the post-hoc read shows the cleared
  state; this is a known limit and is stated so the reading is not over-claimed.

---

## 8. Build refusals (a comment is not a check)

Mirroring 1005, in `scripts/build_1006.sh`: by-value clauses asserting the linked image carries the
`xnu_live_freeze_*` key literals (ien/pend/act/ctlr/n/d/cpsr) and the `entry_freeze_sample` DEFINED
`T` symbol; the call site at `__wrap_machine_idle`'s head; the GIC-routing checker re-run on the new
`GICD_ISACTIVER0` map entry; the negative control against the frozen `armed-d13-ff937225` elf (freeze
literals x0); and the 1005 regressions held (cadence ×4, `xnu_live_sleh_upc` ×1, six-word save set).
The park refuses predecessor arms and requires `STAGE90_XNU_FREEZE_WITNESS=1` plus the carried set in
the record. `tools/check_1006_capture.py` (an extension of 1005's adjudicator) is the press-side
reader: presence + ladder of `xnu_live_freeze_*`, the `d` shape, and the GIC triple at the largest
`d`.

---

## 9. Could not verify

1. The exact instruction at `0x8046b95c` (`sleh_abort+0x468`) — asserted from the symbol and the
   `enable_interrupts+5` pc, not yet disassembled. A read of `sleh_abort`'s body around that offset is
   OWED before the press; §6 is a hypothesis until then and the design does not depend on it.
2. Whether the ~24.4 s freeze overlaps the idle path at all — the 1005 capture cannot say (see §2).
   **This is the central open question, and it is what 1006 exists to answer**, not an assumption the
   design makes. (This section replaces an earlier claim, now retracted, that the pet `calls` proved
   the idle loop was not running: `entry_counter()` wraps every ~3.7 min and the pet ladder publishes
   only 8 gated endpoints, so that arithmetic was the m997 measurement-defect shape.)
3. `GICD_ISACTIVER0` (0x300) is **deferred out of v1** (see §4): the ISR-wedge candidate it would
   discriminate is already retired by 1005 (the handler EOIs *before* servicing,
   `pe_msm8974.c:507`), and it is the only read that would need a new GIC constant. If the v1
   reading leaves the ISR wedge un-separated, v2 adds it with the header name + `check_gic_routing.py`
   `OFFSET_MAP` entry paired to a `src/gic.c` name (recon: the checker iterates `OFFSET_MAP` only, so
   a bare header name would be unguarded).
4. Whether `__wrap_machine_idle` re-enters after the freeze: it is entered before
   `__real_machine_idle`, so the sample fires on the NEXT entry. If the machine never leaves a `wfi`
   (or the fault loop never yields), there is no next entry and the witness is **silent** — which is
   itself a reading (§3), distinguishable from a freeze that ends by the concurrent survival of the
   other channels.
5. The size of `MSM8974_FREEZE_MIN` relative to the real idle-pass period: no measured idle period
   exists (§7). The witness's own `n<=4` baseline publishes it, so the threshold is judged against a
   reading on the FIRST run rather than assumed.