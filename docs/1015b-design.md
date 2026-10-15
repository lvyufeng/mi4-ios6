# 1015b — the H-G fix (candidate **(b)**) and its behaviour read-out

**Rung 1015b is the BEHAVIOUR half of 1015** — the half `docs/1015-design.md` §2 originally carried as
`STAGE90_XNU_USER_FULL_FLUSH` and that the lead removed by value on 2026-10-15 so 1015 could be a pure
READ. It is **deferred by one press, not abandoned**, and 1015's press has now returned the measurement
that makes it decidable.

**THE SWITCH: `STAGE90_XNU_GLOBAL_FIX` (the design's `STAGE90_XNU_USER_FULL_FLUSH` is an earlier name
for the same thing; this rung uses `GLOBAL_FIX`, which is the name the reader
`tools/check_1015b_capture.py` and the marker `1015b: GLOBALFIX` already carry. One value, one
spelling — `mi4-one-value-two-definitions` — and the design's name is recorded here rather than left
live in two spellings.)**

> **STATUS OF THIS DOCUMENT ON WRITE: the payload is AUTHORED, NOT BUILT.** Every number below is
> either marked **MEASURED** (taken from an artifact on disk this session, with the command in the
> text) or `# UNMEASURED — measure at first build` (it cannot exist until the first compile runs).
> This project's most-repeated defect class is a stale copied count
> ([[mi4-measurement-defects]]): a number in this file that is not marked is a bug in this file.
> **No build, no press, and no device action was taken to write it.**

---

## 0. Where 1015b sits — what 1015 measured, and why a behaviour change is now owed

**1015's press returned (`records/1015-press-read.txt`, arm `armed-d13-e51d14fa`, RESIDENT, 2026-10-15
12:43): V-AT1 — H-G CONFIRMED AT THE HARDWARE.**

| cell | MEASURED value (1015 capture) | reading |
|---|---|---|
| `xnu_live_abt_wdatpar` | `0x0000001f` (bit0 = 1) | the MMU **REFUSES** a user WRITE of the storm's VA |
| `xnu_live_abt_wdatparpr` | `0x8419d268` (bit0 = 0, PA `0x8419d000`) | the MMU **TRANSLATES** the same VA for a privileged read |
| `xnu_live_abt_wdctlat` | `0x80080268` (PA `0x80080000`) | the W-SYNC control: the AT instrument itself works |
| `xnu_live_abt_wdleaf` | `0x8417e1bb` (AP `0b11`, APX 0, PA `0x8417e000`) | the walked leaf **IS user-writable** |
| `xnu_live_abt_leafng` | `0x00000000` | **the leaf is GLOBAL (nG = 0)** — H-G's own precondition |
| `xnu_live_abt_wddacr` / `_wddom` / `_wdttbcr` | `1` / `0` / `2` | domain 0 CLIENT, N = 2 — the permission-static alternatives are dead |

The decisive pair is the first two rows: **the CPU faults a user write that the walked leaf permits, and
it lands the privileged read on a DIFFERENT physical page than the leaf the walk reads**
(`0x8419d000` vs `0x8417e000`). The translation in use is not the table's. With `nG = 0`, that
translation is a **GLOBAL** entry — and on ARMv7 `TLBIASID` (`arm_tlb_flushID_ASID`, `c8,c7,2`)
invalidates **only ASID-tagged entries**. It cannot evict it.

**So the mechanism is confirmed and the cure is indicated. 1015b applies it and asks the one question a
read cannot answer: DOES REMOVING THE GLOBAL ENTRY REMOVE THE FAULT?**

> **CITATION DIFFERENCE, RECORDED RATHER THAN SILENTLY FIXED.** 1015 §4 names the fix order
> **"(b) then (a)"**; the earlier `docs/1015-design.md` §2 draft named the switch after (a)'s
> mechanism. This rung builds **(b)**, which is what 1015 §4 asks for and what 1015b was always
> pre-registered to run. §6 below argues (b) over (a) on the measurement, not on the ordering.

---

## 1. The fix — exactly what is edited

**File: `external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.c`** (the D13 tree; staged by
`tools/stage_d13_globalfix.sh` / `tools/patch_d13_globalfix.py`, which is the single source of truth
for the edit — `external/` is a re-provisionable, gitignored checkout).

**Function `pmap_flush_tlbs`, the `pmap_is_shared` test inside `if (flush_self) {`, Apple's lines
`:1346-1349`.** MEASURED this session, the staged tree's text (the 1014 payload is already in it):

