# 1014 press-read — ADVERSARIAL VERIFICATION

Verifier: independent re-derivation from the capture and the source. READ-ONLY; no build, no press.
Capture: `out/stage90/captures/recovered-unknown-arm-20261015-092217-last_kmsg.txt`
(md5 `9ee25d2f8b8d5f4e9235c34cfe6f5964`, 1553071 B, 34379 lines by `grep`/binary split).
Record under test: `records/1014-press-read.txt` (re-read at HEAD `8c9b3a3`; note the record was REVISED
between my first read and this pass — §3/§7 changed; §3's revised text is the one adjudicated).
Tooling `check_1014_capture.py` was RUN for cross-reference (exit 0, prints `F2`); every number below was
re-derived directly from the raw KV stream (line numbers are `grep`/binary-split line numbers, the same the
checker uses).

The capture is a **linear KV FIFO** (one `key=0xvalue` per line, no record framing). Records are recovered
only by knowing each recorder's write ORDER; I grouped on the opener key of each recorder (`wfn`, `wctxn`,
`wkn`, `wwn`, `pten`, `pmpn`, `farn`/`pcn`). All 20 flush rows, 22 ctx rows, 22 walk rows, 20 apex rows,
22 pte rows and 20 pmp rows are accounted for.

---

## CLAIM 1 — F2 (flush ran under the matching ASID): **CONFIRMED, with one over-stated sub-claim**

Direct re-derivation (all 20 `wf*` rows, opener `wfn`):

| wfn | line | wfva | wfttb | wflive | self | kind | asid | seen | shared |
|---|---|---|---|---|---|---|---|---|---|
| 3,4,8,0x10,0x20,0x40,0x80 | 16258,16611,29764,29820,29966,30132,30477 | ≠0xe6000 | 0x80a04000 / 0x81a8d000 | 0x81a6e000 / 0x81a86000 | 1 | **1** | 0 | 1 | 1 |
| 0x100 … 0x40000 (9 rows) | 32905…33740 | **0x000e6000** | **0x81a86000** | **0x81a86000** | 1 | **2** | **2** | 1 | 0 |

- The 9 TAIL rows (wfn ≥ 0x400) all carry `wfkind=0x2` (ASID `arm_tlb_flushID_ASID`), `wfself=1`,
  `wfseen=1`, `wfva=0x000e6000`, `wfttb == wflive == 0x81a86000`, `wfncpus=1`, `wfasid=0x2`. **CONFIRMED** —
  see e.g. wfn=0x40000 @33740 (`wfttb=0x81a86000`, `wflive=0x81a86000`, `wfkind=0x20`, `wfasid=0x2`).
- CTX rows: 22 (`wctxn`), re-derived. `wctx=0x2` on every tail row (wctxn 0x400…0x100000).

**Pairing — PARTLY OVER-STATED. The pairing is by `pcn`-bucket, and for the LAST tail row it is NOT the
same abort.** The checker pairs a `wf*` row to the `wctx*` row with the same *most-recent-`pcn`-at-or-before
the line* (`check_1014_capture.py:198-210`, `277-283`). Re-deriving that pairing:
- wf@33023(wfn 0x400, bucket pcn 0x400) → ctx@33049 (wctxn 0x800, va 0xe6000) ✔ same VA
- … ✔ for wfn 0x800,0x1000,0x2000,0x4000,0x8000,0x10000,0x20000
- wf@**33740**(wfn 0x40000, bucket pcn 0x40000) → ctx@**34246** (wctxn 0x80000, **va 0x3a1141e8**) — a row
  whose `wctxva ≠ wfva`. The checker still prints "paired wctx=2" because it never compares the VA.

So the record's phrase "`wfasid=2` == … `wctx=2` **at the same abort ordinal**" is not what the mechanism
does. It is nonetheless TRUE **by value**: `wctx=2` on every tail row, and a genuine same-VA ctx row exists
immediately before the last wf row (`wctxn=0x40000, wctxva=0xe6000, wctx=0x2` @33679). The F2 *conclusion*
(matching ASID) survives; the claimed *pairing* does not, for the final row. This is exactly the
[[mi4-measurement-defects]] class the 1013b work named — a pairing that happens to be right.

