# 1015 — the build/verify harness (evidence)

Lane: `b1015-script`. Deliverables, all new:
* `scripts/build_1015.sh` — the rung's build + verify script (modeled verbatim on `scripts/build_1014_v1.sh`).
* `tools/check_1015_capture.py` — the capture reader implementing design §3a + §3b + §3c + §3d.
* this file.

The design (`docs/1015-design.md`) is the sole spec; nothing here re-decides a verdict cell.

**Nothing in this doc was measured on the device.** No press, no device state was touched.

## 0. 1015 is ENTRY-ONLY — and why

**Design decision (lead, 2026-10-15).** Design §2 originally carried two halves. 1015 now builds
**only the passive half** (`STAGE90_XNU_PERM_WITNESS`); the ACTIVE half
(`STAGE90_XNU_USER_FULL_FLUSH`) is **not built at all** — not exported, not pinned, not staged in the
tree, and absent from the arm's record. `scripts/build_1015.sh` asserts that absence by value, so the
removal cannot decay into an assumption.

Three reasons, in the lead's own order:

1. **The AT/PAR cell makes the passive half decisive, so the active half is not needed to decide
   H-G.** `_wdatpar` / `_wdatparpr` (§3c) are `armreg_va2pa_uw_ns` + `armreg_par_read`: the CPU
   describing, *at the abort*, the translation it would use for the storm's VA. `_wdatpar & 1 == 1`
   (the MMU itself refuses the write) while the walked leaf is ap=`0b11` / apx=`0` / nG=`0` /
   domain=`0` is a **hardware** confirmation that the translation in use is not that leaf — H-G at
   the hardware, in ONE run, with **no press ambiguity**. The active half answers the same question
   indirectly, as a difference between two runs, and can only ever read V-G3 on a run where the poke
   did not land.
2. **The active half is a behaviour change** — the first in this tree since 986 — bought to answer a
   question the passive half already answers. **Measure first, then change.**
3. **It would have broken this harness's own purity assertion.** With the widening ON the ASID arm
   compiles out, so `osfmk_arm_pmap.o`'s TLBIASID count DROPS. The assertion that the flush arms are
   intact — a reading the harness depends on — would have had to be relaxed to accommodate the very
   edit it exists to police.

**Consequences, all asserted rather than stated:**

| consequence | where it is asserted |
|---|---|
| `EXTRA` has no `-DSTAGE90_XNU_USER_FULL_FLUSH=1`; no `export`; no `EXPORTS+=` pin | `scripts/build_1015.sh` (grep-able; the OFF block *fails* if the token reappears in `EXTRA`) |
| the pool object carries **x0** `USERFULLFLUSH` markers and **exactly 1014's** arm counts (TLBIASID x2, TLBIALL x2 — equalities, not `>=`) | the pool-purity block |
| the staged tree carries **x0** 1015 pool markers | the pool-purity block |
| the fresh record does **not** carry `STAGE90_XNU_USER_FULL_FLUSH=1` | clause `(e)` |
| `src/entry/build_entry.sh` names the removed switch on **0 lines** | clause `(c)` |
| the frozen 1014 elf is still the negative control (x0 `wd*`/`leafng`/`at*` literals, x0 `entry_note_abt_wd`) | clause `(d)` |

**The arm therefore moves for the entry objects alone:** `armed-d13-$(sha8 of the entry bin)`, where
the kernel pool is 1014's `osfmk_arm_pmap.o` byte for byte. The sha8 is **not** the value bundled
with the design — the design's arm presumed the widening. `scripts/build_1015.sh` prints the value
and the neighbours it must differ from.

**And the reader's verdict cell moved with it.** The §3a V-G1/V-G2 branch (the storm STOPPED/PERSISTS
question) is **structurally unreachable** on a real 1015 arm: no widening is compiled, `_wfkind=1`
cannot be published, so the inherited cell reads **V-G3 every time**. The reader keeps the §3a table
(it is the pre-registration, the frozen-1014 negative control is certified through it, and a later
rung that *does* widen the flush is read by it) but **its headline is now the AT/PAR cell** — see
§1.4 and the final `== 1015 VERDICT ==` block.

---

## 1. The reader — `tools/check_1015_capture.py`

### 1.1 Structure