```
1343    if (flush_self) {
1344        if (pmap_asid_ncpus) {
1345            pmap_asid_validate_cpu(pmap, my_cpu);
1346            if (pmap_is_shared)
1347                arm_tlb_flushID();
1348            else
1349                arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);   /* <-- the only steady-state flush */
1350        }
1351        else
1352            arm_tlb_flushID();
1353    }
```

and with `STAGE90_XNU_GLOBAL_FIX=1`, the text the compiler reads:

```
1346            if (pmap_is_shared)                                /* APPLE'S, byte-identical */
1347                arm_tlb_flushID();                             /* APPLE'S, byte-identical */
#if defined(STAGE90_XNU_GLOBAL_FIX) && STAGE90_XNU_GLOBAL_FIX
                else if (pmap != kernel_pmap)
                    arm_tlb_flushID();                         /* THE WIDENED ARM */
#endif /* defined(STAGE90_XNU_GLOBAL_FIX) && STAGE90_XNU_GLOBAL_FIX */
1348            else                                               /* APPLE'S, byte-identical */
1349                arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);    /* APPLE'S, byte-identical */
```

**Three inserted lines, one new condition, one new call, and Apple's `if (pmap_is_shared)` test with
its full-TLBIALL arm untouched in both switch positions.** The insertion is placed BETWEEN Apple's
`:1347` and Apple's `:1348`, which is why the tool anchors on the two HALVES of the block rather than
on the four-line block itself (see §2).

### 1a. The new condition is TAUTOLOGICAL here, and it is written out anyway

`pmap_is_shared` is declared at `pmap.c:1291` as
`(pmap->pm_shared || (pmap == kernel_pmap))`. The `else` at `:1348` is reached **only when
`pmap_is_shared` is FALSE**, i.e. only when `pmap != kernel_pmap` already. So
`else if (pmap != kernel_pmap)` is a test that is TRUE at every execution of the arm it guards.

It is spelled out because **it is the hypothesis's own predicate** — "for a NON-KERNEL pmap take the
full flush" is the sentence the fix is *for*, and a reader of `pmap.c` should not have to derive it
from a negated test two lines above. The honest statement of what this switch does is therefore not
"a new decision appears in the flush" but:

> **`arm_tlb_flushID_ASID` becomes UNREACHABLE for every non-kernel pmap, while remaining in the
> text.**

That is a narrower and more falsifiable claim than "widened flush", and §4's blast-radius section and
the build's refusal set are written against it.

---

## 2. The staging tool — why a NEW tool, against the design's instruction

`docs/1015-design.md` §2b prescribes *"extending `tools/patch_d13_flushwitness.py` with a second,
independently-gated insertion"*. **This rung does the opposite and the reason is MEASURED, not a
preference.** A separate `tools/patch_d13_globalfix.py` + `tools/stage_d13_globalfix.sh` is added:

1. **THE 1014 TOOL'S `--check` WOULD FAIL ON A CORRECT 1015b TREE.** `patch_d13_flushwitness.py:333-337`
   asserts BY SUBSTRING that `"                arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);\n"` is
   present, and labels that clause *"the :1319 ASID-scoped arm"*. 1015b makes that arm conditional
   while leaving it **textually identical** — so this specific clause would in fact still pass — but
   the same `check()` also asserts `"            if (pmap_is_shared)\n                arm_tlb_flushID();\n"`
   as a **contiguous two-line block at 12-space indent**, and 1015b's insertion lands *between* those
   two lines' successors in the same expression. A tool whose clauses are anchored to the *span* the
   new edit splits is a tool that must be re-derived, not extended
   ([[mi4-mutation-anchors-are-code-spans]]).

2. **ITS `--revert` IS CALLED IN THE MIDDLE OF A BUILD.** `scripts/build_1015.sh:832` runs
   `patch_d13_flushwitness.py --revert` as part of the 1014 OFF-identity proof, and that same proof is
   inherited by `scripts/build_1015b.sh`. If 1015b's insertion were owned by that tool, the proof would
   revert **two rungs' payloads at once** and then restore both — and a failure anywhere in between
   would leave the tree staged for one rung and not the other with no reader able to say which.