Also, `w|fkind=2` (`arm_tlb_flushID_ASID`) does **not by itself** mean "the stale entry was evicted" — see
the record's own §1 correction (TLBIASID cannot evict a global entry; §7). The record now says F1 is
"narrowed, not eliminated"; that is honest. The §1 headline line ("The stale-TLB explanation (F1) is dead")
is struck through by the §1 CORRECTION block and by §7 — I concur with the CORRECTION.

---

## CLAIM 2 — V1 three probes read one cell: **CONFIRMED**

Re-derived per recorder (tail rows, ordinal ≥ 0x400):

- **W-APEX** (`wwn`, 20 rows; tail = 0x400…0x40000, 9 rows): every tail row
  `wwl1=0x81bb2001  wwl2pa=0x81bb2000  wwleaf=0x841e91bb  wwapx=0  wwva=0x000e6000  wwft=0x3  wwpatbl=0  wwpaok=1`
  (e.g. wwn=0x40000 @33750).
- **W-WALK** (`wkn`, 22 rows; tail 9 rows): every tail row
  `wkva=0x000e6000  wktb=0x81a8605b  wkl1=0x81bb2001  wkl2pa=0x81bb2000  wkleaf=0x841e91bb  wkleaf2=0x841e91bb
   wkstable=1  wkapx=0  wkoow=0` (e.g. wkn=0x40000 @33707).
- **A-walk** (`pten`, 22 rows; tail 9 rows): `pteva=0x000e6000  ptel1=0x81bb2001  ptel2=0x841e91bb  ptettbr=0x81a8605b`.

**Pairing key `(va,l1,l2pa)` is IDENTICAL across all three probes on all 9 tail rows.**
`wwleaf == wkleaf == ptel2 == 0x841e91bb`. `wkstable=1` (the `dsb`-separated double read agreed). **No tail
row violates the pairing.** CONFIRMED in full.

