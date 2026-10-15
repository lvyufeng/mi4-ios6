# 1014 — THE DECISION TREE: what to instrument, given 1013's leaf reading

**This is a DECISION TREE, not scripts.** 1013 (`armed-d13-9cf2f15e`, built, parked,
pressed; its log is not yet recovered) reads **the VALUE of the leaf the pmap writes and the
LOCATION the MMU's own walk finds it at**. 1013's own verdict table (`docs/1013-design.md` §3)
has exactly three real outcomes — **V1 / V2 / V3** — and three ways the measurement can refuse
to be a verdict. **1014 is not one rung; it is whichever ONE of three rungs 1013's reading
selects.** This document designs all three, plus the rebuild path for the refusals.

Everything below is cited to a line **read by value this session** (2026-10-15, working tree at
`HEAD` = `f73e026`). A line I did not open is not cited. A claim in a comment is not a check;
every refusal proposed below is a **build-side byte comparison**, not a comment.

---

## 0. The frontier, and the one question each branch answers

`. 1012 PRESS RESULT (`records/1012-press-read.txt`, arm `armed-d13-ffdc8ba1`).`  The storm's
tail is constant: `pc=0x2be1e1a0` (dyld), `far=0x0002e000`, `fsr=0x0000080f`
(**FS=15, level 2, WnR=1 — a USER WRITE permission fault**), retried ~2.1 M times, freezing
~24.4 s. 1012 MEASURED that `prot=0x3` (READ|WRITE) **arrives** at `pmap_enter_options`
(`_pmpprot=0x3`) and that the pmap's L1 base is the live TTBR0 base
(`_pmppmap=0x81a86000` == `_pmpttbr=0x81a8605b & 0xFFFFFF00`). Every input and every source line
is correct by value — and the store still permission-faults. 1013 reads the leaf itself.

| 1013 outcome | the question it leaves | the ONE rung |
|---|---|---|
| **V1** `LEAF_WRITABLE_STILL_FAULTS` | the leaf IS writable in the table the pmap wrote **and** the MMU walks, yet the store faults ⇒ is the fault a **stale TLB / a flush that did not reach this CPU / a mismatch between the ASID that was flushed and the ASID the CPU is translating under**? | **§2 — `STAGE90_XNU_FLUSH_WITNESS`** |
| **V2** `F1a_CONFIRMED_LEAF_DEFECT` | the pmap was handed WRITE and the leaf came out READ-ONLY ⇒ **which** writer re-RO'd it (or did the store not land)? | **§3 — `STAGE90_XNU_WRITER_WITNESS`** |
| **V3** `IDENTITY_FAULT_L2` | the apex writes one L2 table and the MMU walks another ⇒ is `pm_l1_virt` the linear map of `pm_l1_phys`, and does `pmap_pte`'s L2 pointer equal the one the LIVE TTBR0 walk resolves? | **§4 — `STAGE90_XNU_L1ID_WITNESS`** |
| **V4/V5/V6** (refusals) | the measurement itself did not produce a leaf | **§5 — rebuild 1013 with the NAMED defect fixed. This is NOT a new rung.** |

**The gate is 1013's, and it is kept.** Each rung below rides 1013's witness (the
`STAGE90_XNU_PTE_WITNESS` recorders and their call sites, `src/entry/entry_trace.c:5761` /
`:5832` / `:5842` / `:5998`) and 1013's **W-SYNC control**. If `check_1013_capture.py` returns
`WITNESS_METHOD_BROKEN` (`tools/check_1013_capture.py:5010`), **no branch is selected at all**
— the method is not certified on this device and the honest move is §5.

---

## 1. Which branch is selected (the mechanical rule)

The checker already computes `WIT_CLASS`; 1014's dispatch is a total function of it, and it is
written down here so the choice is not a judgement call after the log arrives:

```
WIT_CLASS == "WITNESS_METHOD_BROKEN"        -> §5 (rebuild; the method is uncertified)
WIT_CLASS in ("WALK_UNSTABLE_NULL",
              "APEX_OBSERVED_A_TABLE_PAGE",
              "APEX_WRONG_VA",
              "APEX_WRONG_FAULT_TYPE",
              "APEX_NOT_PROOF",
              "NO_APEX_WITNESS",
              "LOCATION_MISMATCH_NULL",
              "APEX_ONLY", "MEASUREMENT_DEFECT")  -> §5 (rebuild; name the defect)
WIT_CLASS == "LEAF_WRITABLE_STILL_FAULTS"   -> §2
WIT_CLASS == "F1a_CONFIRMED_LEAF_DEFECT"    -> §3
WIT_CLASS == "IDENTITY_FAULT_L2"            -> §4
```

(`WIT_CLASS` is assigned at `tools/check_1013_capture.py:5010`, `:5029`, `:5058`, `:5104`,
`:5121`, `:5131`, `:5168`, `:5206`, `:5224`, `:5231`, `:5242`, `:5253` — every class named
above is a string literal read at those lines this session.)

---

## 2. BRANCH V1 — `LEAF_WRITABLE_STILL_FAULTS` ⇒ `STAGE90_XNU_FLUSH_WITNESS`

### 2.1 (a) The probe sites, read BY VALUE this session

**(a1) `external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.c:1274-1328` — `pmap_flush_tlbs`.** The
function this branch is about, read whole:

```c
1274 void
1275 pmap_flush_tlbs(pmap_t  pmap, vm_map_offset_t startv, vm_map_offset_t endv)
1276 {
1280     pmap_paddr_t    ttb = pmap->pm_l1_phys;
1281     boolean_t   flush_self = FALSE;
1282     boolean_t   pmap_is_shared = (pmap->pm_shared || (pmap == kernel_pmap));
1287     if (pmap_asid_ncpus) {
1288         pmap_asid_invalidate_all_cpus(pmap);
1289         __asm__ volatile("":::"memory");
1290     }
1292     for (cpu = 0, cpu_bit = 1; cpu < real_ncpus; cpu++, cpu_bit <<= 1) {
1293         if (!cpu_datap(cpu)->cpu_running)
1294             continue;
1295         uint32_t ttbr_pmap = armreg_ttbr_read() & 0xFFFFFF00;
1297         /* Current pmap is active, flush it. */
1298         if ((ttb == ttbr_pmap) ||
1299             (pmap_is_shared)) {
1300             if (cpu == my_cpu) {
1301                 flush_self = TRUE;
1302                 continue;
1303             }
1305             /* xxx broadcast IPI to all other CPUs to flush */
1306         }
1307     }
1313     if (flush_self) {
1314         if (pmap_asid_ncpus) {
1315             pmap_asid_validate_cpu(pmap, my_cpu);
1316             if (pmap_is_shared)
1317                 arm_tlb_flushID();
1318             else
1319                 arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);
1320         }
1321         else
1322             arm_tlb_flushID();
1323     }
1325     if (__improbable((pmap == kernel_pmap) && (flush_self != TRUE))) {
1326         panic("pmap_flush_tlbs: pmap == kernel_pmap && flush_self != TRUE");
1327     }
1328 }
```

**Five things the probe must publish, and the line each is read from:** `ttb` (`:1280`),
`pmap_is_shared` (`:1282`), the live `armreg_ttbr_read() & 0xFFFFFF00` (`:1295` — **read inside
the cpu loop, at flush time, NOT at enter time**), the value of `pmap_asid_ncpus` (`:1287` /
`:1314`), and **which of the three flush arms ran** (`:1317` full `arm_tlb_flushID()` /
`:1319` `arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF)` / neither).