3. **SEPARATION KEEPS EACH `--check`/`--revert` A ONE-EDIT OPERATION WITH ONE OWNER.** With two tools,
   `patch_d13_flushwitness.py --check` continues to answer exactly "is 1014's payload staged", which
   `scripts/build_1015b.sh` asserts as an **inherited-payload precondition**; and
   `patch_d13_globalfix.py --check` answers "is 1015b's arm staged" as the **delta**. A single tool
   answering both would have to be told which rung is being built.

**The anchors, MEASURED on the staged tree this session** (all counts by value, `str.count`):

| anchor | count | why it is the anchor |
|---|---|---|
| the four-line block `if (pmap_is_shared)`/`arm_tlb_flushID();`/`else`/`arm_tlb_flushID_ASID(...)` | **1** | the text `patch()` splits |
| `            if (pmap_is_shared)\n                arm_tlb_flushID();\n` | **1** | placed AFTER it |
| `            else\n                arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);\n` | **1** | placed BEFORE it |
| `                arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);\n` (20-space) | **1** | *deliberately NOT the anchor* — it is a substring of the block's own tail, and a prefix edit deletes the suffix ([[mi4-an-edit-on-a-prefix-deletes-the-suffix]]) |

The tool refuses unless the anchor set is unique, and it does not half-apply: `revert()` requires the
inserted form exactly once before writing, and `--check` asserts that **Apple's two halves are each
present exactly once** — i.e. that the switch ADDED an arm rather than replacing Apple's.

**A defect in this tool's own first draft, fixed and recorded because it is the rung's recurring
class:** `--anchors` asserted the contiguous four-line block was `1x` **unconditionally**, and staging
splits it — so the tool FAILED on the tree it had just correctly staged. The expected number, not the
code, was the defect ([[mi4-measurement-defects]]). It now asserts `1x` unstaged / `0x` staged, with
the polarity stated in the message.