| piece | what it does |
|---|---|
| `Capture` | the same token fields as `check_1014_capture.py`, plus `.all(key)` |
| `aborts()` | the abort table, **forward-paired** (see §2.1) |
| `classify_storm()` | the tail (`_pcn >= 0x400`), the storm (`far == 0xe6000`), the stop *definition* |
| `s_primary()` | the **inherited** §3a table: V-G5 → V-G3 → V-G4 → V-G1/V-G2. On an entry-only arm only V-G3/V-G5/V-G4 can fire — V-G1/V-G2 require `_wfkind=1`, which is not compiled (§0). Kept as the pre-registration and as the frozen-1014 control. |
| `s_p_leaves()` | the HARD stale-decode check on the published `wd*` cells |
| `s_secondary()` | the §3b ladder, withheld unless `_wdctlok=1`/`_wdseen=1` |
| `at_records()` / `s_at()` / `s_at_cross()` | **the rung's verdict cell** — the AT/PAR leg (§3c): V-AT0..V-AT4 and the §3d two-halves cross-reading |
| `wf_records()` / `wd_records()` | each bound to **the recorder's own single write order** (the 1014 rule) |
| `self_test()` | the synthetic table (34 assertions, §1.3) |
| `main()` | returns 0 for any capture that parses, exactly as 1013/1014 do |

Constants read straight from the design, not invented: `TAIL_MIN_N=0x400`, `STORM_MIN_ROWS=3`,
`STORM_MIN_ORD=0x4000`, `STORM_VA=0x000E6000`, `STORM_FSR=0x0000080F`, `TTBR0_EXPECT=0x81A86000`,
`TTBCR_EXPECT_N=2`, `PT_WINDOW_LO/HI = 0x80000000 / 0xFFFEFFFF`.

### 1.2 The reader refuses to conclude when it has nothing to conclude from

The design's §3a null-control paragraph is the whole point of that table, so it is implemented as a
**gate that runs before every conclusion**:

```python
wf_vacuous = (not wf_storm) or any(r["wfseen"] == 0 or r["wfkind"] != 1 for r in wf_storm)
```

`V-G5` is tested first when there is no tail at all; `V-G3` next; only then `V-G4`/`V-G1`/`V-G2`.
**On a real 1015 arm this gate is closed by construction** — the widening is not compiled, so every
`_wfkind` is `2` and the cell is V-G3 (§0). That is why the AT leg, which has its own withhold rule,
carries the rung.

The P-ladder additionally returns `P-CTL` (and decides nothing) unless the **last storm row** carries
`_wdctlok=1` *and* `_wdseen=1`; it *decides* H-P only in the V-G2 world, which an entry-only arm
cannot reach — so `s_secondary()` is called with `decides_hp=False` on every real 1015 capture and the
ladder is **measured and reported, never adjudicated**. That is deliberate, not a gap: the ladder's
cells are the evidence for a LATER rung, and the reader must not mint a second verdict from a run
whose verdict is the AT cell.

### 1.3 `--self-test` — the run, verbatim