**(a2) The flush's own ASID vs the CPU's ASID.** The flush at `:1319` invalidates only entries
tagged with `pmap->pm_asid & 0xFF`. The translation of the faulting store is tagged with
**CONTEXTIDR**. The two are written from two different places, by value:

* `pmap.c:2520` — `arm_set_context_id(new_pmap->pm_asid & 0xFF);` (inside `pmap_switch`,
  `:2495-2530`; guarded at `:2513` by `if (current_cpu_datap()->user_pmap == new_pmap)
  goto switch_return;` — i.e. **CONTEXTIDR is NOT rewritten when the same user pmap is
  re-dispatched**).
* `pmap_asid.c:296` — `tpmap->pm_asid = new_asid;` (inside `pmap_asid_activate`,
  `:272-307`), and `pmap.c:3903` — `our_pmap->pm_asid = 0;` in `pmap_create`.
* `proc_reg.h:583` — `ARMREG_READ_INLINE(contextidr, "p15,0,%0,c13,c0,1")` — **the read macro
  for CONTEXTIDR exists in this tree and is the one the fault-time probe uses** (no new cp15
  spelling is invented).

**(a3) The two assembly primitives, by value (`armv/cpufunc-v7.s`):**

```asm
 66 ENTRY(arm_tlb_flushID_ASID)
 70 	mcr	p15, 0, r0, c8, c7, 2	@ flush I+D tlb per ASID
 72 	dsb
 73 	isb
 75 END(arm_tlb_flushID_ASID)
 77 ENTRY(arm_tlb_flushID)
 82 	mcr	p15, 0, r0, c8, c7, 0	@ flush I+D tlb all
 84 	dsb
 86 	bx	lr
 87 END(arm_tlb_flushID)
```

`MULTIPROCESSOR` is not defined in this build, so `:70`/`:82` are the spellings that run.
**TLBIASID (c8,c7,2) invalidates only entries whose ASID equals the register's** — that is the
whole mechanism of this branch.

**(a4) The by-value correction that makes this branch the likely one — see §8.** `pmap_asid_ncpus`
is **not 0 at runtime**. `pmap_asid.c:121` is a static initializer:

```c
 121 uint32_t pmap_asid_ncpus = 0;
```

and `pmap_asid_configure` increments it:

```c
148 void pmap_asid_configure(void)
156     if(!pmap_asid_disabled) {
157         if (OSIncrementAtomic(&pmap_asid_ncpus) == machine_info.max_cpus) {
158             kprintf("All ASID/asids enabled: real_ncpus: %d, pmap_asid_ncpus: %d\n", real_ncpus, pmap_asid_ncpus);
```

`pmap_init` calls it (`pmap.c:3681` — `pmap_asid_configure();`, inside
`#ifdef _ARM_ARCH_7`). **Every recovered capture says so:** line 4003 of
`out/stage90/captures/recovered-unknown-arm-20261015-020222-last_kmsg.txt` (the 1012 press
log) reads `All ASID/asids enabled: real_ncpus: 1, pmap_asid_ncpus: 1`, and 25 capture files in
that directory carry the identical line. So **the `:1314-1319` ASID arm is the arm this boot
takes**, and `arm_tlb_flushID_ASID` — not `arm_tlb_flushID` — is what fires.

### 2.2 (b) The new publish keys — `wf*` / `wctx*`, collision-proven

The 1013 keyspace is `w*`/`ww*` (W-APEX), `wk*` (W-WALK), `wc*` (W-SYNC), plus `wr*` (1012
WRSTRIP). The `abt_` key set is 93 literals
(`grep -rhoE 'xnu_live_abt_[A-Za-z0-9_]+' src/entry tools scripts | sort -u | wc -l` → `93`).
Every candidate below was grepped **tree-wide** over `*.c *.h *.py *.sh *.md`:

```
xnu_live_abt_wfn        0     xnu_live_abt_wfkind     0
xnu_live_abt_wfva       0     xnu_live_abt_wfttb      0
xnu_live_abt_wfself     0     xnu_live_abt_wfshared   0
xnu_live_abt_wfncpus    0     xnu_live_abt_wfasid     0
xnu_live_abt_wflive     0     xnu_live_abt_wfnx       0
xnu_live_abt_wctx       0     xnu_live_abt_wctxn      0
xnu_live_abt_wctxva     0     xnu_live_abt_wfmap      0
```

All zero. (Prefix probes `xnu_live_abt_wf` and `xnu_live_abt_wctx` also return 0 — no prefix
collision either. The unrelated `xnu_live_wfi_*` family is **not** under `xnu_live_abt_` and
cannot collide.)

| key | published value | read at |
|---|---|---|
| `_wfn` | the flush-witness ordinal (opens the row; ladder `n<=4 \|\| (n&(n-1))==0`) | — |
| `_wfva` | the `startv`/`endv` this flush was called for (`startv`) | `pmap.c:1275` |
| `_wfttb` | `ttb` = `pmap->pm_l1_phys` | `pmap.c:1280` |
| `_wflive` | `armreg_ttbr_read() & 0xFFFFFF00` **as read inside the loop** | `pmap.c:1295` |
| `_wfself` | `flush_self` (0 = **the flush did NOT run for this pmap**) | `pmap.c:1281/:1301/:1325` |
| `_wfshared` | `pmap_is_shared` | `pmap.c:1282` |
| `_wfncpus` | `pmap_asid_ncpus` **as a runtime value at the call** | `pmap.c:1287/:1314` |
| `_wfasid` | `pmap->pm_asid & 0xFF` — the ASID the flush would use | `pmap.c:1319` |
| `_wfkind` | 0 = no flush; **1** = `arm_tlb_flushID()` (full); **2** = `arm_tlb_flushID_ASID` | `pmap.c:1313-1322` |
| `_wfnx` | DISTINCT `(_wfself,_wfkind,_wfasid)` triples (a change-count; **never the headline**) | — |
| `_wctxn` | the CONTEXTIDR witness ordinal | — |
| `_wctx` | **CONTEXTIDR at the abort** (`mrc p15,0,rX,c13,c0,1`) & 0xFF | `proc_reg.h:583` |
| `_wctxva` | the abort's `far` this row is keyed on | — |

**`_wfkind` is the cell that decides.** `_wfkind = 2` with `_wfasid` published is the
ASID-scoped flush; `_wfkind = 1` is the full flush; `_wfkind = 0` at a tail row means the
`ttb == ttbr_pmap || pmap_is_shared` test at `:1298-1299` **failed** and no flush ran at all.

### 2.3 (c) The switch, and why the probe MUST be a source edit (not a `--wrap`)

**`STAGE90_XNU_FLUSH_WITNESS`** (default 0, spelled `defined(X) && X`). It is
**entry-toolchain + kernel-pool**, exactly like 1012's `STAGE90_XNU_WRSTRIP`:

* the **recorder** `entry_note_abt_wf(...)` lives in `src/entry/entry_stubs.c` and rides
  `STUB_DEFINES` / `STUB_DEFINES_TRACE` in `src/entry/build_entry.sh` (the `PTE_WITNESS`
  precedent — read at `src/entry/build_entry.sh:334`, `:1370`, `:1376-1384`);