**The two OPPOSITE-ORDER hazard, stated because the two tools edit neighbouring text:**
`stage_d13_globalfix.sh` MUST run **after** `stage_d13_flushwitness.sh` (the 1014 payload runs first
in `build_1015b.sh`'s driver list). The 1015b anchors are Apple's own lines, which the 1014 payload
leaves in place, so either order *composes* — but only one order is TESTED, and an untested order is
not a property. `patch_d13_globalfix.py` refuses on a displaced anchor rather than proceeding, which
is what makes the wrong order a build refusal instead of a silent mis-edit.

---

## 3. Where the switch rides, and the arm-identity consequence

`pmap.c` is a **KERNEL-POOL** file — `out/xnu_kernel_obj_d13/osfmk_arm_pmap.o` — and `build_entry.sh`
only **LINKS** it. So the switch has two carriers, plus one cross-lane declaration
([[mi4-one-value-two-definitions]]):

| carrier | file | who reads it |
|---|---|---|
| `XNU_KERNEL_EXTRA_DEFINES="$EXTRA"` | `scripts/build_1015b.sh` | `tools/build_xnu_arm_kernel.sh:1243-1246` → `EXTRA_DEFINES` appended to **every** pool compile (`:1596`) |
| the staged tree text | `tools/stage_d13_globalfix.sh` | the `#if` the compiler evaluates |
| `out/stage90/xnu_arm_entry-arm-extra.txt` | created empty by the build, appended by `scripts/park_1015b.sh` | the **cross-lane declaration**: `STAGE90_XNU_GLOBAL_FIX=1 role=1015b-globalfix`, written when the arm is frozen |

**WHY THE DECLARATION IS NOT `export` AND NOT IN THE RECORD (measured, not assumed).** A rung's
switches are normally declared by `export`ing them before `src/entry/build_entry.sh` runs, which
`echo`s them into `out/stage90/xnu_arm_entry-config.txt`; `scripts/preflight_boot_check.sh`'s
`ENTRY_CFG_KEYS` then checks the record. **That carrier does not exist for this switch**, and both
halves of it were measured this session. There are in fact **two** dead declaration channels, and
the second is worth naming because it looks like it works:

* **the record** — `build_entry.sh` is the only writer of `xnu_arm_entry-config.txt` and does not name
  the switch, so no `export` can put a line in the file (measurements below);
* **the environment** — `build_entry.sh` is invoked through
  `env -i PATH="$PATH" HOME="$HOME" "${EXPORTS[@]}"`, a **wiped** environment rebuilt from the
  explicit `EXPORTS` list. An `export STAGE90_XNU_GLOBAL_FIX=1` in `scripts/build_1015b.sh` reaches
  that invocation through neither channel. The build therefore **refuses if the variable is set in
  its environment at all** (a caller exporting it is declaring a switch through a channel nothing
  reads — the *one value, two definitions* class), and the `-D` token in `EXTRA` is the switch's only
  compile-time path.

The measurements:

```
grep -c 'STAGE90_XNU_GLOBAL_FIX' src/entry/build_entry.sh           -> 0
grep -c 'STAGE90_XNU_GLOBAL_FIX' scripts/preflight_boot_check.sh    -> 0
```

The record is written **by** `build_entry.sh`; a name that script never mentions cannot appear in it,
so an `export` here would be a value nobody reads back — the *declarative* carrier would be a comment.
And `preflight_boot_check.sh`'s key check is a **SET DIFFERENCE** against the record: adding a name
there that no writer produces would make that gate refuse **every** press, not just this rung's
([[mi4-self-written-record-is-not-a-constraint]]). Both files are another lane's, so the resolution is
a drop-in file in the shared output directory: the build creates it **empty at the top of the script**
and re-creates it empty on every run (a declaration left behind by a previous arm would describe the
wrong kernel — [[mi4-out-stage90-is-shared-live-state]]), asserts at the freeze point that it is still
empty, and refuses if it is not; the park appends the one line above and the press refuses unless that
line is present before arming. **The build never asserts the file is non-empty and the park never
asserts it is empty — they are exact inverses over one artifact, and each refuses the other's state.**

**WHY `EXTRA` AND NOT A `DEFINES+=` CLAUSE IN `build_xnu_arm_kernel.sh`.** The precedent is
`STAGE90_XNU_FLUSH_WITNESS`, which this rung inherits: 1014's switch rides `EXTRA_DEFINES` alone
(`build_xnu_arm_kernel.sh:805-819` also carries a `DEFINES+=` clause for it, added later). This rung
does **not** add a `DEFINES+=` clause, because that file is another lane's and a missing clause must
fail loudly rather than silently — so the build asserts the `EXTRA` token is present AND that the
compiled object carries the ON census (§4). A switch whose only carrier is `EXTRA` is *checked* by
reading the object; a switch with a `DEFINES+=` clause is *assumed* by reading the script.

**THE ARM IDENTITY MOVES, AND THAT IS THE DIFFERENCE FROM 1015.** MEASURED this session:

```
out/stage90/xnu_arm_entry.elf   LOAD 0x010000 0x80000000 0x80000000 0x570000   R E
                                LOAD 0x580000 0x80570000 0x80570000 0x09fc54   RW
pmap_flush_tlbs                 0x8002968c .. 0x800298b0   (inside the R E segment)
xnu_arm_entry.bin               6356052 B  (the R E segment's file range)
```

The pool's `.text` **is inside the `.bin` whose sha256 defines the arm** (`e51d14fa` today). So 1015b
moves `out/stage90/xnu_arm_entry.bin` **through the pool segment even if the entry objects were
untouched** — 1015's header calls that out for the *removed* active half
(`scripts/build_1015.sh:1041-1042`), and 1015b makes it live. Consequence for `scripts/park_1015b.sh`
and `tools/check_1015b_capture.py` alike: the arm **is** the entry bin's sha8, and a 1015b arm equal to
`armed-d13-e51d14fa` means **the pool edit did not reach the link**.

---

## 4. What the build REFUSES, and what it cannot yet know

`scripts/build_1015b.sh` is `scripts/build_1015.sh` with 1015's **pool-purity** block replaced by a
**pool-moved** block (the two are exact opposites) and its tail assertions moved from the `wd*` keys to
the fix's own census. The refusal set:

| # | refusal | the false green it closes |
|---|---|---|
| R1 | the staged tree carries the `1015b: GLOBALFIX` marker and `patch_d13_globalfix.py --check` PASSes | a switch exported with no text to compile |
| R2 | `R_ARM_CALL arm_tlb_flushID` inside `pmap_flush_tlbs` is **>= 2** and `R_ARM_CALL arm_tlb_flushID_ASID` **inside `pmap_flush_tlbs`** is **0** | the edit reaching `EXTRA` but not the object (the `build_entry.sh` only-LINKS trap) |
| R3 | the two `arm_tlb_flushID` calls are in **code order** at increasing offsets, both inside the `T pmap_flush_tlbs` symbol range | a widened arm emitted somewhere else — a count alone cannot tell a *second* call from a *moved* one |
| R4 | the 1014 payload is still staged (`patch_d13_flushwitness.py --check`) | 1015b's insertion displacing the inherited probe, which would make `_wfkind` unreadable and the reader's V-G3 control vacuous |
| R5 | the inherited entry keys are undisturbed: `entry_note_abt_wf`/`_wctx`/`_wleaf`/`_walk`/`_wsync`/`_cur` each **1** `(b\|bl)` call site; `entry_note_abt_wd` still **1** | 1015b disturbing the 1015 passive recorder it rides on |
| R6 | `arm-none-eabi-nm` finds `T pmap_flush_tlbs` and a STRICTLY GREATER following `T` symbol | the `awk '/next T/'` parse returning the relocation's own symbol — which is what this check returned on its FIRST measurement, reporting an 8-byte function ([[mi4-measurement-defects]]) |
| R7 | the arm moved: the build FAILS if `_d6_arm == armed-d13-e51d14fa` | landing back on 1015's PRESSED arm, i.e. the pool edit never linked |
| R7b | **the linked image's flush arm changed identity**: x0 `bl arm_tlb_flushID_ASID` and x>=2 `bl arm_tlb_flushID` inside `pmap_flush_tlbs`, with the frozen 1015 elf asserted as a NEGATIVE CONTROL (x1 ASID call) | **the object was rebuilt but a STALE ONE WAS LINKED** — the signature failure of a pool edit (`build_entry.sh` only LINKS). R7 catches it by name; R7b catches it *by value*, and the two fail independently |
| R8 | 1015's **inverted** clauses are gone: `wf_asid -eq 3` / `wf_full -eq 3` / `USERFULLFLUSH`-count-0 must NOT be present in the new script | carrying 1015's purity assertions into the rung whose whole purpose violates them — the copy-paste class |
| R9 | `out/stage90/xnu_arm_entry-arm-extra.txt` exists and is **EMPTY** at the freeze point | a stale declaration from a previous arm surviving in shared live state, i.e. an arm described by a claim it did not make |
| R10 | the record does **not** name `STAGE90_XNU_GLOBAL_FIX`, and neither does `preflight_boot_check.sh` | the cross-lane carrier being "promoted" into a file this lane does not own — which would make the press preflight refuse every arm, not just this one |

**`# UNMEASURED — measure at first build` (they cannot exist before the first compile):**

> **ONE FORMERLY-UNMEASURED ITEM IS NOW MEASURED, AND IT WAS MEASURED BY READING WHAT THE FROZEN COPY
> ACTUALLY CONTAINS.** The brief asked the build to assert the pool object CHANGED vs
> `out/stage90/frozen/armed-d13-e51d14fa/`; that directory holds the LINKED artifact set
> (`xnu_arm_entry.elf`/`.bin`), **not** a pool object, so a byte-comparison of objects is not
> available. The assertion is therefore taken one level up, on the surface the frozen copy does carry:
> **the identity of the flush call inside `pmap_flush_tlbs`.** MEASURED this session on the frozen 1015
> elf — `bl arm_tlb_flushID` @`0x800297d4`, `bl arm_tlb_flushID_ASID` @`0x80029800`, both by
> disassembly. The build requires **x0 `arm_tlb_flushID_ASID` and x>=2 `arm_tlb_flushID`** in the new
> image's function, and carries the frozen 1015 elf as a **negative control** (it must show exactly the
> x1 ASID call) so a checker that cannot fail cannot pass. This is the same invariant as the object
> census (R2/R3), taken on the other half of the only-LINKS trap — they fail independently.