```
$ python3 tools/check_1015_capture.py --self-test \
      out/stage90/captures/recovered-unknown-arm-20261015-092217-last_kmsg.txt
  [ok] V-G1 storm stopped + _wfkind=1 => CONFIRMED: got V-G1, want V-G1 - stopped=True
  [ok] V-G2 storm persists at pcn>=0x4000 + _wfkind=1 => REFUTED: got V-G2, want V-G2
  [ok] V-G3 _wfseen=0 with a persisting storm => VACUOUS, NOT refuted: got V-G3, want V-G3
  [ok] V-G3 _wfkind=0 (flush refused) => VACUOUS: got V-G3, want V-G3
  [ok] V-G3 _wfkind=2 (ASID arm, poke never widened) => VACUOUS: got V-G3, want V-G3
  [ok] V-G4 storm gone, another FS=15 VA at high pcn => SYMPTOM_MOVED: got V-G4, want V-G4
  [ok] V-G5 no tail rows => MEASUREMENT LOST: got V-G5, want V-G5
  [ok] P-D2 (V-G2 + wddacr&3 != 1) => DACR_NOT_CLIENT: got P-D2, want P-D2 - primary=V-G2
  [ok] P-X1 (V-G2 + every §3b clause killed) => INPUTS_ALL_CORRECT: got P-X1, want P-X1
  [ok] P-CTL (_wdctlok=0) => NO_PERM_WITNESS: got P-CTL, want P-CTL
  [ok] P-D1 (_wddom != 0) => L1_DOMAIN_NONZERO: got P-D1, want P-D1
  [ok] P-L3 (_wdsmall=0) => NOT_A_SMALL_PAGE: got P-L3, want P-L3
  [ok] P-T2 (the witness's OWN `_wdva` is at/above the N=2 split 0x40000000) => ROUTED_TO_TTBR1: got P-T2, want P-T2
  [ok] P-L1 (leaf not user-writable) => LEAF_NOT_USER_WRITABLE: got P-L1, want P-L1
  [ok] HARD: a published _wdap that is not the decode of _wdleaf is HARD: got yes, want yes ...
  [ok] V-AT1 (user view faults, priv view succeeds, walk says user-RW/GLOBAL/dom0) => H-G at the hardware: got V-AT1, want V-AT1 - primary=V-G3
  [ok] V-AT2 (both views allow, PA == the walked leaf's own, store still faults) => PROBE DEFECT: got V-AT2, want V-AT2
  [ok] V-AT3 (both views allow, PA != the walked leaf's) => WRONG TRANSLATION: got V-AT3, want V-AT3
  [ok] V-AT4 (user AND priv views both fault) => BOTH VIEWS FAULT, not V-AT1: got V-AT4, want V-AT4
  [ok] V-AT0 (_wdatok=0 => the AT leg was void) => WITHHELD, NOT read as V-AT1: got V-AT0, want V-AT0
  [ok] V-AT0 (an AT row exists but none at the tail) => WITHHELD: got V-AT0, want V-AT0
  [ok] V-AT0 (_wdatparpr carries the CONTROL VA's PA - one key, two definitions) => WITHHELD: got V-AT0, want V-AT0
  [ok] V-AT0 (no `_wdctlat` published - the T-CTL control is unavailable) => WITHHELD, NOT read off `_wdatok`'s one bit: got V-AT0, want V-AT0
  [ok] V-AT0 (T-CTL FAILED: the control VA's AT faults) => WITHHELD, NOT V-AT1: got V-AT0, want V-AT0
  [ok] V-AT0 (user view faults but the walk does NOT say user-RW: hardware and walk agree) => WITHHELD, NOT V-AT1: got V-AT0, want V-AT0
  [ok] no AT keys at all => NO AT CELL (None), not a withhold: got None, want None
  [ok] §3d: V-AT1 + V-G1 => H-G CONFIRMED, CONSISTENT (the strongest cell): got yes, want yes - primary=V-G1 at=V-AT1
  [ok] §3d: V-AT1 + V-G2 => CONFIRMED-TRANSLATION, UNFIXED-BY-ONE-FLUSH (not 'H-G REFUTED'): got yes, want yes - primary=V-G2 at=V-AT1
  [ok] §3d: V-AT2 + V-G1 => CONTRADICTION (the stop was not caused by this translation): got yes, want yes - primary=V-G1 at=V-AT2
  [ok] §3d: V-AT2 + V-G2 => H-G REFUTED FROM BOTH SIDES (the only clean refutation): got yes, want yes - primary=V-G2 at=V-AT2
  [ok] §3d: V-AT0 + any => NO VERDICT (the behaviour column cannot carry the rung alone): got yes, want yes - primary=V-G2 at=V-AT0
  [ok] an AT row with no `_wdsctlr` still reads (the SCTLR line is a plumbing gap, not a gate): got V-AT1, want V-AT1
  [ok] frozen 1014 capture carries ZERO `wd*` keys (the control is a control): got yes, want yes - wd*=x0 leafng=x0
  [ok] the reader REFUSES the frozen 1014 capture for want of wd* evidence (P-CTL): got P-CTL, want P-CTL - it measures the storm fine (x9 storm tail rows)

  == SELF-TEST ==
  34 case(s), 0 failed
  SELF-TEST: PASS
```

rc=0. The self-test **fails** (nonzero rc) if the frozen 1014 capture ever gains a `wd*` key or if the
cell the table is asserted on changes — that is the point of pointing it at a real capture: the
synthetic cases alone would be satisfied by a reader that agrees with itself.

Note the shape of the AT cases 12a–12o: **six** of them are **V-AT0 withholds**, in six different
spellings (no tail AT row; `_wdatok=0`; the `_wdatparpr` control-PA collision; **no `_wdctlat`
published**, so the T-CTL control is unavailable; **T-CTL failed** — the control VA's AT itself
faults; and a user view that faults while the walk does NOT say user-RW), and one asserts that a
capture with **no AT keys at all** reads `None` — "no AT cell" — rather than V-AT0. That distinction is
the whole withhold discipline: V-AT0 is *the leg ran and was void*, `None` is *the leg was never
armed*. Collapsing them would let a pre-AT capture (such as the frozen 1014 one) be read as a broken
instrument.