* the **call sites** are inside `pmap.c`, a **kernel-pool** file
  (`out/xnu_kernel_obj_d13/osfmk_arm_pmap.o`), so the switch goes into
  `tools/build_xnu_arm_kernel.sh`'s `DEFINES` array — **precisely where `STAGE90_XNU_WRSTRIP`
  goes today** (`tools/build_xnu_arm_kernel.sh:801`:
  `[[ ${STAGE90_XNU_WRSTRIP:-0} -eq 1 ]] && DEFINES+=(-DSTAGE90_XNU_WRSTRIP=1)`), with the
  tree text inserted by a `tools/patch_d13_flushwitness.py` shaped **exactly** on
  `tools/patch_d13_wrstrip.py` (idempotent; marker `1014: FLUSHWITNESS`; `defined()`-first
  gate so **OFF is the tree's original bytes**; a wrong-whitespace anchor REFUSES rather than
  no-ops; `--dry-run` / `--check` subcommands).

**WHY A SOURCE EDIT AND NOT `--wrap=pmap_flush_tlbs`.** `--wrap` rewrites only
linker-visible **cross-object** calls; the call the storm's enter rides is **intra-object**.
Measured on the built object this session:

```
arm-none-eabi-objdump -r out/xnu_kernel_obj_d13/osfmk_arm_pmap.o | grep pmap_flush_tlbs
  -> 13 R_ARM_CALL relocations, ALL inside this one object
arm-none-eabi-objdump -dr ... | awk '/^[0-9a-f]+ <.*>:$/{fn=...} /R_ARM_CALL pmap_flush_tlbs/{print fn}' | sort | uniq -c
  -> 2 pmap_page_protect, 2 pmap_enter_options, 1 each: pmap_update_cache_attributes_locked,
     pmap_unnest, pmap_remove_range, pmap_remove, pmap_protect, pmap_nest, pmap_init,
     pmap_expand, pmap_destroy
grep -rln pmap_flush_tlbs external/xnu-hd2-darwin13/xnu/osfmk/   -> arm/pmap.c ONLY (i386/x86_64 are other ports)
```

**Eleven caller functions, thirteen call sites, every one of them in the same object as the
callee.** A `--wrap=pmap_flush_tlbs` would redirect **zero** of them — including
`pmap.c:3473` (`pmap_flush_tlbs(pmap, va, va + PAGE_SIZE);`, the flush the enter at
`enter_options_done:` rides, read at `pmap.c:3469-3473`) and `pmap.c:3168`
(`pmap_flush_tlbs(map, v, v + PAGE_SIZE);`, inside `pmap_expand`). **A `--wrap` here would be
a hook that cannot fire** ([[mi4-hooks-live-at-calls]]). This is the one place in the tree
where the project's "no new `--wrap` unless it is the only way" rule and the "instruments must
be observable" rule agree: the wrap is not merely unnecessary, it is useless.

**Keying.** The probe publishes only for the storm's VA: the recorder filters
`va == g_abtpte_curva` (1013's current-fault key, set **before** the handler —
`src/entry/entry_trace.c:5761`, asserted by line number in
`scripts/build_1013.sh:1840-1846`). `pmap_flush_tlbs` is called for `endv = 0xFFFFFFFF`
(`pmap.c:3666` in `pmap_init`) and for whole ranges; the filter is what keeps the channel from
being spent before the storm (the 998 cap, 24576, and the 1007 ladder lesson).

### 2.4 (d) Build-side refusals and assertions owed

Mirroring `scripts/build_1013.sh`'s structure (the `(a)`-source / `(b)`-linked /
`(c)`-`build_entry.sh`-plumbing / `(d)`-negative-control / `(e)`-record blocks, read at
`scripts/build_1013.sh:1810-1946`):

1. **Switch plumbing, all five sites or REFUSE** (the `PTE_WITNESS` precedent,
   `src/entry/build_entry.sh:1370-1384` + record case + hand-written `echo`):
   `ENTRY_ARM_KEYS`; the record-`case` block (`FLUSH_WITNESS) _v=$FLWIT ;;`); an **explicit
   `echo "STAGE90_XNU_FLUSH_WITNESS=$FLWIT"`** in the record writer; `STUB_DEFINES`
   (entry_stubs.c); `STUB_DEFINES_TRACE` (entry_trace.c). The record writer is the one site a
   name-keyed diff does not visit and the only site the gate reads.
2. **Value refusal:** `FLUSH_WITNESS` neither 0 nor 1 ⇒ refuse (`build_entry.sh:1345`
   precedent).
3. **Conjunction refusal:** `FLUSH_WITNESS=1` requires `ABT_FAR=1 && ABT_PC=1 && ABT_PTE=1 &&
   PMP_ENTER=1 && PTE_WITNESS=1` — the recorder keys on `g_abtpte_curva`, which is 1013's
   W-CUR cell; without `PTE_WITNESS` the key is never written and the probe measures nothing.
4. **The pool-side define must be asserted, not assumed** — the 1012 precedent
   (`tools/build_xnu_arm_kernel.sh:801`) has exactly this hazard: the switch can be absent from
   `DEFINES` while the entry half compiles, so the pool object is the tree's original bytes and
   the probe silently cannot fire. `scripts/build_1014.sh` must **REFUSE** unless
   `out/xnu_kernel_obj_d13/osfmk_arm_pmap.o` contains the new literal and the new call site.
5. **`OFF` byte-identity:** `tools/patch_d13_flushwitness.py --check` proves the inserted forms
   are **staged**; the OFF half is proved by **rebuilding with the switch 0 and `cmp`**-ing the
   pool object against the pre-patch build (the `scripts/build_1013.sh:1938-1939` fallback
   clause, verbatim).
6. **NEGATIVE CONTROL:** the frozen 1012 elf
   `out/stage90/frozen/armed-d13-ffdc8ba1/xnu_arm_entry.elf` (present — verified) must carry
   **zero** of the `wf*`/`wctx*` literals and **zero** of the new symbols. A reader that cannot
   fail certifies nothing.
7. **Exactly one call site each** (the anti-inline rule): image-wide `b|bl` count to
   `entry_note_abt_wf` == 1 and to `entry_note_abt_wctx` == 1. **`(b|bl)`, not `bl`** — 1013's
   own build bug: a void call that is the last statement of its function is **tail-called** and
   the image carries `b <sym>`, so a `bl`-only regex reads it as 0 and manufactures a false
   "the call did not reach the image" ([[mi4-linked-code-order-is-not-source-order]]; fixed for
   1013 per the memory `mi4-1013-leaf-value-witness.md`).
8. **Purity:** the probe is a `void` call that returns; `pmap_flush_tlbs`'s control flow,
   its `flush_self` value and its return are unchanged. Assert by disassembly that the flush
   instructions (`c8,c7,2` / `c8,c7,0`) are still present **and** that the number of `bl`s to
   the probe is exactly the number of probe sites inserted.

### 2.5 (e) Pre-registered verdict cells for this rung ONLY

Read on the **TAIL rows' own values** (any `_wfn >= 0x400`), never a whole-run `max()`
([[mi4-measurement-defects]]). The W-SYNC control must still be sound, else
`WITNESS_METHOD_BROKEN` and V1-F\* are **withheld**.