* the arm sha8 of the 1015b image (and therefore `EXPECT_ARM` in `scripts/press_1015b.sh`, which is
  computed, not hardcoded);
* the linked-elf addresses of the two `bl` sites in `pmap_flush_tlbs` (1015's frozen elf has
  `bl arm_tlb_flushID` @`0x800297d4` and `bl arm_tlb_flushID_ASID` @`0x80029800`; the second becomes
  the widened arm's call, but its ADDRESS is not asserted — only the **object** census is, because
  the linked address moves with every rung);
* the `.text` size delta of `osfmk_arm_pmap.o`. **This one is deliberately NOT asserted, and the
  reason is a measurement:** the arm is a hand-written conditional, and the compiler is free to emit
  the extra `cmp` and reuse the `beq` it already had (net 0), or to emit a fresh branch pair
  (net +8 B in the object, +4 B in the linked `.text`). **A size assertion here would be a guess
  dressed as a check.** What IS asserted is R2/R3 — the relocation census, which is invariant under
  the codegen choice. The size is *printed* at build time so the first build's number is on the record.
* the ON/OFF delta of the linked elf. Stated as a claim to be measured, not a number.

**The OFF proof, and what it CANNOT prove.** 1015's OFF block (`build_1015.sh:749-868`) is inherited
verbatim and its `cmp` semantics are unchanged: it rebuilds `osfmk/arm` twice with the ONE token
stripped and `STAGE90_XNU_FLUSH_WITNESS=0`, and compares `.text` and the `pmap.c:<n>`-normalized
object. **It is extended, not replaced, for 1015b**: the OFF define set must strip BOTH
`-DSTAGE90_XNU_FLUSH_WITNESS=1` (1015's, the inherited proof) AND `-DSTAGE90_XNU_GLOBAL_FIX=1` (this
rung's), and BOTH environment variables must be 0 for the duration. A proof that strips only one token
compiles the other switch into both halves and measures the source text rather than the gate — the
exact failure 1015's own block records at `:800-802` ("differ in SIZE (45376 vs 44904)").

**AND THE HONEST LIMIT OF "OFF IS BYTE-IDENTICAL" FOR THIS SWITCH:** `arm_tlb_flushID` and
`arm_tlb_flushID_ASID` are defined in ONE SHARED source, `osfmk/arm/armv/cpufunc-v7.s:66-84`, compiled
into its own object. This switch changes **which symbol `pmap.c` calls**; it never changes either body
and it never touches that object. So the OFF proof is valid **on the `pmap.c`-born object** and says
nothing about `cpufunc-v7.o` — which is fine, because that object is not in the delta. This is stated
because "OFF is byte-identical" reads as a claim about the whole pool, and here it is a claim about one
object.

---

## 5. PRE-REGISTERED VERDICT CELLS — read by `tools/check_1015b_capture.py`, whose table is not
redefined here

**The reader exists and owns the adjudication** (added by a sibling lane, commit `745cc2b`). It reuses
1015's storm classifier **by identity** (`importlib` on `tools/check_1015_capture.py`), so there is one
definition of "the storm". Its cells:

| cell | exact condition | what it means |
|---|---|---|
| **G-CONFIRMED** ("H-G CONFIRMED") | fewer than `STORM_MIN_ROWS` (= 3) storm tail rows **AND** no storm row at `_pcn >= STORM_MIN_ORD` (= `0x4000`), with the capture structurally complete (terminal marker `No errors detected` PRESENT, the `xnu_live_abt_*` family and the `_pcn` ladder both present) | **the widened TLBIALL removed the fault.** The stale GLOBAL entry was the permission decision |
| **G-REFUTED** ("H-G REFUTED IN BEHAVIOUR") | **>= 3** storm rows in the tail, **OR** any storm row at `_pcn >= 0x4000` — a **positive** measurement, it must point at a storm ROW | the translation is **re-derived**, not stale. Go to 1015's §3b permission ladder (P-X1), then H-X (the CPU's own state: SCTLR/PRRR), **not** another table read |
| **G-WITHHOLD** | no tail at all / a truncated capture / a named missing key | **never a negative.** A log that ends is not a storm that stopped |