### 1.4 The AT/PAR leg (`[1015-AT]`) — the hardware's own answer

Every other cell in this reader is a **host-side read**. `_wd*` walks the page tables from the abort
context; `_wf*` reads the pmap's flush decision; the storm is read off the abort ladder. None can see
the translation the CPU is *actually using*. `AT`/`PAR` can: it asks the MMU to walk the VA with its
current TLB state and return a PAR, and **PAR bit0 = 1 means the MMU itself faults that access**;
bit0 = 0 means the translation succeeded and `PA = PAR & 0xFFFFF000`.

The table implemented is **design §3c** (five cells, keyed on **both** PARs **of the STORM VA**, with
the W-SYNC control VA's AT on its own key `_wdctlat` as the T-CTL null control), with the §3d stitch as
the cross-reading.

| cell | `_wdatpar` (user WRITE) | `_wdatparpr` (priv READ) | reading |
|---|---|---|---|
| **V-AT0** | — | — | **AT_INSTRUMENT_BROKEN — WITHHOLD.** `_wdatok=0`, or no AT row at a tail ordinal, or the PAR keys missing, or `_wdctlat` missing / failing T-CTL, or the `_wdatparpr` collision below. |
| **V-AT1** | bit0 = 1 (FAULTS) | bit0 = 0 (allows) | **H-G CONFIRMED AT THE HARDWARE** — a user-denied but privileged-allowed translation of a leaf the tables call user-RW. |
| **V-AT2** | bit0 = 0, PA = `0x841e9000` | bit0 = 0 | **TRANSLATION CORRECT YET THE STORE FAULTED — probe defect, not H-G.** |
| **V-AT3** | bit0 = 0, PA ≠ `0x841e9000` | bit0 = 0 | **WRONG TRANSLATION IN THE TLB** — same conclusion as V-AT1, from the PA side. |
| **V-AT4** | bit0 = 1 (FAULTS) | bit0 = 1 (FAULTS) | **BOTH VIEWS FAULT** — the entry is not merely user-denied; a domain/invalid/SCTLR mechanism is in play. Re-rank toward perm-static cause #2. |

This **replaced** an earlier five-row draft I had implemented from the lead's prose message (V-AT4 =
"leg void", no V-AT0, single-PAR conditions). The design's §3c is authoritative and differs on exactly
those points; the reader now matches it.

**Six withholds, each with its own printed reason** (a withhold that does not say why is a status
whose producer did not deliver one): `_wdatok != 1`; an AT row exists but none at `_pcn >= 0x400`; the
PAR keys absent; **`_wdctlat` absent**; **T-CTL failed on `_wdctlat`**; and — the defensive carry-over —
**the `_wdatparpr` collision**.

**T-CTL is checked on `_wdctlat`, not on `_wdatparpr` (ruling 2026-10-15).** The control VA's AT is the
leg's NULL CONTROL: without it a faulting user view cannot be distinguished from a recorder that wrote
the wrong VA, used the wrong register, or skipped its ISB. The recorder publishes it **raw** on its own
key, and folds its success into `_wdatok`; the reader checks `_wdctlat` **directly** as well, because a
gate published as a single bit is not auditable and a status is a verdict only if its producer
delivered one ([[mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one]]). Self-tests 12g-bis
(absent) and 12g-ter (failed) pin both spellings.

