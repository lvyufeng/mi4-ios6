# 1015 — the level-2 permission fault on a WRITABLE leaf, by value

Static forensics only. READ-ONLY. No build, no press. Tree under analysis:
`external/xnu-hd2-darwin13/xnu/osfmk/arm/` (D13, `master`). The 4570 tree is NOT consulted — frozen
different branch.

Capture: `out/stage90/captures/recovered-unknown-arm-20261015-092217-last_kmsg.txt`
(arm `armed-d13-29ec9e48`, commits `ad01a89`+`d312229`; record `records/1014-press-read.txt`).

## The measurement under adjudication

Tail storm rows (ordinal `n >= 0x20000`, e.g. capture lines 33600-33700 and the file tail 34317-34379):

    far = 0x000e6000   fsr = 0x0000080f   abtcpsr = 0x60000030   type = 4 (SLEH_ABORT_TYPE_DATA_ABORT)
    pc  = 0x2bed61a0   ptenx/wknx = 0x392 (392 distinct descriptors seen over the whole run)
    ptettbr = 0x81a8605b   pteu = 1   pteva = 0x000e6000
    ptel1 = 0x81bb2001   ptel2 = 0x841e91bb   pteoow = 0
    wkl1  = 0x81bb2001   wkl2pa = 0x81bb2000   wkleaf = wkleaf2 = 0x841e91bb   wkstable = 1
    wkleaf2 == wkleaf  (double-read stable, same word)
    wfkind = 2   wfself = 1   wfseen = 1   wfttb = wflive = 0x81a86000   wfasid = 2   wctx = 2
    wfncpus = 1
    ptethr = 0x81a86000   (thread->map->pmap->pm_l1_phys, STAGE90_ACT_MAP chain)
    pmppmap = 0x81a86000   pmpttbr = 0x81a8605b   pmpprot = 3   wwpa = 0x841e9   wwpatbl = 0
    vmfret = 0 (KERN_SUCCESS)   vmeprot = 3

Channel registers read once at console install (`entry_stubs.c:3929-3932`):
`xnu_live_ttbcr = 0`, `xnu_live_dacr = 0x1`, `xnu_live_sctlr = 0x00c55879`, `xnu_live_prrr = 0`.

**`abtcpsr = 0x60000030` ⇒ CPSR[4:0] = 0b10000 = 0x10 = User mode.** This IS a user-mode write.

---

## Item 1 — AP bits (`pmap.h:217-231`, `arm_l2_entry_t` `pmap.h:128-139`)

`arm_l2_entry_t` (`pmap.h:128`): `nx:1`@bit0, `valid:1`@bit1, `bufferable:1`@bit2, `cacheable:1`@bit3,
**`ap:2`@bits[5:4]**, `tex:3`@bits[14:12], **`apx:1`@bit9**, `shareable:1`@bit10, `non_global:1`@bit11,
`pfn:20`@bits[31:12].