**The storm is identified BY VALUE, never by a literal VA** (`classify_storm` returns `storm["va"]`:
the `far` with the most tail rows). This is not a nicety — 1015's storm moved to `far=0x00027000`
(1015 capture, 9 tail rows) and a checker keyed to `0xe6000` reports a **vacuous `STOPPED`** on it,
which is exactly the measurement defect `records/1015-press-read.txt` §0 records.

### 5a. The attribution guard — a stop is not a confirmation until it is attributed

`_wfkind = 1` (the full TLBIALL) is **not** by itself evidence of the widened arm: 1014's capture
already publishes `_wfkind=1` for FULL flushes of NON-CURRENT pmaps at low ordinals
(`records/1014-press-read.txt` §4), and 1015's capture carries 7 such rows. The reader therefore
requires a `_wfkind=1` row **keyed to the storm** (`_wfva == storm_va` or `_pcfar == storm_va`). A
`G-CONFIRMED` whose verdict text carries the **caveat** that no such row exists is a stop that is
**measured but not attributed** — and it MUST NOT be read as a confirmation (§3a's V-G3 vacuity, the
other way round).

### 5b. The falsification — what kills this rung's claim

1. **By value, before any press.** `_leafng == 1` on the storm's row would mean the leaf is
   non-global, TLBIASID would evict it, and H-G dies with no behaviour change needed.
   **MEASURED 1015: `_leafng = 0x00000000` — the door is closed, H-G survives it.**