**The `_wdatparpr` collision — reported, ruled on, now a defensive carry-over.**
`docs/1015-design.md` §3c's first draft named `_wdatparpr` *twice with two different meanings*: the
table's "priv. read" column (the storm VA's privileged view) **and** the paragraph above it (the W-SYNC
**control** VA's AT) — one key, two definitions ([[mi4-one-value-two-definitions]]). The lead's ruling
gave the control its own key `_wdctlat`; **the design §3c and this reader now agree on that name.** The
reader still detects the OLD shape **by value**: if a storm row's `_wdatparpr` names the control VA's PA
(`0x80080000`) it is carrying the control reading, so the priv-read discriminator that separates V-AT1
from V-AT4 is unavailable — V-AT0 withhold, the collision named, the recorder told to publish
`_wdctlat`. It costs one comparison and it refuses a silently-unfixed recorder. Self-test 12g pins it.

**The nG key spelling.** The clause as handed over is `wdng==0`; the published keyspace's key for the
raw nG bit is `xnu_live_abt_leafng` (design §2b gives it a name deliberately *not* prefixed `wd`). The
reader reads `_leafng` and documents it at the site rather than inventing a `_wdng` key.

**A conservative half of V-AT1, folded into V-AT0.** The design's V-AT1 text states the precondition
itself ("even though the walked leaf is `ap=0b11`, `apx=0`, `domain=0`, `nG=0`"), so it is checked
rather than assumed: if the user view faults *and* the priv view succeeds *but* the walk does **not**
clear every clause, the hardware and the walk may simply agree on a genuine user-AP denial — and
reading H-G out of that would be the one-value-two-definitions error. That case is **V-AT0**, failing
clause named. Self-test 12h pins it.

**No AT keys at all ⇒ no AT cell (None), never a withhold.** A capture whose build never armed the leg
is not evidence of anything — the same discipline as the P ladder on a pre-1015 capture. Self-test 12i
pins it; the frozen 1014 capture reads `1015 AT/PAR CELL: n/a`, and the build script FAILS if that
ever becomes a cell.

**`_wdsctlr` is reported, not gated.** The design flags it as a *plumbing gap* (`entry_stubs.c` does
not emit it yet), so its absence must not withhold a verdict: the reader prints SCTLR.M when present
and notes that M=0 would void every PAR reading, but does not make it a condition. Self-test 12o pins
that an AT row with no `_wdsctlr` still reads.

**`s_at_cross()` — the §3d stitch**, in the design's own order:

| V-AT | V-G | printed as |
|---|---|---|
| V-AT1 / V-AT3 | V-G1 | **H-G CONFIRMED, CONSISTENT** — the strongest cell in the rung |
| V-AT1 / V-AT3 | V-G2 | **CONFIRMED-TRANSLATION, UNFIXED-BY-ONE-FLUSH** — do **not** read V-G2's "H-G REFUTED" alone |
| V-AT2 | V-G1 | **CONTRADICTION** — the stop was not caused by this translation |
| V-AT2 | V-G2 | **H-G REFUTED FROM BOTH SIDES** — the only clean refutation in the rung |
| V-AT0 | any | **NO VERDICT** — the behaviour column cannot carry the rung alone |

All five rows are pinned by self-test cases 12j–12n.

### 1.5 The frozen 1014 capture read by the 1015 reader (the pre-registration, by value)

```
1015 AT/PAR CELL (THE VERDICT - the CPU's own translation): n/a
1015 PASSIVE CELL (the permission ladder): P-CTL
1015 INHERITED PRIMARY CELL (the REMOVED active half - V-G3 by construction): V-G3
1014 FLUSH CELL (inherited): F2
HARD ANOMALIES: 0
```

`V-G3`, **not** `V-G2`. The frozen capture's storm rows carry `_wfkind=2` (1014's ASID arm — the poke
was never widened), and the reader must refuse to turn that into "a full flush ran and it still
faults". This is exactly the sentence §3a's null-control paragraph forbids, and it is the failure a
"does the storm persist?" reader would have made. Note the reader still *measures* the storm in that
capture (9 tail rows, 5 at `_pcn >= 0x4000`) — the withholding is about the verdict, not the
observation. `scripts/build_1015.sh` greps this exact label and value as a build-level control.

`1015 AT/PAR CELL: n/a` is the other half of the withhold discipline, and the build checks it too: a
pre-AT capture has **no AT keys at all**, which is `None` ("the leg was never armed"), *not* V-AT0
("the leg ran and was void"). Printing V-AT0 there would manufacture a broken-instrument reading out
of a missing keyspace.

`1015 PASSIVE CELL: P-CTL` is also correct: that capture has zero `wd*` rows (pre-1015 arm), so no P
cell may be read; `P-CTL` is the design's own cell for "no permission witness". Its secondary effect
is that the reader is *usable as a control* on any pre-1015 log.

**The consequence of the entry-only decision, stated once more where it bites:** on a REAL 1015
capture the only line that will move is the first one — `n/a` becomes V-AT1 (or V-AT2/3/4/V-AT0) — and
the inherited primary cell stays `V-G3` **because nothing was widened**, which is not a failure of the
rung but its design. A reader that required V-G1/V-G2 to call the press meaningful would call every
1015 press void.

---

## 2. Ambiguities in the design's §3 tables, and the reading chosen