Constants (`pmap.h`): `L2_ACCESS_PRW 0x10` (`:218`) = bit4 = **ap[0]**; `L2_ACCESS_USER (1<<5)`
(`:221`) = bit5 = **ap[1]**; `L2_ACCESS_APX (1<<9)` (`:222`) = AP[2]. `L2_SMALL_PAGE 0x2` (`:226`) is
**bit 1, the TYPE bit**; `L2_NX_BIT 0x1` (`:227`) is bit0 = **XN**, not part of the type
(1013c's fix, `d312229`).

Decode `0x841e91bb` by hand. `0x...91bb` = `1001 0001 1011 1011`:

| bits | field | value |
|---|---|---|
| [1:0] | type | `0b11` → bit1=1 = small page (present) |
| bit0 | XN | 1 (execute-never; a DATA page, `pmap.c:1128` sets it for `!VM_PROT_EXECUTE`) |
| bit2 | B | 0 |
| bit3 | C | 1 |
| **[5:4]** | **ap** | **`0b11`** |
| **[9]** | **apx** | **0** |
| bit10 | S | 0 (not shareable) |
| [14:12] | TEX | `0b001` → TEX=001, C=1, B=0 |
| [31:12] | pfn | `0x841e9` → PA `0x841e9000` |

ARMv7 short-descriptor, AP[2:0] = apx:ap[1:0] = `0b011`, SCTLR.TRE=0:
**`011` = privileged RW, user RW.** With `apx = 0` a USER WRITE IS ALLOWED.

This is exactly what the builder produces: `pmap_vm_prot_to_page_flags` (`pmap.c:1104-1130`) sets
`L2_ACCESS_USER` for every non-kernel pmap (`:1113`), sets `L2_ACCESS_PRW` unconditionally (`:1114`),
and **clears** `L2_ACCESS_APX` when `VM_PROT_WRITE` (`:1119-1120`). The apex read `pmpprot=0x3`
(`VM_PROT_READ|VM_PROT_WRITE`) through `pmap_enter_options` (`pmap.c:3484-3494`).

**Verdict: the leaf is NOT the cause. RULED OUT.**

## Item 2 — Domain field and DACR (`locore.s:75-78`, `pmap.h:117-126`)

`arm_l1_entry_t` (`pmap.h:117`): **`domain:4` at bits[8:5]**.

The ONLY place a user coarse L1 descriptor is built is `pmap_expand`:
`pmap.c:3205` — `*tte = ((page->phys_page << PAGE_SHIFT) & L1_PTE_ADDR_MASK) | L1_TYPE_PTE;`
`L1_TYPE_PTE` = 1 (`pmap.h:210`), `L1_PTE_ADDR_MASK` = `0xfffffc00` (`pmap.h:202`). No domain bits are
OR'd — **the domain field is 0.** (Confirmed by value: the L1 word is `0x81bb2001`, bits[8:5] = `0`.)

`locore.s:75-78` writes `DACR = 0x1`, i.e. **DACR[0] = 0b01 = CLIENT**. Nothing else in the tree writes
`c3,c0,0` (grep: only `locore.s:78`).

Under a CLIENT domain, AP is checked: `ap=0b11` ⇒ user RW ⇒ **ALLOWED**. Under a MANAGER domain AP is
ignored and access is allowed outright. `DACR[0]=1` (client) is therefore still permissive here; a
permission fault would require `DACR[0]==0` (NO ACCESS, which raises a **domain** fault with
DFSR status `0b1011` = 11, not 15) or an AP that denies user writes. Neither holds.
(DFSR's own Domain field, bits[7:4] of `0x80f`, is 0 — consistent.)

**Verdict: domain + DACR are NOT the cause. RULED OUT.**

## Item 3 — Section vs page vs supersection

`L1_TYPE_MASK` = 3 (`pmap.h:207`); `0x81bb2001 & 3 = 0b01` = `L1_TYPE_PTE` = **coarse page table**
(`pmap.h:210` `tte_is_page_table`). Not a section (`0b10`), not reserved (`0b11`).
L2 table base = `0x81bb2001 & 0xfffffc00` = `0x81bb2000` — 1 KB aligned, first 256 entries of a 4 KB
page, exactly what `pmap_expand`'s `pmap_grab_page` gives and what `pte_offset` (`pmap.h:199`) indexes.
`L2_SMALL_PAGE` = 4 KB granularity; supersection (`0b10` L1, 16 MB) does not exist in this port.

**Verdict: confirmed coarse 4 KB page tables. No section/supersection subtlety.**

## Item 4 — TTBR routing (`arm_vm_init.c:473-475`)

`arm_vm_init.c:473-475`: `set_mmu_ttb(cpu_ttb); set_mmu_ttb_alt(cpu_ttb); set_mmu_ttbcr(2);`

TTBCR.N = 2 ⇒ split = `2^(32-2)` = `0x40000000`. TTBR0 owns `[0, 0x40000000)`, TTBR1 owns
`[0x40000000, 4 GiB)`. **`0xe6000 < 0x40000000` ⇒ TTBR0 resolves it.** The walker implements the same
rule (`entry_stubs.c:2314-2343`), reads TTBR0 via `mrc p15,0,rX,c2,c0,0`, and `_pteu = 1` records
"TTBR0 answered". TTBR1 holds `cpu_ttb` (`set_mmu_ttb_alt`, `:474`) and is irrelevant here.

**Verdict: routing is TTBR0 and correct. RULED OUT.** (This also disposes of any "the VA is being
resolved through the kernel table" idea: `0xe6000` is nowhere near the split.)

## Item 5 — A SECOND / DIFFERENT pmap  ← the task's highest-value question

How to read the FAULTING thread's pmap: `current_thread()` = TPIDRPRW (`mrc p15,0,rX,c13,c0,4`,
`entry_stubs.c:655`) → `thread->map` (`STAGE90_ACT_MAP`, `entry_saved_state.h:102`) → `map->pmap`
(`STAGE90_MAP_PMAP`) → `pmap->pm_l1_phys` (**offset 0** of `struct pmap`, `pmap.h:99-100`,
`#pragma pack(4)`; the same field `pmap.c:1280` reads). Implemented as
`stage90_abt_thread_l1_base()` (`entry_stubs.c:2487-2503`), published as `_ptethr` (`:2572`), and it is
read on the SAME tail rows as the storm.

`_ptethr = 0x81a86000` = `_ptettbr & 0xfffffc00` = `_pmppmap` = `_wfttb` = `_wflive`. Four independent
paths — the faulting thread's own map chain, the live TTBR0 the walk sampled, the pmap passed to
`pmap_enter_options`, and the pmap whose L1 `pmap_flush_tlbs` compared at `pmap.c:1298` — all name the
SAME 4 KB table base `0x81a86000`, on the tail storm rows. A page-table identity mismatch is
**measured against** by four cells that would each have had to lie in the same direction.

The early rows DID see a different pmap: `_ptethr = 0x81a6e000` with `_wctx = 1`, `_wfasid = 1` at
ordinals 0x2000-region. The storm's tail is unambiguously the `0x81a86000` / ASID-2 pmap.

**Verdict: the faulting thread's pmap IS the measured pmap. RULED OUT** — the one "everything would be
explained" hypothesis is itself falsified by `_ptethr`.

## Item 6 — TLB staleness under an ASID  ← **the record is WRONG here**

`pmap.c:1344-1352`:

    if (pmap_asid_ncpus) {
        pmap_asid_validate_cpu(pmap, my_cpu);
        if (pmap_is_shared) arm_tlb_flushID();
        else                arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF);
    } else arm_tlb_flushID();

`arm_tlb_flushID_ASID` is `armv/cpufunc-v7.s:66-74`. The assembled D13 object
(`out/xnu_asm_obj_d13/cpufunc-v7.o`, disassembled) is **`mcr 15,0,r0,cr8,cr7,{2}` = TLBIASID** —
invalidate by ASID. `arm_tlb_flushID` is `cr8,cr7,{0}` = **TLBIALL**.

**TLBIASID invalidates only NON-GLOBAL entries.** Entries with `nG = 0` are *global*: they are not
tagged with an ASID at all and are matched by VA alone, so **TLBIASID cannot invalidate them**; only
TLBIALL (or a per-entry invalidate) can.

**Every leaf this port installs is global.** `nG` is bit 11 of `arm_l2_entry_t` (`pmap.h:137`,
`non_global:1`), sourced from `L2_NG_BIT 0x800` (`pmap.h:231`) — and a tree-wide grep finds
`L2_NG_BIT` **at its definition line only and nowhere else**; nothing in `pmap.c` sets bit 11.
`arm_pte_prot_templates` (`pmap.c:565-581`) carries `nx/ap/apx` only; `pmap_get_cache_attributes`
(`pmap.c:1211-1235`) returns only TEX/C/B. Our leaf's bit11 = 0 confirms it by value, on the storm's
own rows.

Therefore **`wfkind = 2` (an ASID-scoped invalidate that ran, under the matching ASID) does not
establish that any *global* translation for `0xe6000` was invalidated.** The record's §1 claim —
"F2 … The stale-TLB explanation (F1) is dead" — is a **measurement defect**: it generalises a
non-global result to a TLB that is entirely global for this port's leaves. F1 is *narrowed*, not
eliminated.

That said, a full TLBIALL does run on the pmap_switch path: `arm_context_switch`
(`armv/cpufunc-v7.s:34-44`, disasm `:28 mcr cr8,cr7,{0}`) invalidates the whole TLB after each TTBR0
write, so a stale global entry is normally purged at the ASID-1→ASID-2 transition. The claim is still
load-bearing for the discriminating press: it says *which* flush the next arm must witness.

**Verdict: staleness is NOT excluded by F2 as measured — but no positive stale-global entry is measured
either. OPEN, and the record must be corrected.** See the STALE-GLOBAL-TLB section below for the full
adjudication: the flush-policy defect is CONFIRMED, the hypothesis that it *causes this storm* is
NARROWED (contradicted by the capture's fault history).

## Item 7 — execution-never / nG / per-process-only subtleties

- XN: bit0 = 1 on the leaf, but XN only gates **instruction fetch**; this is a data WRITE (`WnR=1`,
  `fsr` bit 11). It cannot raise a data permission fault. Not a cause.
- nG: as in Item 6 — the field is never set, so the entry is global. Relevant to Item 6, not itself a
  fault cause.
- Shareable (bit10=0), TEX/C/B decoded above as Normal write-back; none of these can deny a CPU write.
- The commpage hacks (`pmap.c:1685-1686`, `:3490-3492`) force `L2_ACCESS_USER` (or `USER|APX|NX` for
  the `IOS7LAB_COMMPAGE_ALIAS`) and are keyed on `_COMM_PAGE_BASE_ADDRESS` / `IOS7LAB_COMMPAGE_ALIAS`,
  not on `0xe6000`. Irrelevant here.

**Verdict: no execution-never or per-process-only mechanism applies. RULED OUT.**

## Item 8 — FSR nuance (`trap.c:359-360`, `trap.c:378-414`)

`SLEH_ABORT_TYPE_DATA_ABORT = 4` (`trap.c:360`) — `_abt_type = 4` confirms the DATA side, so the pair
read is DFSR/DFAR (`c5,c0,0`/`c6,c0,0`, `entry_trace.c:5744-5747`).

`trap.c:378-414`'s own decode: `case 15: return "Permission fault on page"` (`:411-412`) — Apple's own
table calls bit[3:0]=15 the level-2 permission fault, matching the short-descriptor DFSR.
`0x0000080f` decomposes as: bits[3:0] = `0xF` = permission fault, level 2; **bit11 (WnR) = 1** = a
write; bits[7:4] (Domain) = 0; **bit10 (FS[4], the "extended format" bit) = 0** ⇒ standard ARMv7
short-descriptor DFSR, **not LPAE**, so bit9 is not an "LP" bit and the status really is 15. (LPAE's
long-descriptor format sets FSR bit 9; `0x80f` has bit9 = 0.)

**Verdict: the FSR unambiguously states a WRITE permission fault, status 15, level 2, domain 0.
Confirmed.**

## Item 9 — `_wwnoproof` POLARITY INVERSION (confirmed by value)

`src/entry/entry_stubs.c:2965-2966`:

    ttbrbase = entry_ttbr_base(ttbr, ttbcr & 7u);              /* live TTBR0 */
    proof = (ttbrbase != 0u && ttbrbase == entry_ttbr_base(pm_l1_phys, ttbcr & 7u)) ? 1u : 0u;

`proof` is **1 when the two bases are EQUAL**. It is published as `_wwnoproof`
(`entry_stubs.c:2998`). The comment immediately above (`:2957-2963`) says *"`_wwnoproof`: 1 when the
pmap's own L1 base is NOT the base the MMU is walking with"* — the documented meaning is the **negation**
of what the code computes. **CONFIRMED: the polarity is inverted; `1` means "provably the SAME table",
not "not provable".**

The checker inherits the inversion: `tools/check_1013_capture.py:4832` (*"The pmap's own L1 base is not
the live TTBR0's base"*), `:5156-5162` (`APEX_NOT_PROOF` when `noproof == 1`), `:5202`, `:5435` all read
`1` as failure. So the tool flags the **healthy** case as the defect and passes the broken one.

A second, independent defect sits at the same cell: `pm_l1_phys` at `:2966` is a **post-`__real_` read**
(`entry_note_abt_wleaf` reads `*pmap` at `:2934`, after `__wrap_pmap_enter_options`'s real call), while
`ttbrbase` is live. The comment at `:2965-2985` (1015's `PERM_WITNESS` block) admits this and adds
`_wwpre`/`_wwpost`/`_wwmoved` — but leaves `_wwnoproof` publishing the invert-named `proof` unchanged.

**What `_wwnoproof = 1` (bases equal) PROVES, and what it does NOT.** It proves that the pmap whose
`pm_l1_phys` the recorder sampled names the same 4 KB table base the MMU's TTBR0 names *at that instant
after the enter*. It does **not** establish that the CPU's cached translation for `0xe6000` came from
that table (Item 6), nor that the table's contents were the ones walked *at the moment the store
executed* (the table can be rewritten between walk and store — `pmap_expand`, `pmap_flush_tlbs`). So it
**rules out "the enter wrote a different table than the one the walk reads"** and it **reinforces
ruling out Item 5 (a different pmap)** — but only as one more corroborating equality, on top of the
three that were already independent of this buggy cell (`_ptethr`, `_ptettbr`, `_wfttb`). It does not
touch the TLB-staleness question.

---

## STALE-GLOBAL-TLB — adjudication of the team-lead's hypothesis

**Verdict: NARROWED (not CONFIRMED, not REFUTED).** The mechanism is real and the record's F1
elimination was wrong; but the hypothesis as stated — *"a stale global entry survives every flush and
this storm is that"* — is contradicted by the capture's own fault history, and the HD2 reference does
**not** escape it.

**Every link of the chain, checked:**

1. **Leaf bit11 = 0 ⇒ global.** CONFIRMED. `arm_l2_entry_t` (`pmap.h:127-139`): bit10 = `shareable`,
   bit11 = `non_global`. `pmap.h:231 #define L2_NG_BIT 0x800`. `0x841e91bb` bit11 = 0.
2. **`L2_NG_BIT` is never emitted.** CONFIRMED, and stronger than stated: the grep must exclude decimal
   literals. `pmap.c:218-219` read `pmap_nesting_size_min = 0x8000000` — a *different* number that a
   naive `0x800` grep will match. Excluding those, `L2_NG_BIT` occurs **only at `pmap.h:231`**, in the
   whole of `src/` and `external/xnu-hd2-darwin13/`. The templates (`pmap.c:565-581`) carry
   `nx/ap/apx` only; `pmap_get_cache_attributes` (`pmap.c:1211-1235`) returns TEX/C/B only;
   `L2_ADDR_MASK 0xfffff000` (`pmap.h:214`) and `L2_SMALL_PAGE 0x2` (`:226`) do not touch bit11. So
   every leaf this port ever builds is global.
3. **The storm's flush is TLBIASID.** CONFIRMED by disassembly: `pmap.c:1349` →
   `out/xnu_asm_obj_d13/cpufunc-v7.o` `arm_tlb_flushID_ASID` = `ee080f57 mcr 15,0,r0,cr8,cr7,{2}`.
   The `_wfncpus = 1` cell proves the `pmap_asid_ncpus` arm was taken, and `pmap_asid.c:157-158`'s
   console line (`PMAP: enabled ASID support` / `All ASID/asids enabled: real_ncpus: 1,
   pmap_asid_ncpus: 1`, capture lines 4001-4003) proves the increment completed.
4. **"Therefore a stale global entry survives."** This is the ARM-spec link and it is **unverified by
   this session**. WebSearch returned no content and the ARM developer page returned a 301 to a page
   with no article text, so I could not obtain a citable sentence. I state plainly: **I could not
   verify by value whether TLBIASID evicts global entries on this implementation.** The claim is
   plausible and widely held, but as it stands it rests on assumption, not on a measurement or a
   quoted spec line. It must not be a load-bearing premise of the press.
5. **`arm_context_switch` full TLBIALL only on a change.** CONFIRMED: `pmap.c:2554-2566` bails at
   `user_pmap == new_pmap` before reaching `arm_set_context_id`/`arm_context_switch`; the storm
   re-enters one pmap.

**The counter-check the hypothesis did not survive — `pmap_expand` runs a full flush.** `0xe6000` is
the FIRST page of the `0xe` L1 region (`tte_offset(0xe6000) = 0`), so `pmap_pte` is NULL there and
Apple's `pmap_enter_options` is forced through `while ((pte = pmap_pte(...)) == 0) pmap_expand(pmap, va)`
(`pmap.c:3262-3266`) — *"Expand the pmap to include the new PTE if necessary"*. `pmap_expand` ends
`*tte = ... | L1_TYPE_PTE; pmap_flush_tlbs(map, v, v + PAGE_SIZE);` (`pmap.c:3205, 3210`). At that
moment `map == current_cpu_datap()->user_pmap` (the faulting thread is executing), so
`pmap_flush_tlbs`'s `ttb == ttbr_pmap` test (`:1298`) is **TRUE**, `flush_self = TRUE`, and the flush
is **`arm_tlb_flushID_ASID`** — the same weak flush. So the expand does **not** escape the hypothesis.

**But the storm's own history rules the hypothesis OUT as the cause of THIS storm.** The capture has
**9** `far = 0xe6000` faults (lines 32939, 33057, 33144, 33231, 33318, 33412, 33499, 33593, 33687 —
a doubling ladder `vfse`) and **every one has `fsr = 0x0000080f`**. The VA is never hit as an
`0x805`/`0x807` translation fault first. A stale entry is only *created* by a protection change on a
line that was already translated — which would have to come from a first fault taken on **this same**
VA **before** any ladder row. The ladder's ordinal 1 and 2 are not in the file (`farn/fsr` start at
`nfar=0x400`), so I cannot exclude a pre-ladder `0x807`, but nothing in the capture shows one.
**A simpler reading, fully consistent with all cells, is that no entry was ever created** — the very
first access to `0xe6000` faulted, was handled, and the retry re-faulted with identical status because
**there is no valid translation for it at all**, not because a stale one survived. This is the reading
the cells support; the "written-over RO→RW in place while a stale RW-less entry persisted" history is
**not** supported by any cell.

**How the HD2 reference escapes it: it does not escape it — and it does not need to.** `/tmp/leo-up/pmap.c`
is byte-equivalent at every load-bearing point: no `L2_NG_BIT` emission anywhere (grep: zero hits), the
same `pmap_flush_tlbs` with the same `arm_tlb_flushID_ASID(pmap->pm_asid & 0xFF)` at `:1318`, and the
same `pmap_switch` bail. The three full-TLBIALL call sites are the same three
(`pmap_flush_tlbs` for `pmap_is_shared`, `pmap_asid_configure` at boot, `pmap_destroy` via
`pmap_flush_tlbs(pmap,0,0xFFFFFFFF)`). **There is no per-retry or per-element full flush in the
reference that our tree lacks.** This is a genuine counter-signal against the hypothesis doing
explanatory work: whatever the mechanism, HD2 boots with exactly this code. The most likely reason is
that on HD2 the `0xe6000`-class first-touch fault is a genuine **translation** fault (`0x805`/`0x807`),
which `pmap_enter` fixes by *creating* the entry — and a freshly created entry is used by the retry
regardless of global/ASID semantics. Our storm re-faults with a **permission** status on a VA whose leaf
already exists, which is a different starting condition than HD2's normal path, and the reference
therefore offers no evidence either way about our storm.

**What this leaves:** the global/TLBIASID observation is a real, correct-by-value property of the port,
and the record's blanket "F1 is dead" is wrong. But the global-TLB mechanism does **not** explain a storm
that has never once shown a translation fault at this VA; it explains a *different* shape (a
permission-change flipping an already-cached translation). **NARROWED: report it as a correctness
defect in the flush policy (global entries in a per-ASID-invalidated TLB) and as the reason the record's
F1 elimination is invalid — not as the measured cause of this storm.** The decisive experiment is still
K6/K7 (ask the hardware with `AT`/`PAR`) and, if K6 faults, a TAINTED-VA test: pre-set the leaf to the
TARGET writability *before first touch*, then fault — a stale-transition mechanism still faults, a
no-translation mechanism does not.

---

# RANKED CAUSES

0. **NO TRANSLATION WAS EVER CREATED for `0xe6000` — the PMAP NEVER TOOK THE WRITE.** (Added by the
   STALE-GLOBAL-TLB adjudication above.) All 9 `far=0xe6000` faults carry `fsr=0x80f`; the VA is never
   once seen as `0x805`/`0x807` (translation). A stale-entry mechanism *requires* an earlier successful
   translation of this VA; nothing in the capture shows one. The mechanism that fits every cell without
   inventing an unseen step is that the retry re-executes the store against a translation that
   `pmap_enter_options` failed to install, and `vm_fault` returns `KERN_SUCCESS` anyway
   (`_vmfret = 0`, `_vmekr = 0`) — i.e. the enter "succeeded" without an effective mapping. **This is
   the reading the cells support; it is ranked first because it needs no unobserved step.**

1. **A translation whose permission is NOT the leaf's — i.e. a stale/other TLB entry — is NOT excluded
   (Item 6).** The only flush witnessed as running for the storm is **TLBIASID**, and every leaf in this
   port is **global (nG=0)**, which TLBIASID cannot invalidate. `wfkind = 2` therefore does not prove
   the TLB held the table's writable leaf. This is the single largest hole in the record's reasoning.
   **NARROWED, not confirmed**: it explains a permission-*change* on an already-cached translation, and
   no such change is measured here (see the adjudication). Secondary to cause 0 for *this* storm.

2. **The fault is real, the leaf is real and writable, and the CPU is NOT consulting that leaf — because
   the CPU's translation decision is made from SCTLR/PRRR-controlled state the probes never read.**
   `SCTLR = 0x00c55879` at console install has **TRE (bit28) = 0** and **AFE (bit1) = 0**, and
   `PRRR = 0`. With TRE=0 the leaf's TEX/C/B are used raw (Normal WB — benign), and AFE=0 means an
   Access-Flag fault would be status 3/6, not 15, so this does not produce *this* FSR. **Low**, but it
   is the residual "outside the page tables" bucket and must be closed by reading SCTLR/PRRR *at storm
   time*.

3. **A pmap/table identity mismatch** — the task's headline hypothesis. **Falsified by value**:
   `_ptethr == _ptettbr == _pmppmap == _wfttb == _wflive == 0x81a86000` on the tail rows. Four
   independent readers of "which table" agree, and Item 9's polarity fix adds a fifth equality (the
   apex's post-enter `pm_l1_phys` base == the live TTBR0 base). **RULED OUT** — precisely: ruled out is
   *"the enter wrote into a table other than the one the walk reads"*. NOT ruled out by this are (a)
   the CPU's cached translation coming from somewhere other than that table (Item 6/cause 1), and
   (b) the table being rewritten between the walk and the store.

4. **The leaf, domain, DACR and TTBR routing.** **RULED OUT** — Items 1-4: AP[2:0]=`0b011` is user-RW;
   L1 domain field = 0; DACR[0] = 1 = client (permissive for AP=011; a client-vs-manager distinction
   cannot deny a write that manager mode would allow); TTBCR.N=2 routes `0xe6000` through TTBR0.
   If Items 1-4 all hold — and they do, measured, on the storm's own rows — then **the leaf, the
   domain, the DACR and the routing are all ruled out and the cause is either a TLB entry that is not
   the leaf, or state outside the page tables.** State this plainly: the record's §2 conclusion
   "V1 LEAF_WRITABLE_STILL_FAULTS holds" is correct, and its §6 conclusion that the next rung must
   read "the OTHER permission inputs" is right — but its §1 elimination of staleness (F1) is wrong.

## Contradictions with the record, itemised

- `records/1014-press-read.txt` §1: *"The stale-TLB explanation (F1) is dead."* — **Wrong as stated.**
  F2 proves a flush ran; it does not prove a *global* entry was invalidated, and this port's entries are
  all global. F1 is narrowed, not eliminated.
- §2: *"bit4 (PRW)=1; bit9 (APX)=0 ⇒ USER-WRITABLE"* — correct; must add that ap[1] (bit5) is also 1,
  i.e. ap = `0b11`, which is what makes user RW rather than user RO.
- §3: the `_wwnoproof` self-contradiction analysis is sound; note that W-WALK (`wkl1`) and the apex
  (`wwl1`) both read `0x81bb2001`, so the pairing is valid independently, as the record already says.

# WHAT A SINGLE PRESS MUST MEASURE TO DISCRIMINATE THEM

All reads INSIDE `src/entry/entry_stubs.c`'s recorder, called from `__wrap_sleh_abort`
(`src/entry/entry_trace.c:5732`), **BEFORE `__real_sleh_abort`**, keyed on `g_abtpte_curva`, on the
ladder tail ordininals only. Reuse the tree's own spellings — `osfmk/arm/proc_reg.h` — so no value gets
a second definition.

| # | key (suggested) | reads | where |
|---|---|---|---|
| K1 | `_w_sctlr` | `armreg_sctlr_read()` via `proc_reg.h` | rules out stale/other SCTLR (TRE, AFE, XN-enable) |
| K2 | `_w_dacr_raw` | `mrc p15,0,rX,c3,c0,0` | the STORM's DACR, not the console-install one (`locore.s:78`) |
| K3 | `_w_ttbcr_raw` | `armreg_ttbcr_read()` (`proc_reg.h:511`) | the STORM's N, to re-derive the split |
| K4 | `_w_l1domain` | `(ptel1 >> 5) & 0xF` — **already have `ptel1`** | the L1 **domain field** (`pmap.h:118`), decoded, not assumed 0 |
| K5 | `_w_leafbits` | `ptel2 & 0xFFF` — the full low-12 decode (ap, apx, tex, S, **nG**) | proves the nG=0 (global) claim on the storm's own leaf |
| K6 | **`_w_at_par`** | write `armreg_va2pa_uw_ns(cura)` (`proc_reg.h:547`), read `armreg_par_read()` (`proc_reg.h:522`) | **the killer key**: the HARDWARE, asked directly, whether `0xe6000` translates for a *user write*, and to which PA. `par & 1` = fault; else `par & 0xfffff000` = the PA the MMU itself uses. **This spelling is the tree's OWN for exactly this job**: `pmap_extract`'s `_ARM_ARCH_7` arm (`pmap.c:3016-3051`) selects `va2pa_ur_ns` precisely when `pmap != kernel_pmap` (`is_priv = FALSE`, `:3018`), i.e. to translate a USER-pmap VA from PL1. Use `uw` for the write question. Still run the control (see below). |
| K7 | `_w_at_par_pr` | same with `armreg_va2pa_pr_ns` (`:544`) | privileged view; K6 vs K7 separates "AP denies user" from "other" |
| K8 | `_w_thpmap` | `stage90_abt_thread_l1_base()` (exists, `_ptethr`) **plus** `current_cpu_datap()->user_pmap` | closes Item 5 from BOTH sides at the same instant |

**The NULL CONTROL for K6, without which K6 is unreadable.** Measure `_w_at_par` in the
console-install context FIRST, against a KNOWN-GOOD VA — `0x80080000` is in `__TEXT` and is the exact
VA 1013's W-SYNC control already reads a correct writable leaf at (`_wcleaf = 0x8008021a`,
`_wck = 1`). A K6 that reports `par & 1` (fault) for `0x80080000` is a **null reading** — the op is
refused in that context and says nothing about `0xe6000`. Only a K6 with a passing control is a
measurement. Publish the control row and the storm row under the same key so the pairing is by
construction, never by the reader's assumption (the 998/1013b lesson).

**The discriminating rule.** Read K6/K7 at the same ordinal as `ptel1`/`ptel2`:

- **K6 faults while K5 shows ap=`0b11`, apx=0 and K4's domain is 0** ⇒ the MMU's translation is not the
  leaf the walk reads: a TLB entry that is not this leaf. If K5's **nG = 0**, the culprit is a global
  entry surviving TLBIASID — fix by making the flush for a user pmap a range/global invalidate
  (`arm_tlb_flushID` or a VA-scoped one), or by setting nG on user leaves.
- **K6 succeeds and names PA `0x841e9000`** while the store still faults ⇒ the fault is not from this
  translation at all: the DFSR/DFAR pair and the frame are describing a different access, and the probe
  ordering/key must be re-examined (the `g_abtpte_curva` write in `__wrap_sleh_abort`).
- **K6 succeeds and names a DIFFERENT PA** ⇒ wrong translation in the TLB (same fix as the first case).

If K6 is added and K1-K5 confirm SCTLR-as-read, DACR[0]=1, N=2, domain=0, ap=011/APX=0/nG=0, then the
page tables are exhaustively cleared by measurement and only the TLB-entry identity (or the probe's own
key) remains — which is precisely the question K6 answers.

## The one-line correction to the record

**`wfkind = 2` (TLBIASID) cannot invalidate a global entry, and this port installs every leaf as global
(`nG` is never set — `L2_NG_BIT`, `pmap.h:231`, appears at its definition only). The record's
elimination of stale-TLB (F1) by F2 is therefore invalid; the ASID flush is the wrong instrument for a
global entry, and the next press must measure `nG` on the storm's own leaf and ask the hardware with
`AT`/`PAR` rather than a host-side walk.**