| # | tail-row condition | verdict | what it means |
|---|---|---|---|
| **F1** | `_wfself = 0` at every tail row for this VA | **FLUSH_NEVER_FIRED** | the test at `pmap.c:1298-1299` was FALSE: `pmap->pm_l1_phys != (armreg_ttbr_read() & 0xFFFFFF00)` **at flush time** and the pmap is not shared ⇒ **no flush ran at all**. A by-value CONTRADICTION with 1012/1013 (which measured the two bases EQUAL at the *enter*): it would mean TTBR0 changes between the enter and the flush. |
| **F2** | `_wfself = 1`, `_wfkind = 2`, and `(_wfasid != (_wctx & 0xFF))` on the **tail** pair | **ASID_MISMATCH** | the flush fired, but it was `TLBIASID(pm_asid)` while the CPU translates the faulting store under a **different** CONTEXTIDR: the invalidated entries are not the entries the store walks. This is the by-value mechanism (`pmap.c:1319` vs `pmap.c:2520`), and it is the shape 1012's records **dismissed** on a mis-read static initializer. |
| **F3** | `_wfself = 1`, `_wfkind = 2`, `_wfasid == (_wctx & 0xFF)`, and the fault persists | **ASID_CONSISTENT_STILL_FAULTS** | the correct ASID was invalidated and the store still permission-faults ⇒ **NOT a TLB staleness in the ASID sense**. Next rung must read the TLB shape itself (a section mapping at this VA, or a TTBR1 walk, or the domain/DACR). Named, not guessed. |
| **F4** | `_wfself = 1` and `_wfkind = 1` (the **full** `arm_tlb_flushID`) and the fault persists | **FULL_FLUSH_STILL_FAULTS** | the whole TLB was invalidated for this CPU and the store still faults ⇒ staleness is dead; the entry is rebuilt from the *table*, so the table the MMU walks is not the table 1013 read. **Cross-check with 1013's V3 cell** (`_wwl2pa` vs `_wkl2pa`) — if they were equal, this is a hard contradiction and the rung after must re-open the walk. |
| **F5** | `_wfn = 0` at the tail while the storm ran | **NO_FLUSH_WITNESS** | the flush recorder never fired on the storm VA: a provenance/keying failure (the flush runs outside the handler's `g_abtpte_curva` window) ⇒ **rebuild, do not conclude** (§5). |
| **F6** | `_wfncpus` differs from the `All ASID/asids enabled` line's value | **HARD (stale decode)** | the published runtime value disagrees with the boot's own print ⇒ a broken recorder (`tools/check_1013_capture.py:4939-4947` precedent). |
| — | any `_wfkind`/`_wfasid` that does **not** match the disassembled branch taken | **HARD (stale decode)** | the same class: a published decode that is not the decode of what ran. |

### 2.6 (f) Honest scope — what this rung does NOT prove

* **It cannot see the TLB.** It proves WHICH flush arm ran, for WHICH pmap, under WHICH ASID,
  with WHICH live TTBR0, and (via `_wctx`) under which CONTEXTIDR the fault was taken. It
  **cannot** prove the hardware honoured `TLBIASID`. F3 is named precisely because "the flush
  was correct and the TLB still held the entry" is a shape this rung **cannot distinguish**
  from "the entry was rebuilt from a table 1013 did not read" — that is why F4 carries an
  explicit cross-check back into 1013's V3 cell rather than a conclusion.
* **It does not observe interleaving.** `pmap_flush_tlbs` is called from 13 sites in 11
  functions; the probe names the call, not the caller. If the storm's enter rides a different
  flush site than `pmap.c:3473`, the row's `_wfva` will say so (`startv`), but attributing it to
  the enter requires the VA, not the return address.
* **It installs no fix.** As with 1013, the run is expected to freeze at the same ~24.4 s
  shape; the verdict is a successful READ (exit 0).
* **It presumes F2's premise is well-posed.** If `pmap_asid_ncpus` were in fact 0 on the press
  (it is not, per §2.1(a4) — but the press is the authority), the rung still reads correctly
  (`_wfncpus = 0`, `_wfkind = 1`) and the whole ASID line of reasoning dies by measurement
  rather than by citation. That is the design's intent.

---

## 3. BRANCH V2 — `F1a_CONFIRMED_LEAF_DEFECT` ⇒ `STAGE90_XNU_WRITER_WITNESS`

### 3.1 (a) Every site that can set APX on a leaf, read BY VALUE this session

`grep -n L2_ACCESS_APX external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.c` — the **complete** set
(eleven lines, read this session):

```
1111:        pte &= ~(L2_ACCESS_APX);    /* APX-bit, RW? */
1113:        pte |= (L2_ACCESS_APX);      /* APX-bit, R-? */
1644:    if(map==kernel_pmap&&va==IOS7LAB_COMMPAGE_ALIAS)value|=L2_ACCESS_USER|L2_ACCESS_APX|L2_NX_BIT;
2876:    assert((lab_pte & (L2_ACCESS_USER | L2_ACCESS_APX | L2_NX_BIT)) == ...
3260:            template_pte |= L2_ACCESS_USER | L2_ACCESS_APX | L2_NX_BIT;
3452:        template_pte |= L2_ACCESS_USER | L2_ACCESS_APX | L2_NX_BIT;
3611:                *ptep |= L2_ACCESS_APX;
3658:            dpte |= L2_ACCESS_APX;
4060:                *(pt_entry_t *) pte |= (L2_ACCESS_APX);
4304:                        *spte &= ~(L2_ACCESS_APX);
4306:                        *spte |= L2_ACCESS_APX;
```

Reachability on the single-threaded write-fault retry, by value:

| line | what | reachable on the retry? |
|---|---|---|
| `:1113` | `pmap_vm_prot_to_page_flags`, the **input** builder — the RO branch | **yes**, but only if `prot` reached it without `VM_PROT_WRITE`; that is 1012's refuted strip-above-the-apex |
| `:1644` | `leo_pte_build` | **DEAD** — `pmap_enter_options` dispatches to `leo_enter` only under `#if BOARD_CONFIG_QSD8250_LEO` (`pmap.c:3181-3183`), and the D13 build's only board define is `-DBOARD_CONFIG_MSM8974_CANCRO=1` (`tools/build_xnu_arm_kernel.sh:202`) |
| `:3260`, `:3452` | the **commpage alias** (`IOS7LAB_COMMPAGE_ALIAS`) | only for `kernel_pmap` at that one VA; the storm is a USER pmap at `far=0x2e000` |
| `:3611`, `:3658` | `pmap_init`'s kernel-text and const-DATA loops | **boot only** |
| `:4060` | **`pmap_page_protect`'s write-protect arm** | **YES — the prime suspect** |
| `:4304`, `:4306` | **`pmap_protect`** | **YES — the second suspect** |
| `:2876` | an `assert` comparing a lab pte to `USER\|APX\|NX` — not a writer | no |

**`pmap_page_protect` (`pmap.c:3943`), the write-protect arm, read verbatim:**

```c
4016             if (remove || pmap == kernel_pmap) {
4020 #if BOARD_CONFIG_QSD8250_LEO
4021                 leo_mapping_wire(pv_e,FALSE);
4022 #endif
4023                 *(pt_entry_t *) pte = 0;
4024                 pmap_flush_tlbs(pmap, vaddr, vaddr + PAGE_SIZE);
4026                 if (pmap->pm_stats.resident_count < 1)
4027                     panic("pmap_page_protect: resident_count");
...
4056             } else {
4057                 /*
4058                  * Write-protect.
4059                  */
4060                 *(pt_entry_t *) pte |= (L2_ACCESS_APX);
4061                 pmap_flush_tlbs(pmap, vaddr, vaddr + PAGE_SIZE);
4062             }
```

and its entry dispatch (`pmap.c:3970-3985`): `case VM_PROT_ALL: return;  /* nothing to do */`
— **so `pmap_page_protect(pa, VM_PROT_ALL)` is a no-op and only a protection that lacks READ
reaches `:4060`.** Its callers reach it from `vm_object_pmap_protect` (`vm_object.c:2901`,
`:2940`, `:2969`, `:3622`, `:3708`), `memory_object.c:231` (`VM_PROT_ALL & ~prot`), and
`pmap_disconnect` (`pmap.c:4494-4499`, `pmap_page_protect(pa, 0)` — the **remove** arm).

**`pmap_protect` (`pmap.c:4225`), the RO/RW arm, read verbatim:**

```c
4296             while (spte < epte) {
4297                 if (*spte & ARM_PTE_DESCRIPTOR_4K) {
4303                     if (prot & VM_PROT_WRITE)
4304                         *spte &= ~(L2_ACCESS_APX);
4305                     else
4306                         *spte |= L2_ACCESS_APX;
```

reachable from `vm_object_pmap_protect` (`vm_object.c:2888`, `:2912`, `:2932`, `:2960`) and
`vm_map_protect` (`vm_map.c:3678`, `:3683`).

**And the negative by-value finding — the one writer that CANNOT do it.**
`pmap_update_cache_attributes_locked` (`pmap.c:1338-1383`) does
`*cpte &= cacheability_mask; *cpte |= attributes;` (`:1376-1377`), where

```c
1336 static uint32_t cacheability_mask = ~((ARM_L2_TEX_011 << 2) | ARM_L2_4KB_TEX(ARM_L2_TEX_111));
```

with `ARM_L2_TEX_011 = 3` (`:658`), `ARM_L2_TEX_111 = 7` (`:662`),
`ARM_L2_4KB_TEX(x) = ((x & 0x7) << 6)` (`:648`) ⇒ `cacheability_mask = ~((3<<2) | (7<<6))
= ~(0xC | 0x1C0) = ~0x1CC`. **Bit 9 (0x200) is NOT in that mask**, so this writer can neither
set nor clear APX. It is reachable (`pmap_set_cache_attributes`, `:1391-1423`) and it is
**excluded by value**, not by inspection of its name.

### 3.2 (b) Keys — `wp*`, collision-proven

`xnu_live_abt_wpn`, `_wpsite`, `_wpva`, `_wpprot`, `_wppte`, `_wppn`, `_wpnx`,
`_wpthr` — all grep to **0** tree-wide (`--include=*.c *.h *.py *.sh *.md`). The whole
`xnu_live_abt_wp` prefix returns 0.

| key | value | site |
|---|---|---|
| `_wpn` | ordinal (opens the row) | — |
| `_wpsite` | **1** = `pmap_page_protect` `:4060`; **2** = `pmap_protect` `:4306`; **3** = `pmap_protect` `:4304`; **4** = `pmap_page_protect` remove `:4023`; **5** = `pmap_update_cache_attributes_locked` `:1376` | the five probes |
| `_wpva` | the VA the writer is acting on (`vaddr` at `:4004`, `sva..eva` at `:4225`) | |
| `_wpprot` | the `prot` argument (for `pmap_protect`) | `:4225` |
| `_wppte` | **the leaf word AFTER the store** — `*(pt_entry_t *)pte` at `:4060`, `*spte` at `:4304/4306` | |
| `_wppn` | the `pn` (for `pmap_page_protect`) | `:3943` |
| `_wpthr` | 1 = the storm VA is inside `[sva, eva]` for this `pmap_protect` call | `:4225` |
| `_wpnx` | distinct `(site, pte-after)` pairs (change-count; never the headline) | |

### 3.3 (c) The switch, and the same source-edit requirement

**`STAGE90_XNU_WRITER_WITNESS`** (default 0). Same plumbing as §2.3 — an entry-side recorder
(`entry_note_abt_wp`) riding `STUB_DEFINES`/`STUB_DEFINES_TRACE`, and **tree-side probes inside
`pmap.c`** inserted by `tools/patch_d13_writerwitness.py` (gated `defined()`-first, marker
`1014: WRITERWITNESS`, idempotent, refusing on a half-applied marker — the
`tools/patch_d13_wrstrip.py` shape verbatim). `pmap.c` is a kernel-pool file, so the define
goes to `tools/build_xnu_arm_kernel.sh`'s `DEFINES` array (`:801` precedent) **and** must be
asserted present in `out/xnu_kernel_obj_d13/osfmk_arm_pmap.o`.

**Why not `--wrap`:** `pmap_page_protect` is called from `vm_object.c` (5 sites) and
`memory_object.c` (1) — those ARE cross-object and a `--wrap` would catch them — but
`pmap_disconnect` (`pmap.c:4497`) calls it **intra-object**, and `pmap_protect` is called from
`vm_object.c`/`vm_map.c` but is itself the function under test. A wrap would observe the call
but **not the leaf store**, which is the cell that matters. The store is inside the function.
Source edit.

### 3.4 (d) Build-side refusals

As §2.4 items 1-3, 5-8, with:
* the **conjunction** `WRITER_WITNESS=1` requires `ABT_FAR && ABT_PC && ABT_PTE && PMP_ENTER &&
  PTE_WITNESS` (the storm-VA key);
* the **reachability refusal**: assert `tools/build_xnu_arm_kernel.sh` does **not** define
  `BOARD_CONFIG_QSD8250_LEO` (`grep -c 'BOARD_CONFIG_QSD8250_LEO' ...` → the tree's own
  `pmap.c:3181` gate is a `#if` on a macro that must be **absent**), so `:1644` is dead and the
  probe's absence there is correct rather than an oversight;
* a **frozen-elF negative control** as §2.4 item 6.

### 3.5 (e) Pre-registered verdict cells

| # | tail-row condition | verdict |
|---|---|---|
| **W1** | `_wpsite = 1` fires on the storm VA **after** the enter | **SECOND_WRITER_PAGE_PROTECT** — `pmap_page_protect`'s write-protect arm (`:4060`) set APX after the enter. The upstream caller is then named by `_wppn`: trace `pn` back through `vm_object_pmap_protect`. |
| **W2** | `_wpsite ∈ {2,3}` fires on the storm VA | **SECOND_WRITER_PROTECT** — `pmap_protect` (`:4304/4306`) re-RO'd the range; `_wpthr = 1` proves the storm VA was in `[sva, eva)`. |
| **W3** | `_wpsite = 4` fires on the storm VA | **MAPPING_REMOVED** — the leaf was *cleared* (`:4023`), not RO'd. A different failure than 1013's V2 describes; report it as such. |
| **W4** | `_wpsite = 5` fires | **CACHE_ATTRIBUTES_WRITER** — and by §3.1 this **cannot** be an APX change (`cacheability_mask` has bit 9 clear); a firing here with an APX flip is a HARD contradiction. |
| **W5** | **no `_wp*` row at all** on the storm VA (`_wpn = 0`) | **NO_SECOND_WRITER** — no instrumented writer touched this VA after the enter ⇒ the APX bit was set **by the enter itself** (the `prot` it was handed lacked `VM_PROT_WRITE`, contradicting 1012's `_pmpprot=0x3`) **or** the store never landed. Next rung re-reads `prot` inside `pmap_enter_options` **after** `pmap_vm_prot_to_page_flags` returns, to separate "the builder made it RO" from "the store was lost". |
| **W6** | a row whose `_wppte` **does not carry APX** | **HARD** — the recorder published a write-protect event whose own after-value is writable: a stale decode. |

### 3.6 (f) Honest scope

* It names the **writer**, not the **reason**. `pmap_page_protect` firing tells you a page's
  protection was restricted; it does not tell you *why* `vm_object_pmap_protect` was called
  (pageout, copy-on-write shadow, a `memory_object` lock). That is the rung after.
* It observes only writers **in `pmap.c`**. A leaf could in principle be RO'd by a non-pmap
  path; the by-value sweep of §3.1 says there is none in the arm pmap, and this rung's W5 cell
  is the measurement that tests that sweep.
* Ordering between the enter and the writer is established by the **ordinal**, not by
  timestamps; a writer that fires between two enters of a *different* VA is filtered out by
  `_wpva`.

---

## 4. BRANCH V3 — `IDENTITY_FAULT_L2` ⇒ `STAGE90_XNU_L1ID_WITNESS`

### 4.1 (a) The identity cells, read BY VALUE this session

**The pmap's own view of its L1 table — `pmap_tte` / `pmap_pte` (`pmap.c:1774-1827`):**

```c
1774 vm_offset_t pmap_tte(pmap_t pmap, vm_offset_t virt)
1776     uint32_t tte_offset_begin;
1777     tte_offset_begin = pmap->pm_l1_virt;
1778     if ((tte_offset_begin + L1_SIZE) < addr_to_tte(pmap->pm_l1_virt, virt))
1779         panic("Translation table entry extends past L1 size (base: 0x%08X)", tte_offset_begin);
1780     return addr_to_tte(pmap->pm_l1_virt, virt);
...
1817     pte = L1_PTE_ADDR(tte);
1824     ptep = (uint32_t *) ((phys_to_virt(pte) + pte_offset(virt)));
```

**The pair that can diverge — `pmap_expand_ttb` (`pmap.c:3078-3086`), read verbatim:**

```c
3079            pmap_deallocate_l1(map);
3082            /*
3083             * Set the new TTB base.
3084             */
3084            map->pm_l1_virt = phys_to_virt(pages->phys_page << PAGE_SHIFT);
3085            map->pm_l1_phys = pages->phys_page << PAGE_SHIFT;
3086            map->pm_l1_size = expansion_size;
```

and the follow-on context switch (`:3094-3095`):
```c
3094            if (map == current_cpu_datap()->user_pmap) {
3095                arm_context_switch(map->pm_l1_phys);
```

**The L2 descriptor store — `pmap_expand` (`pmap.c:3163-3168`):**

```c
3163    *tte = ((page->phys_page << PAGE_SHIFT) & L1_PTE_ADDR_MASK) | L1_TYPE_PTE;
3166     * Flush the TLBs since we updated the page tables.
3168    pmap_flush_tlbs(map, v, v + PAGE_SIZE);
```

**The translation itself — `pmap.h:192-193`, read verbatim:**

```c
192 #define virt_to_phys(p) ((unsigned int)((((unsigned long)(p)) - gVirtBase) + gPhysBase))
193 #define phys_to_virt(p) ((unsigned int)((((unsigned long)(p)) - gPhysBase) + gVirtBase))
```

and the struct layout (`pmap.h:98-100`): `pm_l1_phys` is **offset 0**, `pm_l1_virt` **offset 4**
(`#pragma pack(4)` at `:93`). On this port `gPhysBase == gVirtBase == 0x80000000`
(`arm_vm_init.c:331-333` + the boot's own identity, `entry_stubs.c:1715-1717`'s comment and
`:2297-2300`'s window reasoning), **so `phys_to_virt` is the IDENTITY** — which is exactly why
a divergence is possible and must be measured, not assumed: `pm_l1_virt` was computed by
`phys_to_virt` at `:3084` and at `pmap_create`'s `pmap.c:3924`, but the MMU walks
`pm_l1_phys` with the TTBR0 alignment rule `2^(14-N)`.

### 4.2 (b) Keys — `wl*`, collision-proven

`xnu_live_abt_wln`, `_wlva`, `_wlvirt`, `_wlphys`, `_wlvd`, `_wlsame`, `_wl2pre`, `_wl2post`,
`_wl2apxpre`, `_wl2apxpost`, `_wlnx`, `_wlvn` — **all zero** tree-wide; the `xnu_live_abt_wl`
prefix is also zero.

| key | value | read at |
|---|---|---|
| `_wln` | ordinal (opens the row) | — |
| `_wlva` | the VA | — |
| `_wlvirt` | `map->pm_l1_virt` | `pmap.c:3084` / `:3924` |
| `_wlphys` | `map->pm_l1_phys` | `pmap.c:3085` / `:3923` |
| `_wlvd` | `virt_to_phys(pm_l1_virt)` — the physical page the pmap's own VA actually resolves to | `pmap.h:192` |
| `_wlsame` | `1` iff `_wlvd == _wlphys` — **THE IDENTITY CELL** | — |
| `_wl2pre` | the L2 base `pmap_tte`/`pmap_pte` would compute for the storm VA (`L1_PTE_ADDR(*tte)`), read at the **enter** | `pmap.c:1817` |
| `_wl2post` | the L2 base the **live TTBR0 walk** resolves for the same VA (1013's `stage90_read_l2_pte`) | `entry_stubs.c:2323` |
| `_wlnx` | distinct `(_wlsame, _wl2pre, _wl2post)` triples (change-count) | — |

### 4.3 (c) The switch

**`STAGE90_XNU_L1ID_WITNESS`** (default 0). Entry-side recorder `entry_note_abt_wl` +
`entry_note_abt_wl2` (the live-walk side, which reuses the **existing** walker
`stage90_read_l2_pte`, `entry_stubs.c:2254` — **a second walker is how 1010 and 1010b each
produced a defective reading**, `entry_stubs.c:2283-2289`); tree-side probes in `pmap.c` via
`tools/patch_d13_l1idwitness.py` (`defined()`-first, marker `1014: L1IDWITNESS`). Same
plumbing set as §2.3.

### 4.4 (d) Build-side refusals

As §2.4, plus:
* assert the probe **uses `phys_to_virt`, not a hand-rolled subtraction** — the tree's own
  `pmap.h:193` spelling. A transcription is the "a stand-in can be the right size and the wrong
  value" class;
* assert the live-walk side calls `stage90_read_l2_pte` and **not** a second walker:
  `grep -c 'stage90_read_l2_pte(' src/entry/entry_stubs.c` must grow by exactly the number of
  new call sites and no new walk function may be defined.

### 4.5 (e) Pre-registered verdict cells

| # | condition | verdict |
|---|---|---|
| **D1** | `_wlsame = 0` at the tail | **L1_VIRT_NOT_THE_LINEAR_MAP** — `virt_to_phys(pm_l1_virt) != pm_l1_phys`: the pmap writes its table through a VA that resolves to a **different** physical page than the one TTBR0 walks. The apex leaf and the MMU leaf are then two different words by construction. Next rung: read the window the alias falls in (the 1010b/1010c out-of-window machinery already exists, `entry_stubs.c:2167-2169`, `:2336-2343`). |
| **D2** | `_wlsame = 1` and `_wl2pre != _wl2post` | **L2_BASE_DIVERGENCE** — the L1 tables agree but the L2 descriptor under this VA differs between the pmap's walk and the live walk: an `pmap_expand` hoist between the two reads, or a second L1 entry. |
| **D3** | `_wlsame = 1` and `_wl2pre == _wl2post` | **IDENTITY_REFUTED** — the two walks resolve the same L2 table by value, so **1013's V3 was a measurement artifact** (the two probes' bases differed without the tables differing). Report it; the branch returns to §2 or §3 by 1013's leaf cell. |
| **D4** | `_wln = 0` while a `pmap_expand` happened | **NO_L1ID_WITNESS** — the expand never ran on this pmap / the probe did not fire ⇒ rebuild. |
| **D5** | `_wlvd` computed by anything other than `phys_to_virt` | **HARD** — a transcribed translation, not the tree's. |

### 4.6 (f) Honest scope

* It proves the two **bases** and the two **L2 descriptors** agree or not. It does **not**
  prove *when* a divergence happened relative to the enter (an `pmap_expand` between the two
  reads produces the same signature as a permanent mismatch) — D2 says so explicitly.
* If D1 fires, the *reason* `pm_l1_virt` is not the linear map is not read here; only the fact
  and the addresses are.
* The live-walk side is bounded by the same `[gPhysBase, gPhysBase + mem_size)` window as
  1010c, so an out-of-window L2 is published as `_wl2post = 0` with the out-of-window flag
  rather than dereferenced — a zero here is "the walk refused", not "the table is absent".

---

## 5. The refusal path — V4 / V5 / V6 (`wwnoproof=1` / `wwn=0` / a stale 0)

**This is not a new rung. It is a rebuild of 1013 with the named defect fixed**, and it is
specified here so the branch is a decision rather than an improvisation.

| 1013 class | the named defect | the rebuild |
|---|---|---|
| **V4 `APEX_NOT_PROOF`** (`_wwnoproof = 1`) | `pm_l1_phys` ≠ the live TTBR0 base at the apex row, so the apex leaf is not provably the live table (`entry_stubs.c:2894-2895`) | The apex must read **the table the MMU walks** rather than the pmap's own field. Rebuild: `entry_note_abt_wleaf` walks from `entry_ttbr_base(armreg_ttbr_read(), ttbcr & 7)` **when** `proof == 0`, publishing both leaves (`_wwleaf` from the pmap's table, `_wwleafq` from the live TTBR0) and the flag. `entry_ttbr_base` (`entry_stubs.c:2131-2139`) is already the ONE spelling of the alignment rule — 1010b's fixed-constant mask is the defect this must not repeat. |
| **V5 `NO_APEX_WITNESS`** (`_wwn = 0`) | the enter never reached the wrapper on the storm VA | Re-assert the **keying**: the build already asserts `entry_note_abt_cur(far_)` is **strictly before** `__real_sleh_abort(regs, type);` by line number (`scripts/build_1013.sh:1840-1842`). Rebuild with the assertion re-run **and** a new cell: publish the recorder's `g_abtpte_curva` at the moment of the abort **and** the count of `pmap_enter_options` calls seen by the wrapper at all (`_wwall`), so "the wrapper saw nothing" separates from "the wrapper's filter dropped everything". |
| **V6 `HARD (stale 0)`** | a present L2 with no leaf, or a published decode that is not the decode of the published leaf (`tools/check_1013_capture.py:4939-4947`) | The recorder is broken: the decode (`_wwapx` from `_wwleaf`) and the leaf must be published from **one read into one local**, and the build must assert that the store of the leaf and the store of its decode share that local (a source-level check, `grep -c` on the published expression, mirroring `scripts/build_1013.sh:1857`). Rebuild and re-press. |
| any `*_NULL` / `MEASUREMENT_DEFECT` | the named gate that fired | Rebuild **only that instrument**, never the kernel; the run installs no fix in any branch. |

**The rule for all of §5:** the rebuild changes the **measurement**, never the kernel's
behaviour, and it must re-pass §2.4's OFF-byte-identity and negative-control clauses before it
is parked. A refusal is a reading about the instrument ([[mi4-measurement-defects]]); the
response to it is a better instrument, not a conclusion.

---

## 6. Shared build discipline (all three rungs)

1. **The switch set is 1013's, verbatim, plus ONE switch** — the `scripts/build_1013.sh`
   precedent (`:2` "1012's switch set VERBATIM plus ONE new switch"). Every new build exports
   the full 1013 set for **BOTH** `STUB_DEFINES` and `STUB_DEFINES_TRACE`, and pins the new key
   in `EXPORTS` (`scripts/build_1013.sh:589`).
2. **The stale-arm refusals are extended**, not replaced: `scripts/build_1013.sh:607-618` is a
   chain of `[[ $_d6_arm == armed-d13-<pressed> ]] && { echo "FAIL: ..."; exit 1; }` guards. Each
   new rung adds **the immediately previous pressed arm** (`armed-d13-9cf2f15e`) to that chain
   with the "the entry bin did NOT move" message.
3. **`OFF` is byte-identical**, proven by rebuild-and-`cmp` of the affected objects — both the
   entry objects and (for §2/§3/§4) `out/xnu_kernel_obj_d13/osfmk_arm_pmap.o`.
4. **Body-level line assertions by LINE NUMBER** where an ordering is load-bearing (the
   `entry_note_abt_cur` precedent, `scripts/build_1013.sh:1840-1842`).
5. **`(b|bl)`, never `bl`** for any call-site count (the 1013 tail-call reader defect).
6. **The recorder is a `void` call that observes and returns.** Any rung that changes a value
   is a fix, and these are not fixes.
7. **`preflight_boot_check.sh` names every new key** — the gate is a converse check
   (`scripts/build_1013.sh:1945-1946`); a switch absent from `ENTRY_CFG_KEYS` refuses the press.

---

## 7. Keyspace collision proof (grep, this session)

The `xnu_live_abt_` namespace holds **93** literal keys
(`grep -rhoE 'xnu_live_abt_[A-Za-z0-9_]+' src/entry tools scripts | sort -u | wc -l` → `93`),
and the existing member set includes the 1013 witness family
`{wwn, wwl1, wwl2pa, wwleaf, wwapx, wwnoproof, wwpa, wwpaok, wwva, wwft, wwpatbl, wwnx,
wkn, wk, wktb, wkl1, wkl2pa, wkleaf, wkleaf2, wkstable, wkapx, wkoow, wkfsr, wka***
 wkapex, wknx, wcnt, wcva, wck, wcl1, wcl2pa, wcleaf, wcapx, wcoow}` and 1012's
`{wrn, wrsite, wrprot, wrva, wrnx}`. Every prefix proposed in §2/§3/§4 was grepped tree-wide
(`--include=*.c --include=*.h --include=*.py --include=*.sh --include=*.md`) and returned **0**:

```
xnu_live_abt_wf   -> 0     xnu_live_abt_wctx -> 0     xnu_live_abt_wp -> 0
xnu_live_abt_wl   -> 0     xnu_live_abt_wx   -> 0     xnu_live_abt_wg -> 0
```

The switch names are equally free:

```
STAGE90_XNU_FLUSH_WITNESS   -> 0
STAGE90_XNU_WRITER_WITNESS  -> 0
STAGE90_XNU_L1ID_WITNESS    -> 0
```

(The tree holds 1880 distinct `STAGE90_XNU_*` tokens; none of the three is among them.)

**One deployment note, stated so it is not discovered late:** if more than one branch is ever
armed at once (which the contract forbids — 1014 is ONE rung), `wf*`/`wp*`/`wl*` are disjoint
three-way, so no key can be double-written. The `_wwnx`/`_wknx`/`_wrnx` precedent is followed
throughout: **change-counts are observations and are never the headline.**

---

## 8. MOST LIKELY BRANCH — V1, and the by-value contradiction behind it

**I judge V1 (`LEAF_WRITABLE_STILL_FAULTS` ⇒ `STAGE90_XNU_FLUSH_WITNESS`) the most likely
outcome of 1013, and the reason is a by-value contradiction already in this tree.**

**THE FLAG (loudly): two documents in this tree state the same fact in two contradictory ways,
and the WRONG one is the load-bearing one.**

* `records/1012-press-read.txt:84`, read verbatim this session:
  `"... and pmap_asid_ncpus=0 (pmap_asid.c:121) so it takes the full arm_tlb_flushID() branch."`
* `docs/1012-second-writer.md:160-165`, read verbatim this session, repeats it in substance:
  `"On this boot real_ncpus == 1 (mp.c:38) and cpu_running == 1 (arm_init.c:113; cpu.c:1056),
  so flush_self is set and arm_tlb_flushID() runs - the flush is sound provided the pmap's
  pm_l1_phys equals the live TTBR0."`

**`pmap_asid.c:121` is a static initializer, not a runtime value**, and the measured runtime
value is **1**:

```
out/stage90/captures/recovered-unknown-arm-20261015-020222-last_kmsg.txt:4003:
  All ASID/asids enabled: real_ncpus: 1, pmap_asid_ncpus: 1
```

That line is present in **25** capture files in `out/stage90/captures/`. The producer is
`pmap_asid_configure` (`pmap_asid.c:148-168`), called from `pmap_init` (`pmap.c:3681`), which
does `OSIncrementAtomic(&pmap_asid_ncpus)` at `:157`. **So `pmap_flush_tlbs` takes the
`pmap.c:1314-1319` arm: `pmap_asid_validate_cpu` then `arm_tlb_flushID_ASID(pmap->pm_asid &
0xFF)` — TLBIASID (c8,c7,2), an ASID-SCOPED invalidate, NOT the full `arm_tlb_flushID()` the
1012 record asserts.** The 1012 record read a stand-in and called it a value — the project's
own "a stand-in can be the right size and the wrong value" class.

`docs/1012-f1a-static.md:271-274` got it **right** (it quotes the capture line and names
`(c2) ASID-limited flush` as a live hypothesis with its own falsifier). The tree therefore
holds **two contradictory statements of the same fact**, and the conclusion 1012 built on the
wrong one ("⇒ so the TLB-staleness explanation is ALSO refuted by value") is **not supported**.
The ASID-scoped flush at `pmap.c:1319`, against CONTEXTIDR written at `pmap.c:2520`, is a live,
by-value, never-tested mechanism — and it is exactly the question V1 leaves open.

Supporting by-value reasons V1 is the likely 1013 cell: (i) 1012 MEASURED `prot=0x3` arriving
and the L1 bases EQUAL, and `pmap_enter_options` rebuilds the leaf **unconditionally** from
`prot` on every path (`pmap.c:3280` and `:3460`, both read this session, each preceded by
`template_pte |= pmap_vm_prot_to_page_flags(pmap, prot, wired, 0);` at `:3254` / `:3443`) — so a
writable leaf is the *predicted* output, which is what V1 says was found; (ii) V2's writers are
either dead (`:1644` under a board macro the build does not define), boot-only (`:3611`,
`:3658`), one-VA (`:3260`, `:3452`), or excluded by a by-value mask
(`pmap_update_cache_attributes_locked`, §3.1) — leaving only `pmap_page_protect`/`pmap_protect`,
which 1012's exhaustive sweep (`docs/1012-second-writer.md`) found unreachable on this path;
(iii) V3 requires the L1 bases to differ, and 1012 measured them equal at the enter — the same
cell this rung would have to overturn.

**This is a judgement of likelihood, not a result.** 1013's leaf reading decides, and §1 says
so mechanically: V2 and V3 are designed here in full, and either may be what the log shows.

---

## 9. Honest scope of this document

* **Nothing here is built.** No script, no patch tool, no switch exists yet; this is the
  decision tree the next rung is chosen from.
* **The dispatch presumes 1013's checker is correct**, including its `WIT_CLASS` strings and
  its tail-ordinal rule (`WIT_TAIL_MIN_N = 0x400`, `tools/check_1013_capture.py:1029`). If the
  log shows a class not in §1's table, that is a **checker defect** and the response is §5.
* **Every line number is the working tree at `HEAD` (`f73e026`), read this session.** Line
  numbers are hints; the anchors are the quoted bytes.
* **The branch choice is a reading, never a fix.** All three rungs are pure measurements, the
  runs are expected to freeze at the same ~24.4 s shape, and a verdict is a successful READ.
* **The `pmap_asid_ncpus` flag in §8 is about the RECORDS, not the kernel.** The kernel is
  correct by value; two of this project's own documents mis-state what it does at runtime, and
  a rung was designed on the mis-statement. Correcting the documents is a separate, operator-
  owned action — **this document only states the contradiction and the evidence.**

*(Sources read by value this session: `pmap.c` `:1095-1122`, `:1274-1328`, `:1336-1383`,
`:1720-1769`, `:1774-1827`, `:2495-2530`, `:3036-3105`, `:3112-3170`, `:3179-3183`,
`:3247-3291`, `:3440-3486`, `:3588-3685`, `:3697-3700`, `:3895-3936`, `:3943-4029`,
`:4056-4062`, `:4225-4329`, `:4337+`; `pmap.h` `:93-115`, `:190-231`; `pmap_asid.c`
`:114-168`, `:170-307`; `armv/cpufunc-v7.s` `:60-87`, `:166-170`; `proc_reg.h:583`;
`arm_vm_init.c` `:331-338`, `:473-475`; `src/entry/entry_stubs.c` `:2125-2139`, `:2141-2173`,
`:2254-2353`, `:2715-2741`, `:2806-2950`, `:2952-3039`; `src/entry/entry_trace.c`
`:5702-5705`, `:5752-5761`, `:5824-5843`, `:5965-6002`; `src/entry/build_entry.sh`
`:1330-1384`; `tools/build_xnu_arm_kernel.sh` `:202`, `:790-801`; `tools/patch_d13_wrstrip.py`
(whole); `tools/check_1013_capture.py` `:934-1030`, `:4796-4815`, `:4882-4959`, `:5010-5272`;
`docs/1013-design.md` `:247-383`; `docs/1012-second-writer.md` `:145-186`;
`records/1012-press-read.txt` `:78-92`; `out/xnu_kernel_obj_d13/osfmk_arm_pmap.o` (objdump
`-r`/`-dr`, relocation and function attribution); captures at
`out/stage90/captures/recovered-unknown-arm-20261015-020222-last_kmsg.txt` `:4003`.)*