The design was followed where it is unambiguous. Four places admit more than one reading; each is
resolved below **in the direction that withholds or reports**, never in the direction that concludes.

### 2.1 `aborts()` pairs FORWARD (a recorder defect, not a design ambiguity)

The recorder publishes an abort's ordinal (`_pcn`) and *then* that abort's own state
(`_pcfar`/`_fsr`/`_pc`). A "running value at the `_pcn` line" read therefore returns the *previous*
abort's `far`, which silently merges two aborts. Fixed by opening a record at its `_pcn` and filling
each field from its **first occurrence after** that line. This was caught by the V-G4 self-test (the
dyld abort was under-counted and the cell came out `V-G2`); it is recorded here because it is a
*generic* 1014/1015 recorder property, not a 1015-only one.

### 2.2 V-G3's two spellings — the UNION is taken

§3a's table spells the vacuous condition `_wfseen = 0` **or** `_wfkind = 0`; §3a's null-control
paragraph spells it `_wfkind != 1`. The reader takes the **union** — it withholds if *either* is true —
which is strictly the conservative direction: it refuses a V-G2 in every case that either spelling
refuses, and adds only cases (`_wfkind` outside `{0,1}`) where a reader could not honestly certify
either way. The three self-test cases (§1.3) pin all three spellings separately.

### 2.3 §3b's row order makes P-T2 unreachable for a *correct* capture

The table lists `P-T1` (`(_wdttbcr & 7) != 2`) **before** `P-T2` (`(_wdttbcr&7)==2 && _wdva >=
_wdsplit`). For a truthful capture the storm's `far` is `0xe6000`, far below the `N=2` split
`0x40000000`, so if `TTBCR.N` really is 2 the P-T1 clause is false and P-T2 is false too — **P-T2 can
only fire if the witness's own `_wdva` does not match the abort's `far`** (i.e. a pairing defect
between the witness row and the abort it is published at). Chosen reading: implement the table in the
design's own order, and let P-T2 mean what it says — *the VA the witness read is routed to TTBR1* —
which makes it a real (if narrow) diagnostic. The self-test therefore constructs its P-T2 case with
`_wdva=0xC0001000`, deliberately not the storm VA. A reader that wanted P-T2 to be reachable for the
storm VA would have to invent a different meaning for `_wdva`; that is not this reader.

### 2.4 `P-D3` / `P-T1..P-T3` partly eliminated / V-G5 needs no `wd*` row