2. **By behaviour.** `G-REFUTED` kills H-G **as the cause of the fault**, even though V-AT1 confirms
   the diagnosis at the hardware. That is a real and pre-registered possibility, and it is why the
   rung is an experiment rather than a fix: V-AT1 says the CPU uses a translation that is not the leaf;
   it does **not** say the translation is *stale* rather than *re-derived wrongly* — AT cannot separate
   those, and only this press can.
3. **By structure.** `G-WITHHOLD` is the verdict when the capture cannot support either, and it is
   never read as a stop.

### 5c. The reader — a blocker that was REAL and is now RE-MEASURED AWAY

**Earlier in this rung's authoring** `tools/check_1015b_capture.py` could not emit `G-CONFIRMED`: the
committed revision (`745cc2b`) keyed the storm off the module literal `STORM_VA` (`0x000E6000`) — a VA
with zero rows in 1015's capture — and the working-tree revision was mid-edit and died with
`NameError: name 'STORM_VA' is not defined`. **That was reported rather than patched** (the file is a
sibling lane's — `745cc2b`, [[mi4-file-lanes]]) and **the report was acted on: it now RE-MEASURES
CLEAN at HEAD `2e7e657`**:

```
python3 tools/check_1015b_capture.py --self-test   -> 30 case(s), 0 failed / SELF-TEST: PASS  (rc 0)
python3 tools/check_1015b_capture.py <1014 capture> ->
    1015b GLOBAL-FIX CELL: G-REFUTED (storm_persists)
    1015 AT/PAR CELL (re-run): n/a
    1015 PASSIVE CELL (re-run): P-CTL
```

So the `NameError` is gone and the two positive/negative cells both resolve on a real capture. The
rung's tail assertion is therefore promoted from "self-test only" to **a reader negative control**:
`scripts/build_1015b.sh` runs the reader on the frozen 1014 capture and **requires `G-REFUTED`** —
proving, at build time, that the cell that must fire on a storm that persisted does, on an artifact
already on disk. (The control direction is deliberate: a build cannot manufacture a stopped storm, so
the reachable proof is the negative cell; a reader that emits `G-REFUTED` on the 1014 capture is one
that is reading the storm by value.) The adjudication hint in `scripts/press_1015b.sh` still states the
by-value storm VA to confirm before trusting a `G-CONFIRMED`.

---

## 6. Why **(b)** over **(a)** — on the measurement, not on the ordering

**(a)** = emit `L2_NG_BIT` on every user leaf — `pmap_vm_prot_to_page_flags`'s user branch
(`pmap.c:1113`, guarded `pmap != kernel_pmap`) plus the other user OR sites (`:3302`/`:3494`).
**(b)** = this rung's arm.

| | (b) `arm_tlb_flushID()` for non-kernel pmaps | (a) `L2_NG_BIT` on user leaves |
|---|---|---|
| diff size | **one arm, three lines, one condition** | an encoding change in ≥3 sites, each needing its own review |
| changes PTE encoding | **no** | **yes — every user leaf in the tree** |
| changes mapping LIFETIME semantics | no | **yes** — interacts with ASID allocation/reuse; a recycled ASID with stale non-global entries is a NEW failure class (`pmap_asid.c` would need auditing) |
| retires entries ALREADY in the TLB | **yes — it sweeps any stale global entry** | **no** — it fixes future emissions only; the live stale entry survives |
| attributable in one press | **yes — it IS the experiment 1015b pre-registers** | no; it needs its own rung and its own control |
| cost | coarser: a full TLBIALL per user pmap update, invalidating kernel-visible global translations too | keeps TLBIASID's cheapness |

**The measurement that decides it: `_leafng = 0` on the ONE row we have (the storm's), and
`grep -rn L2_NG_BIT` tree-wide returns its DEFINITION AND NOTHING ELSE** (`docs/1015-design.md:10-11`)
— so (a) would change the encoding of **every user leaf in the kernel** on the strength of **one
page's** evidence, and would still leave the entry that is causing *this* fault in the TLB. (b) is the
smaller diff, changes no encoding, is *self-attributing*, and is exactly the experiment this rung
already pre-registers — **confirmation and fix are the same code**. If (b) confirms and the per-fault
cost then measures badly, **(a) is the principled follow-on**, and 1015's `_leafng` cell is already the
instrument that would guard it.

**What (b) costs, stated rather than buried:** every non-kernel pmap flush becomes a full TLBIALL. That
is *always safe* (invalidating more can only be correct) but it is not *free*, and its two real
consequences are in §7.

---

## 7. Blast radius

**Kernel: one arm, one call target, in one function.** No PTE encoding change, no ASID semantics
change, no new failure class. `pmap_flush_tlbs`'s `kernel_pmap` panic guard, its `flush_self`
decision, its in-loop TTBR comparison, and its `pmap_asid_ncpus` arm are all asserted intact (§4 R2/R3
and the tool's `_PURITY` set).

**Two consequences that ARE real and are named here so the read-out is not read naively:**

1. **THE RUN MAY GET SLOWER, AND THE WATCHDOG IS THE OUTER NET.** `pmap_flush_tlbs` is on the storm's
   hottest path — 1015's capture publishes a `_pcn` ladder to `0x100000`, i.e. the retry loop runs
   ~10^6 times. A full TLBIALL per flush is more expensive than a TLBIASID, and the storm's own
   observed span is already ~24 s against a 29 s hardware watchdog
   (`STAGE90_HW_WATCHDOG_TIMEOUT_S 29u`, asserted in the build). **So a `G-CONFIRMED` reading MUST be
   read together with `_pcn_max` and the run sanity:** a run that stopped *because the machine stalled
   earlier* looks, in a tail, exactly like a storm that stopped. The reader's completeness guards
   (terminal marker, `_abt_*` family, `_pcn` ladder present) close most of this, and the run sanity
   cell names RESIDENT / PANICKED / UNKNOWN — but **`_pcn_max` materially below the 1014 band is a
   stall, not a stop**, and that judgement is the operator's, stated here as part of the
   pre-registration rather than discovered afterwards.
2. **Kernel-visible GLOBAL translations are invalidated too.** A full TLBIALL evicts the kernel's own
   global entries along with the user pmap's. That is correct-but-coarser; it is the same trade (a)
   avoids and (b) accepts, and it is why (b) is documented as a *preference for measurement* and (a)
   as the follow-on rather than the reverse.

**Entry objects: zero.** Neither `entry_stubs.c` nor `entry_trace.c` reads `STAGE90_XNU_GLOBAL_FIX`;
the switch has no entry-toolchain consumer, so no `STUB_DEFINES` plumbing is needed and — unlike
`STAGE90_XNU_FLUSH_WITNESS` — **no `src/entry/build_entry.sh` change is required**. The arm moves
anyway (§3), through the pool segment of the `.bin`.

**Storage / COW / filesystem: none.** The fault is a permission decision on a user mapping; the
storage path is untouched.

---

## 8. What 1015b does NOT do

* It does **not** build (a). §6.
* It does **not** retire H-X. If the storm persists, the residual is a translation the CPU re-derives
  wrongly, and the ladder is 1015's §3b then SCTLR/PRRR — a **different** instrument, not a table read.
* It does **not** fix the `_wdttbr0` / `_wwnoproof` / `_wfself` probe defects 1015's §4 lists as OWED.
  They are field-defect reports on existing keys; this rung changes none of them, and none of them is
  load-bearing for `G-CONFIRMED`/`G-REFUTED`.
* It does **not** repair `tools/check_1015b_capture.py` (§5c). Reported.

---

## 9. Artifacts this rung authors

| path | role |
|---|---|
| `tools/patch_d13_globalfix.py` | the edit, by value; `--check` / `--revert` / `--dry-run` / `--anchors` |
| `tools/stage_d13_globalfix.sh` | the stage driver (the single source of truth for the tree edit) |
| `scripts/build_1015b.sh` | 1015's switch set VERBATIM + `STAGE90_XNU_GLOBAL_FIX=1`; the refusal set of §4 |
| `scripts/park_1015b.sh` | freeze the arm; refuses a re-park and asserts the pool MOVED |
| `scripts/press_1015b.sh` | the press; refuses the 1015 arm; states the adjudication hint |
| `docs/1015b-design.md` | this file |
| `tools/check_1015b_capture.py` | **NOT this rung's** — authored by a sibling lane (`745cc2b`); see §5c |

**No build, no press, no device action, and no `git add` was performed while authoring these.**