Note on probe independence: W-WALK and A-walk are NOT independent walkers — both call the one walker
`stage90_read_l2_pte` (`entry_stubs.c:2528` = A-walk; `:3091`/`:3123` = W-WALK). W-APEX is the one that is
independent (it walks from the apex's own `pm_l1_phys`, `entry_stubs.c:2953-2997`). So the record's "three
independent probes" is really **two walks through one routine plus one apex read**; the 3-way agreement is
still strong (an independent table + the live TTBR0 table), but the word "independent" overcounts.

---

## CLAIM 3 — the leaf decode: **CONFIRMED** (one digit transposed)

Independent bit-decode of `0x841e91bb` against the TREE's `arm_l2_entry_t` (`pmap.h:128-139`), field order
`nx,valid,bufferable,cacheable,ap[2],tex[3],apx,shareable,non_global,pfn[20]`:

| field | bits | value |
|---|---|---|
| nx (bit0) | 0 | **1** |
| valid (bit1) | 1 | **1** → present; `L2_SMALL_PAGE=0x2` (`pmap.h:226`) tests bit1 |
| bufferable (bit2) | 2 | 0 |
| cacheable (bit3) | 3 | 1 |
| ap (bits5:4) | 5:4 | **0b11** |
| tex (bits8:6) | 8:6 | **0b110** (=6) |
| apx (bit9) | 9 | **0** |
| shareable (bit10) | 10 | 0 |
| non_global (bit11) | 11 | **0** → GLOBAL |
| pfn | 31:12 | 0x841e9 → **PA 0x841e9000** |

- **User-WRITABLE: CORRECT.** The task's concern — "if ap is 0b01 user has NO access" — **does not apply**:
  the measured `ap` field is `0b11` (bit5 `L2_ACCESS_USER=1<<5` **and** bit4 `L2_ACCESS_PRW=0x10` both set).
  With APX/AP[2]=0, ARMv7 `AP=0b011` = **privileged RW + user RW** (full access). Independently,
  `pmap_vm_prot_to_page_flags` (`pmap.c:1103-1126`) ORs `L2_ACCESS_USER` for a non-kernel pmap (bit5=1 ✔) and
  **clears** `L2_ACCESS_APX` when `prot & VM_PROT_WRITE` (bit9=0 ✔, and `prot=0x3` carries WRITE). The
  measured leaf is exactly what a user-RW build of `prot=0x3` produces. **The V1 verdict is unchanged.**
- L1 descriptor `0x81bb2001`: bits[8:5] domain = **0**. DACR value **0x00000001** — XNU's own
  `mmu_reinitialize` writes it (`external/xnu-hd2-darwin13/xnu/osfmk/arm/locore.s:75-78`:
  `mov r4, #0x1; mcr p15,0,r4,c3,c0,0`), and the capture reads `xnu_live_dacr=0x00000001` (@3937, entry
  time, before XNU runs — so this is the entry's own value, but locore sets the same). Domain 0 = `0b01`
  = **client** → the AP bits ARE enforced (a manager domain `0b11` would bypass them). bit1=0 ⇒ coarse
  table (type 1). **Correction: the record's §3 "DACR=0x1" is RIGHT; this report's first draft said 0x3 and
  was wrong** — the conclusion (domain 0 is client, AP enforced) is unchanged.
- **Minor error in the record (§7 LINK/re-decode line 96):** it writes `bits[8:6] tex=0b011`. The measured
  tex field is **0b110**, not 0b011 (the three bits are transposed; `bit8=1,bit7=1,bit6=0`). Immaterial to
  the verdict, but it is a wrong digit in the record.

---

## CLAIM 4 — HARD #1 (`_wwnoproof=1`) is a PROBE DEFECT: **CONFIRMED** — and the REVISED cause is right

The record was revised between reads. The **current** §3 claims the key is **polarity-inverted**, not (as the
first version claimed) a post-call read hazard. Re-derived by value, the revised claim is CORRECT:

Source at HEAD (`src/entry/entry_stubs.c:2979`, and `:3056` at working tree):
```c
proof = (ttbrbase != 0u && ttbrbase == entry_ttbr_base(pm_l1_phys, ttbcr & 7u)) ? 1u : 0u;
entry_write_kv("xnu_live_abt_wwnoproof", proof); /* 1 = this table is not provably live */
```
`proof` is **1 when the two bases are EQUAL** (the table *is* provably live). It is published under a key
whose name and comment assert the OPPOSITE ("1 = not provably live"). The producer and the label disagree.

Evidence it is the label, not the value, that is wrong:
- **Apex `pm_l1_phys` base == live TTBR0 base on the tail, independently.** The apex reads its L1 slot at
  `pm_l1_phys + tte_offset` and gets `wwl1=0x81bb2001`; the live TTBR0 walk (`wktb=0x81a8605b`) reads the same
  slot and gets `wkl1=0x81bb2001`. The apex's `proof=1` requires `entry_ttbr_base(pm_l1_phys,2) ==
  entry_ttbr_base(TTBR0,2) == 0x81a86000`. Both lines agree: `pm_l1_phys` base = 0x81a86000.
- **The pmp recorder (PRE-enter `*pmap`) reads `pmppmap=0x81a86000` and `pmpttbr=0x81a8605b` on all 20 pmp
  rows** (e.g. `pmpn=0x40000` @33761: `pmppmap=0x81a86000`, `pmpttbr=0x81a8605b`). Pre == live. The apex's
  POST value base is also 0x81a86000 ⇒ **`pmap_expand_ttb` did NOT move it** ⇒ the "post-call read hazard"
  of the record's FIRST version does not fire here.
- **The candidate counter-argument — "maybe the enter's pmap IS a different pmap from the faulting thread's" —
  is REFUTED by the capture.** Four cells name one table on the tail: the faulting thread's own map L1 base
  `ptethr=0x81a86000` (`ptettbr & 0xFFFFFF00 == 0x81a86000`), the apex `pmppmap=0x81a86000`, the walk
  `wktb=0x81a8605b`, and the flush `wfttb=wflive=0x81a86000`. There is no second table in the evidence.

So the checker's `APEX_NOT_PROOF` (`check_1013_capture.py`, `noproof = [r for r in apex_tail if
r["wwnoproof"] == 1]`) is firing on rows that actually **PROVE** the apex table is the live one. **The V0
provenance gate PASSES by value on the tail rows** (`wwva==far`, `wwft=3`, `wwpatbl=0`, `wwpaok=1`), and V1
stands without the caveat.

Why the first version's "post-call read hazard" is the weaker reading: it requires `pmap_expand_ttb` to have
moved `pm_l1_phys` inside this enter, which would make the apex's `wwl1` disagree with the walk's `wkl1` — but
they are IDENTICAL (0x81bb2001) on every tail row. The observed defect is a pure sign/label inversion, which
needs no table to move.

Its consequence for the record's OWN owed-item ("publish BOTH pre and post") is now moot for the tail: pre ==
post, so publishing both would not change any cell. The real fix is to invert the published value or rename the
key. (Note: a 1015 `PERM_WITNESS` block has since been added at `entry_stubs.c:3013` and reads
`g_abtpte_wleaf_pre`; it does not change the 1014 cells.)

---

## CLAIM 5 — HARD #2 (7 `wfself=1` rows with `wfttb != wflive`) is a RULE defect: **CONFIRMED**

Exact set (re-derived): `wfself=1` **and** `wfttb&0xFFFFFF00 != wflive&0xFFFFFF00` → exactly **7** rows:

| line | wfn | kind | self | **shared** | ncpus | wfttb | wflive |
|---|---|---|---|---|---|---|---|
| 16258 | 3 | 1 | 1 | 1 | 1 | 0x80a04000 | 0x81a6e000 |
| 16611 | 4 | 1 | 1 | 1 | 1 | 0x80a04000 | 0x81a6e000 |
| 29764 | 8 | 1 | 1 | 1 | 1 | 0x81a8d000 | 0x81a86000 |
| 29820 | 0x10 | 1 | 1 | 1 | 1 | 0x81a8d000 | 0x81a86000 |
| 29966 | 0x20 | 1 | 1 | 1 | 1 | 0x81a8d000 | 0x81a86000 |
| 30132 | 0x40 | 1 | 1 | 1 | 1 | 0x81a8d000 | 0x81a86000 |
| 30477 | 0x80 | 1 | 1 | 1 | 1 | 0x81a8d000 | 0x81a86000 |

All 7 are `wfkind=1` **and `wfshared=1`**. The full-flush arm IS genuinely reached for a non-current pmap:

- Gate (`external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.c:1328`): `if ((ttb == ttbr_pmap) || pmap_is_shared)`.
  With `pmap_is_shared=1` the gate is TRUE **regardless of `ttb == ttbr_pmap`**, so `flush_self=TRUE` (`:1331`).
- Arm (`:1343-1352`): `if (flush_self) { if (pmap_asid_ncpus) { pmap_asid_validate_cpu(...); if (pmap_is_shared)
  arm_tlb_flushID();  // full  (:1347)  else arm_tlb_flushID_ASID(...);  //  (:1349) } else arm_tlb_flushID(); }`.
  shared ⇒ **full** `arm_tlb_flushID()` ⇒ `kind=1`. The fingerprint matches `pmap.c:1361-1365`'s kind triple
  exactly.

So `ttb != live` on a `wfkind=1`/`pmap_is_shared` row is the CORRECT shape (a shared/nested pmap's own table
is not the running one), and the checker's rule — `bad_bases = [.. if wfself==1 and wfttb≠wflive masked]`,
`check_1014_capture.py:378-380` — is too broad. Every `wfkind=2` row (including all 9 tail rows) has
`wfttb == wflive`. **Scoping the FAIL to `wfkind==2` is correct.** CONFIRMED.

**Error in the record:** §4 lists the 7 ordinals as "4/8/0x10/0x20/0x40/0x80/0x100". The real set is
**3/4/8/0x10/0x20/0x40/0x80** (the record drops `wfn=3` and wrongly includes `wfn=0x100` @32905, which is a
`wfkind=2` tail row with EQUAL bases and is not in the disagreeing set). The count (7) and the example row are
right; the ordinal list is shifted by one.

---

## NET

**The record's bottom line SURVIVES.** F2 holds (the tail flush ran as `arm_tlb_flushID_ASID`, `wfasid=2` ==
`wctx=2` on every tail row) and V1 holds (three reads of one cell agree at `0x841e91bb`; the leaf is present,
user-RW, PA 0x841e9000, and the CPU still takes FS=15 ~2^18+ times at a constant far/pc). Neither HARD is a
device fact: HARD #1 is a **label/sign inversion** in the probe (proven by value — apex base == live TTBR0 base
== `pmppmap` on every tail row), HARD #2 is a **rule scoping** defect (all 7 disagreeing rows are
`wfkind=1`+`pmap_is_shared=1`, the legitimate full-flush-of-a-non-current-pmap shape).

**Strongest single piece of evidence FOR the record:** on the tail, `wfttb == wflive == 0x81a86000`
(flush rows), `pmppmap=0x81a86000 == pmpttbr&0xFFFFFF00` (pmp rows), `ptethr=0x81a86000` (thread map) and
`wktb=0x81a8605b` (walk) — four independent cells name one table, and `wwleaf == wkleaf == ptel2 == 0x841e91bb`
with `wkstable=1`. The leaf is real and user-writable; the flush ran.

**Strongest single piece of evidence AGAINST the record (overclaims to correct):**
1. **§1's pairing claim is over-stated.** The `wf↔wctx` pairing is by a monotone `pcn`-bucket, not "the same
   abort ordinal" — for the last tail row the checker pairs `wf@33740 (va 0xe6000)` to `ctx@34246
   (va 0x3a1141e8)`. F2 is right by value, but not for the reason stated.
2. **§4's ordinal list is wrong** (3/4/8/0x10/0x20/0x40/0x80, not 4/8/…/0x100).
3. **§7 line 96 decodes `tex=0b011`**; it is `0b110`.
4. **"three INDEPENDENT probes"** overcounts: W-WALK and A-walk share the one walker `stage90_read_l2_pte`;
   only W-APEX is independent.
5. **"~2^20 re-faults at a constant far/pc" conflates two numbers.** `pcn=0x00100000` is the TOTAL abort count.
   The constant-far tail is `pcn 0x400 … 0x40000` (the last three published rows: 0x40000 far=0xe6000,
   0x80000 far=**0x3a1141e8**, 0x100000 far=**0x3a0138e0**, with `pc` changing to 0x38413ad0/0x3840ea3e). The
   storm's steady state holds through ordinal 0x40000; the run's LAST two samples are a different VA/pc. The
   claim should read "~2.6e5 constant-far re-faults (through ordinal 0x40000); the run ended on a different
   fault at ~pcn 0x80000".

None of these corrections changes F2 or V1. The two HARDs are probe/rule defects, as the record states.

---

## POLARITY — the `_wwnoproof` cell is a KEY/LABEL inversion, and its VALUE is uniformly correct

Adjudicating the team-lead's five questions by value. **VERDICT: CONFIRMED** — the cell is polarity-inverted
at the key/comment, published verbatim, and the checker is not aware; BUT (sharper than the claim) the cell's
*value* is a correct positive identity on every row, and on the 1014 tail it certifies V1.

**Q1 — the comparison and the publish.** `src/entry/entry_stubs.c:2989`:
```c
proof = (ttbrbase != 0u && ttbrbase == entry_ttbr_base(pm_l1_phys, ttbcr & 7u)) ? 1u : 0u;
```
where `ttbrbase = entry_ttbr_base(ttbr, ttbcr & 7u)` (`:2988`) and `ttbr` is the LIVE TTBR0 (`:2986`,
`mrc p15,0,%0,c2,c0,0`). Published **verbatim, no negation**: `:3056`
`entry_write_kv("xnu_live_abt_wwnoproof", proof); /* 1 = this table is not provably live */`. There is **no
downstream `!proof`** (grepped: the only producers are `:3056` and nothing reads it back before publishing) and
the **checker does not invert it back** — `tools/check_1013_capture.py:5158`
`noproof = [r for r in apex_tail if r["wwnoproof"] == 1]` treats `1` as `APEX_NOT_PROOF`. So `proof=1`
(equal → live) is read as "NOT live". **CONFIRMED.**

**Q2 — the tail rows.** For every tail apex row (VA 0xe6000, wwn ≥ 0x100): the pmp row's `pmpttbr=0x81a8605b`
⇒ `entry_ttbr_base(ttbr,2) = 0x81a8605b & 0xFFFFF000 = 0x81a86000`; the pmp row's `pmppmap = pm_l1_phys =
0x81a86000`; so `entry_ttbr_base(pm_l1_phys,2) = 0x81a86000`. **EQUAL ⇒ proof=1 ⇒ recorded `wwnoproof=1`** —
which means *the pmap's table IS the live table*, the OPPOSITE of the key name. By value: apex `wwl1=0x81bb2001`
equals the live-TTBR0 walk's `wkl1=0x81bb2001` ⇒ the apex's `pm_l1_phys` *is* the running TTBR0. CONFIRMED.

**Q3 — the `wwnoproof=0` rows: the bases genuinely DIFFER (NOT a `pm_l1_phys==0` artifact).** Seven rows carry
`wwnoproof=0` (wwn = 2, 3, 8, 0x10, 0x20, 0x40, 0x80). A `pm_l1_phys==0` value would have **returned early**
(`entry_stubs.c:2968-2969`) and published no `wwleaf` — but every one of these rows **has** a `wwleaf`
(0x81a8219b, 0x81a8519b, 0x81c1e3bb, 0x81c683bb, 0x81c593bb, 0x81c5b3bb, 0x81cbc3bb). So `pm_l1_phys != 0`,
the `!= 0u` conjunct did **not** force 0, and the two bases genuinely differ. Cross-check by value: at those
ordinals the live TTBR0 was `0x81a6e000` (flush `wflive=0x81a6e000` @16129/16139; `ptethr=0x81a6e000` @16188)
and the pmap's own table was the other one (flush `wfttb=0x81a8d000` @29766, kind=1) — a NESTED map, not the
running one. So `proof=0` there means "not the live table", again the positive identity. (A `0` and a `1` both
have a row, so `pm_l1_phys==0` is nowhere in play.) **The inversion is a pure label inversion: the VALUE is
correct on all 20 rows as "the pmap's L1 base == the live TTBR0 base".**

**Q4 — same pmap object? RANKED.** The alternative explanations, by this capture's value:
- **(a) polarity / key-naming defect — PRIMARY, CONFIRMED.** Proven by Q1–Q3: `proof` is 1-on-equal, key named
  "noproof", checker filters `==1`.
- **(b) post-enter re-read hazard — REFUTED for the tail.** The pmp recorder reads `*(uint32_t*)pmap` PRE-enter
  (`entry_trace.c:6055`, `pm_l1_phys`), and `entry_note_abt_wleaf`'s post-enter read agrees with it (via
  `wwl1==wkl1`); if `pmap_expand_ttb` had moved the table mid-enter, the apex's L1 read and the walk's L1 read
  could not agree — they are IDENTICAL (`0x81bb2001`) on all 11 tail rows. Pre == post ⇒ no move. (This capture
  predates `_wwpre/_wwpost/_wwmoved`; those are under `PERM_WITNESS`, OFF in the 1014 build, hence absent.)
- **(c) a genuinely different pmap — REFUTED.** The enter's pmap IS the aborted thread's: the tail rows carry
  `ptettbr=0x81a8605b` ⇒ `ptethr=0x81a86000` (the faulting thread's own map, @32958…33706), which EQUALS
  `pmppmap` and the walk's `wktb` base. `pmpprot=0x3` on every pmp row = the storm's write reaching this pmap.
So (a) ≫ (b) ≈ (c): one defect, not three.

**Q5 — does V1 get CERTIFIED? YES.** For the 1014 **tail** rows the V0 provenance gate passes by value —
table == live TTBR0 (`pmppmap == pmpttbr & 0xFFFFF000`) — *plus* `wwva==far (0xe6000)`, `wwft=3`,
`wwpatbl=0`, `wwpaok=1`. The checker's `APEX_NOT_PROOF` withholds V1–V3 on **all 11** tail rows solely because
each carries `wwnoproof=1` — i.e. the gate fires exactly on the rows that *prove* the pairing. **V1 stands.** It
holds independently of the polarity question, too: `wkleaf == wwleaf == ptel2 == 0x841e91bb` and the walk reads
through the live TTBR0 (`wktb=0x81a8605b`) — ground truth the apex gate cannot affect.

**Adversarial footnote.** The defect is real, but its *direction* is a judgement call: the producer computes
"table IS live" and the **key name + the recorder's own comment (`:2980`, "1 when the pmap's own L1 base is NOT
the base the MMU is walking with") + the checker** all assume "1 = NOT live". So the design SPEC and the code
disagree; the fix is either to invert the published value or to rename the key to `_wwproof` and flip the
checker — the record's §3 chooses the label reading ("the key's name/comment is the thing that is wrong"), which
is defensible. What is NOT in doubt is the by-value conclusion: on the storm's rows the pmap's L1 base IS the
live TTBR0 base.