* `P-D3` (`DACR_CLIENT`) is listed as a cell but is, by construction, the *negation of the branch
  condition* of the row below it — it is recorded as "H-P's domain form KILLED" and is **not** emitted
  as a verdict cell. The reader folds it into the else-chain (a cell is emitted only where the
  design's condition *fires*); the `P-X1` case in the self-test is the observable proof that D3 was
  taken (all clauses killed ⇒ `P-X1`).
* `P-T3` masks TTBR0 with `2^(14-n)` per §0.2, but the design's table writes the constant
  `0x81a86000`. For the measured `N=2` the two agree (`0x4000`-granular, but TTBR0's low 14 bits are
  0 in the measured value), so the reader uses the **constant** `TTBR0_EXPECT` and reports the masked
  live value beside it, so a divergence at another `N` is visible rather than silently re-based.
* V-G5 is evaluated on the **abort ladder alone** (`_pcn` rows), never on the presence of a `wd*`/`wf*`
  row: a run that never reached the tail has no `wd*` row either, and "no witness" must not be
  reported as "measurement lost" when the reason is a different one.

### 2.5 The storm is defined on the ABORT's own `far` — never on a witness row

`classify_storm()` keys on `_pcn`/`_pcfar`. The 1014 capture's tail is **not** uniform: after the nine
storm rows come two rows at `far` ≈ `0x3a0xxxxx` (dyld's own image, `pc` ≈ `0x384xxxxx`). A reader
that defined the storm by "a `wf*` row exists nearby" would have counted those. Consequence: V-G4
(`other_fs15`) is computed from `fsr == 0x80f` on non-storm tail rows, which is what makes the V-G4
self-test case meaningful.

---

## 3. The build script — `scripts/build_1015.sh`

Modeled on `scripts/build_1014_v1.sh` (structure, `EXPORTS+=` block near `:822`, arm-hash stamping,
park/verify steps all inherited verbatim). Changes:

1. **Switch set.** `EXPORTS` gains **one** key — `STAGE90_XNU_PERM_WITNESS=1` — beside the inherited
   `STAGE90_XNU_FLUSH_WITNESS=1`; the 1014 switch set is inherited **verbatim** (nothing removed). The
   key is threaded through `build_entry.sh`'s record + refusal path and named in
   `scripts/preflight_boot_check.sh`'s `ENTRY_CFG_KEYS`, or the build refuses. **No
   `STAGE90_XNU_USER_FULL_FLUSH` anywhere** — not in `EXTRA`, not exported, not pinned (§0), and the
   freshly written record is asserted **not** to carry it.
2. **Stale-arm refusal** naming `armed-d13-29ec9e48` (1014's arm) and `armed-d13-9bf25f8f` (1013b):
   if the entry bin does not move, the 1015 edits did not reach the linked image.
3. **The pool is 1014's, UNTOUCHED — asserted as a purity claim, not a widening one.** The build
   still requires `tools/patch_d13_flushwitness.py --check` to PASS (the 1014 payload is the staged
   text), then reads the object and requires: `x0` `USERFULLFLUSH` markers, `TLBIASID == 2` and
   `TLBIALL == 2` — **equalities**, deliberately, because a `>= 2` lower bound is exactly what the
   removed widening would have satisfied. Then it reads the staged tree and requires `x0` 1015 pool
   markers there too. *That the removal is real is a check on both sides of the artifact.*
4. **OFF byte-identity, two ways.**
   * *pool*: the inherited 1014 OFF proof. `_wf_build()` is run with `STAGE90_XNU_FLUSH_WITNESS=0`,
     the one `-D` token is stripped from the define set (asserted to have been present, and asserted
     **absent** afterwards), the tree is `--revert`ed to Apple's bytes and re-staged, and the `.text`
     of the two objects is `cmp`ed. A gate that leaves code behind fails here. A clause also FAILS if a
     `STAGE90_XNU_USER_FULL_FLUSH` token is found in `EXTRA` — so the removed half cannot be
     reinstated without the build saying so.
   * *entry*: **`STAGE90_1015_OFF_CONTROL=1`** (off by default — it costs a second full entry build)
     runs a gated twin build with `STAGE90_XNU_PERM_WITNESS=0` and asserts the OFF image carries
     **0** `wd*`/`leafng`/`at*` literals and **0** `entry_note_abt_wd` symbols *while still carrying
     1014's 11 `wf*` literals* — so the control cannot pass by measuring two switches at once. It
     swaps `out/stage90` for a scratch and restores it under an `EXIT INT TERM` trap (the
     live-shared-state caveat is written into the block).
5. **Reader controls at the end of the build**: the frozen 1014 elf must carry zero
   `wd*`/`leafng`/`at*` literals and zero `entry_note_abt_wd`; the frozen 1014 capture read by the
   1015 reader must give the inherited cell `V-G3` **and** AT cell `n/a` (a `V-G2` there is a
   build-level FAIL — the reader would manufacture a refutation out of a run where the widened arm
   never existed; an AT cell that is not `n/a` would mean the reader is minting a verdict from a
   missing keyspace); and the reader's `--self-test` must PASS.
6. **The AT/PAR leg** (added by `b1015-entry` after this harness's first draft): the **five** key
   literals `wdatpar`/`wdatparpr`/**`wdctlat`**/`wdatok`/`wdsctlr` must be in the linked image and named
   ≥5 times in `entry_stubs.c` (a partial leg would let a PAR be read without its `_wdatok` gate, drop
   the T-CTL null control `_wdctlat` that makes the leg's silence readable, or drop the SCTLR half of
   §3c's re-rank). `wdctlat` is the 2026-10-15 ruling's split of the §3c `_wdatparpr` collision. This
   matters more than it did when the harness was first written:
   **with the active half removed the AT leg is no longer one cell among many, it is the rung's
   verdict** — so a build that silently dropped it would ship a press with no decidable reading. The
   frozen-1014 negative control and the `STAGE90_1015_OFF_CONTROL` twin both count the AT literals
   among the keys that must be absent OFF.
7. **Assertions on the entry edits**: image-wide `(b|bl)` count to `entry_note_abt_wd` == 1 (**`(b|bl)`,
   never `bl`** — a void call that ends a function is tail-called, the 1013 reader defect); the
   recorder's published expressions each contain `_wdleaf` (the single-read rule); `grep -c 'c3, c0, 0'`
   in `src/mmu.c` == 2 and in `pmap.c` == 0 (1015 **reads** DACR, never writes it); all 23 §2b key
   literals present; the six inherited recorders still present exactly once each.

---

## 4. Status / open items

* `tools/check_1015_capture.py`: complete, `--self-test` PASS (34 cases / 0 failed), frozen-capture
  control read as V-G3 / P-CTL / AT n/a. **Independently exercised**: yes (both commands above are
  real runs).
* `scripts/build_1015.sh`: complete to the extent the tree allows; `bash -n` passes. It has **not been
  run end-to-end** here — the entry objects and the pool object are other agents' lanes, and
  `external/**` was not edited from this lane. `tools/patch_d13_flushwitness.py` currently has **no**
  `USERFULLFLUSH` / `PERM_WITNESS` marker (grepped, 0 hits) — and after the entry-only decision that
  is **the correct and required state**: no 1015 pool insertion should ever appear. The pool clauses
  now assert that absence, so they are expected to PASS once the 1014 payload is staged, and to
  **refuse** the moment a second insertion lands. `src/entry/build_entry.sh` likewise names
  `STAGE90_XNU_USER_FULL_FLUSH` on **0 lines** (grepped), which clause `(c)` asserts.
* **Two clauses fixed after `b1015-entry`'s report (`docs/1015-entry-half.md` §2.6).** Both had failed
  on their own literal text, not on the code:
  * `build_1015.sh` grepped the symbol `entry_note_abt_wdctl`, which exists nowhere — `_wdctl` is a
    **key** (`xnu_live_abt_wdctl`). Now `grep -cF 'xnu_live_abt_wdctl' src/entry/entry_stubs.c`;
    verified against the tree: the key matches (2 occurrences), the symbol never could.
  * the record-case clause grepped `'STAGE90_XNU_PERM_WITNESS)      _v=$PERMWIT ;;'` with **six**
    spaces; `build_entry.sh:2293` has **five** (`PERM_WITNESS` is 12 chars where `FLUSH_WITNESS` is
    13). Now a whitespace-insensitive `grep -cE 'STAGE90_XNU_PERM_WITNESS\)[[:space:]]+_v=\$PERMWIT ;;'`,
    verified: 1 match, old spelling 0. Both are
    [[mi4-a-claim-in-a-comment-is-not-a-check]] running backwards — a check whose literal was wrong.
    The whitespace-insensitive form also means a future alignment edit cannot resurrect the defect
    while the arm it names stays correct.
* **RESOLVED by the entry-only decision:** the interface questions previously sent to `b1015-kernel`
  (does `tools/stage_d13_flushwitness.sh` stage a second insertion; does `--check`/`--revert` cover
  it; what is the image-visible observable) are **moot** — 1015 stages no pool edit, so there is no
  second insertion to stage, `--check` covers the 1014 payload alone, and the observable this build
  asserts is its **absence** (x0 markers, 1014's arm counts exactly). The doc's `docs/1015-design.md`
  §2/§4 still describe the widening; **the design doc is the lead's lane and has not been edited from
  here** — this file records the amendment, the build asserts it, and a reader of the design should
  read §2 together with §0 above.
* **RULED (2026-10-15): the `_wdatparpr` collision is split.** (1) The control VA's AT gets its OWN
  key **`xnu_live_abt_wdctlat`**; `_wdatparpr` keeps its single meaning (the STORM VA's privileged
  read). The recorder now publishes **five** AT cells (`wdatpar`/`wdatparpr`/`wdctlat`/`wdatok`/
  `wdsctlr`) and folds T-CTL's success into `_wdatok`; the design §3c, the build key list and this
  reader all name `wdctlat`, and the reader's T-CTL check is a real check on it (self-tests 12g-bis,
  12g-ter). The collision detector is kept as a defensive carry-over. **`entry_stubs.c` must publish
  `xnu_live_abt_wdctlat` — the build's (b2) clause refuses until it lands** (that file is
  `b1015-entry`'s lane). (2) The AT table I first implemented from the lead's
  prose (V-AT4 = "leg void", no V-AT0) has been replaced by the design's §3c table, where **V-AT0**
  is the withhold and **V-AT4** is "both views fault"; if any consumer was written against the prose
  spelling, it needs the same update. (3) The inherited §3a cell is now **V-G3 by construction** —
  no consumer should read it as a rung verdict; if the design's §3a ordering is ever re-used, the
  entry-only fact has to travel with it.