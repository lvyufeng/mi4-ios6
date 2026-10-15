# 1015 — THE AT/PAR HARDWARE CONFIRMATION: is the CPU's translation of `0xe6000` the walked leaf? (ENTRY-ONLY)

**This is a rung design, not a build.** 1014 (`armed-d13-29ec9e48`, pressed, RESIDENT) read the leaf
**VALUE** and the **flush**: the leaf the MMU walks is user-writable, the ASID-scoped flush ran under
the matching ASID — and the store still takes `FS=15` permission faults at a constant `far`/`pc`,
~2^20 times. The decision the CPU makes **is not the leaf's AP bits**.

Two by-value findings name the leading cause:

* **Every user leaf is GLOBAL.** `L2_NG_BIT 0x800` (`pmap.h:231`) is **never ORed anywhere in the
  tree** — `grep -rn L2_NG_BIT --include=*.c --include=*.h .` returns its definition and nothing else.
  The storm's leaf `0x841e91bb` has bit11 (`non_global`, `pmap.h:137`) = **0** ⇒ GLOBAL.
* **The steady-state flush cannot evict a GLOBAL entry.** `pmap_asid_configure` issues a full
  `arm_tlb_flushID()` **once at boot** (`pmap_asid.c:161`, called from `pmap_init`, `pmap.c:3723`).
  The only steady-state flush is `arm_tlb_flushID_ASID` (`pmap.c:1349`, = 1014's measured
  `_wfkind=2`). On ARMv7 **TLBIASID invalidates only ASID-tagged (`nG=1`) entries; a GLOBAL entry
  survives it** — only TLBIALL (`c8,c7,0`) evicts it.

⇒ **H-G (STALE_GLOBAL_TLB)**: a stale **GLOBAL** translation of `0xe6000` — installed when the page
was mapped read-only by earlier activity — survives every ASID flush and **shadows** the newer
user-RW leaf. That is exactly a *constant* `far`/`pc`, FS=15 **write** re-fault on a page whose
current leaf is writable. **1015 confirms or refutes it at the HARDWARE: the ARMv7 AT instruction
asks the MMU itself whether `0xe6000` translates for a user write, and to which PA (§2a, §3a).**

### 1015 is ENTRY-ONLY — the active half is DEFERRED to 1015b (§2b)

The full-flush experiment (`STAGE90_XNU_USER_FULL_FLUSH`, making `pmap_flush_tlbs` take a full
`arm_tlb_flushID()` for non-kernel pmaps) was the original primary of this design. **The lead
deferred it to a follow-on rung 1015b**, for three reasons recorded here so the decision travels with
the design:

1. **AT/PAR makes the passive half decisive on its own.** An AT-user-write that faults
   (`par & 1 == 1`) while the walked leaf is `ap=0b11 / apx=0 / nG=0 / domain=0` is a **by-hardware**
   confirmation that the CPU's translation is not that leaf — that *is* H-G. No behaviour change and
   no "does the fix work?" press is needed to reach the diagnosis.
2. **The full flush is a kernel-behaviour change** (the first since 986) and it **compiles OUT the
   ASID arm**, breaking the build's `TLBIASID >= 2` object assertion — so it deserves a rung where
   *the storm stopping* is the sole read-out, not a rider on a diagnostic press.
3. **One question per rung.** A press that both diagnoses and changes behaviour cannot attribute its
   own result; §5 states this as the project's single-variable rule.

Everything below is cited to a line **read by value this session** (`HEAD` = `0ef1949`). New names
grep to 0 tree-wide: `STAGE90_XNU_PERM_WITNESS`, `xnu_live_abt_leafng`, `xnu_live_abt_wd*`,
`xnu_live_abt_wdatpar`, `xnu_live_abt_wdatparpr`, `xnu_live_abt_wdctlat`, `xnu_live_abt_wdatok`,
`xnu_live_abt_wdsctlr`.
(The deferred rung's `STAGE90_XNU_USER_FULL_FLUSH` likewise greps to 0 outside this design's own
hits — see §2b/§8d.)

**Revised 2026-10-15, twice.** (i) After `docs/1015-perm-static.md` landed: it rules C1/C2 out and
independently confirms the stale-GLOBAL-TLB cause — see **§8a**; the **AT/PAR hardware witness** was
folded in (§2a K6/K7, §3a V-AT0..4). (ii) After the lead's ENTRY-ONLY decision: the active half is
now **§2b (deferred to 1015b)**, and the AT/PAR cells are the **deciding pass** (§3a).

---

## 0. THE 1014 READING, RESTATED BY VALUE (no other file needed)

Capture `out/stage90/captures/recovered-unknown-arm-20261015-092217-last_kmsg.txt`, device
`4a2fe00b`, **no panic**, `xnu_live_wdt_pets=0x61` ⇒ RESIDENT. The storm's own tail rows
(`_pcn` `0x400`..`0x40000`) all carry:

```
_pc=0x2bed61a0  _fsr=0x0000080f  _abtcpsr=0x60000030  _pcfar=0x000e6000
_ptel1=0x81bb2001  _ptel2=0x841e91bb  _ptettbr=0x81a8605b  _pteu=1
_wkl1=0x81bb2001   _wkleaf=0x841e91bb _wktb=0x81a8605b
_wwleaf=0x841e91bb _wwva=0x000e6000  _wwft=0x00000003
```

### 0.1 `0x841e91bb` decoded against the tree's OWN struct (`pmap.h:128-142`)

```
0x841e91bb = 0b1000 0100 0001 1110 1001 0001 1011 1011
  bit1 (valid)       = 1   => present ; L2_SMALL_PAGE 0x2 (pmap.h:226)
  bit0 (nx)          = 1   => L2_NX_BIT (pmap.h:227)
  bits[5:4] (ap)     = 0b11 => bit4 PRW = 1  AND  bit5 USER = 1  (pmap.h:218/:221) => USER READ|WRITE
  bit9  (apx)        = 0   => AP[2] clear, writable  (pmap.h:222)
  bits[8:6] (tex)    = 0b110 ; bit3 (C) = 1 ; bit2 (B) = 0
  bit11 (non_global) = 0   => **GLOBAL**   <-- THE H-G CELL
  bits[31:12] = 0x841e9 => PA = 0x841e9000
```

`pmap_vm_prot_to_page_flags` (`pmap.c:1104`) produced it: `if (pmap != kernel_pmap) pte |=
L2_ACCESS_USER;` (`:1113`), `pte |= L2_ACCESS_PRW;` (`:1114`), `if (prot & VM_PROT_WRITE) pte &=
~(L2_ACCESS_APX);` (`:1120`), `if (!(prot & VM_PROT_EXECUTE)) pte |= L2_NX_BIT;` (`:1128`). **It never
sets `L2_NG_BIT`** — so the leaf is GLOBAL by omission, and so is every other user leaf.

### 0.2 `0x81bb2001` decoded (`pmap.h:117-125`)

```
bits[1:0] = 0b01 => is_coarse (L1_TYPE_PTE 1, pmap.h:210) => an L2 table follows
bits[8:5] (domain) = 0     <-- the domain the DACR must mark client  (witness, §2a)
bits[31:10] (pfn)  = 0x206ec8 => L2 base 0x81bb2000
```

### 0.3 Three probes agree; the flush cell (F2); the control

`_wwl1 == _ptel1 == _wkl1 == 0x81bb2001` and all three leaves are `0x841e91bb`; the apex's own
`_pmppmap = 0x81a86000` equals `_ptettbr & 0xFFFFFF00` (the identity cell). **`_ptethr` is absent on
the storm rows in this capture** (the `VME_PROT`-gated store at `entry_stubs.c:2572` did not reach
the tail) — 1015 must not depend on it; it re-reads the lineage itself. Flush: `_wfkind=0x02`,
`_wfself=1`, `_wfasid=0x02 == _wctx=0x02`, `_wfttb == _wflive == 0x81a86000`. Control (W-SYNC,
`entry_stubs.c:3125`) came back SOUND: `_wck=1 _wcl1=0x80a08001 _wcl2pa=0x80a08000 _wcleaf=0x8008021a
_wcapx=1 _wcoow=0`.

### 0.4 The tail is NOT uniform (a by-value finding 1014's summary omits)

| `_pcn` | `_pc` | `_pcfar` | `_abtcpsr` | `_ptel2` | `_ptel1` |
|---|---|---|---|---|---|
| `0x400`..`0x40000` (9 rows) | `0x2bed61a0` | `0x**000e6000**` | `0x60000030` USER | `0x841e91bb` | `0x81bb2001` |
| `0x80000` | `0x38413ad0` | `0x3a1141e8` | `0x00000010` USER | `0x842461bb` | `0x842c5001` |
| `0x100000` | `0x3840ea3e` | `0x3a0138e0` | `0x60000030` USER | `0x841a71bb` | `0x841c2001` |

The last two are inside dyld's own image (`far` ≈ `0x3a0xxxxx`) — a *different* process from the
1013-era dyld (`pc=0x2be…`). Still FS=15 WnR=1 USER, but **not the storm's `far=0xe6000`**. 1015's
verdicts key on `_far == 0xe6000` **and** the tail ordinal, never a whole-run aggregate
([[mi4-measurement-defects]]).

---

## 1. THE QUESTION, AND THE HYPOTHESES

> **PRIMARY (deciding, this rung).** Ask the MMU directly: **does `0xe6000` translate for a USER
> WRITE, and to which PA?** (`_wdatpar`/`_wdatparpr`, the ARMv7 AT instruction — §2a.)

* **H-G STALE_GLOBAL_TLB** (leading, sole primary — §8a). A GLOBAL translation of `0xe6000` survives
  TLBIASID and shadows the RW leaf. **Confirm:** AT-user-write **faults** (`par & 1 == 1`) — or lands
  on a PA that is not the leaf's — while the walked leaf is present and user-writable. **Kill:** AT
  translates to exactly the leaf's PA `0x841e9000` with no fault.
* **H-P PERMISSION_INPUTS** (residual). The permission inputs themselves are wrong — domain/DACR
  (`_wddom`, `_wddacr`), TTBCR routing (`_wdttbcr`), the wrong pmap/table (`_wdthrmap`, `_wdttbr0`),
  or the leaf is not what 1014 decoded. **All are CORRECT-looking in 1014** and ruled out by
  `docs/1015-perm-static.md` Items 1-5 (§8a), so this is measured as a **residual null**, not a live
  rival: it survives only in the form "an input moved between the walk and the fault", which the
  same-instant cells of §2a close.
* **H-X STATE_OUTSIDE_THE_PAGE_TABLES.** Every table input is right and off-table state explains the
  refusal — SCTLR/PRRR (§2a's `_wdsctlr`; perm-static's ranked cause #2) or a domain the L1 does not
  name. Next rung reads the CPU's own state at vector time, never another table read.

**Ranking — H-G is a SOLE primary.** It is the only candidate that explains all three of 1014's cells
*at once* — writable leaf, ASID-matching flush that ran, **and** a recurring fault — and the two
by-value facts in the header (never-`nG`, ASID-only steady-state flush) make it mechanically
available on this port. Its two strongest rivals are **already dead by forensics**, not by hope:
`docs/1015-perm-static.md` **rules out C1 (domain/DACR)** and **C2 (pmap identity)** by value — §8a.
H-P survives as this arm's residual half (§2a/§3b) and as the ruler that says *which* input moved if
one did. The one thing 1015 alone **cannot** do is watch the storm stop when the global entry is
evicted — that is 1015b's read-out (§2b/§3c).

---

## 2. WHAT 1015 MEASURES — ENTRY-ONLY, NO KERNEL CHANGE

### 2a. THE MEASUREMENT: `STAGE90_XNU_PERM_WITNESS` (all of 1015)

Read in the abort context (`__wrap_sleh_abort`, `entry_trace.c:5732`) **before** the handler, so the
values describe the fault as taken. **No new walker** — the L1 and leaf come from the ONE walker
`stage90_read_l2_pte` (`entry_stubs.c:2293`). **No kernel source is touched**; every cell is an
entry-side read. All keys grep to **0** tree-wide.

| key | width | value | read at (file:line) | in |
|---|---|---|---|---|
| `xnu_live_abt_wdn` | 32 | the ladder ordinal (opens the row) | — | recorder |
| `xnu_live_abt_wdva` | 32 | the abort's `far` this row is keyed on | `g_abtpte_curva` | recorder |
| `xnu_live_abt_**leafng**` | 32 | **(T1) the leaf's bit11 — GLOBAL vs ASID-tagged, published RAW** | `(leaf >> 11) & 1u`, the W-WALK leaf read (`entry_stubs.c:3043`) | recorder |
| `xnu_live_abt_wdempty` | 32 | 1 iff the walk returned 0 (present-test refused) | `entry_stubs.c:2417` | recorder |
| `xnu_live_abt_wddacr` | 32 | **DACR raw at the abort** — `mrc p15,0,Rt,c3,c0,0` | new cp15 read (spelling at `entry_stubs.c:3824`) | recorder |
| `xnu_live_abt_wddom` | 32 | the L1 descriptor's domain field `(l1>>5)&0xF` | `out_l1_desc`, `entry_stubs.c:2380` | recorder |
| `xnu_live_abt_wdttbcr` | 32 | **TTBCR raw at the abort** — `mrc p15,0,Rt,c2,c0,2` | new read (`proc_reg.h:511`) | recorder |
| `xnu_live_abt_wdsplit` | 32 | `1u << (32u - (ttbcr&7u))`; `n==0` ⇒ published 0 | derived | recorder |
| `xnu_live_abt_wdttbr0` | 32 | live TTBR0 raw (`c2,c0,0`) | as `entry_stubs.c:2308` | recorder |
| `xnu_live_abt_wdttbr1` | 32 | live TTBR1 raw (`c2,c0,1`) | as `entry_stubs.c:2333` | recorder |
| `xnu_live_abt_wdl1` | 32 | the raw L1 word (pairs with `_ptel1`/`_wkl1`) | walk out-param | recorder |
| `xnu_live_abt_wdleaf` | 32 | the leaf from **ONE read** — all decodes below come from this local | walk return | recorder |
| `xnu_live_abt_wdap` | 32 | `(leaf>>4)&3u` | derived | recorder |
| `xnu_live_abt_wdapx` | 32 | `(leaf>>9)&1u` | derived | recorder |
| `xnu_live_abt_wdtex` | 32 | `(leaf>>6)&7u` | derived | recorder |
| `xnu_live_abt_wdcb` | 32 | `(leaf>>2)&3u` | derived | recorder |
| `xnu_live_abt_wdsmall` | 32 | `(leaf>>1)&1u` | derived | recorder |
| `xnu_live_abt_wdpcpu` | 32 | TPIDRPRW at the abort | `entry_trace.c:5754` reads the same register | recorder |
| `xnu_live_abt_wdthrmap` | 32 | the aborting thread's own pmap L1 base: TPIDRPRW → `thread+STAGE90_ACT_MAP` (`entry_saved_state.h:102`) → `+STAGE90_MAP_PMAP` (`:103`) → `+STAGE90_PMAP_L1_PHYS` (`:115`) | the 1011 chain, `entry_stubs.c:2487` | recorder |
| `xnu_live_abt_wdmapok` | 32 | **1 iff every deref landed in `[0x80000000, 0xFFFEFFFF]`** — the lineage's own null control | window tests at `entry_stubs.c:2493-2500` | recorder |
| `xnu_live_abt_wdseen` | 32 | **1 iff the cells were actually produced** (the `_wfseen` shape, `entry_stubs.c:3308`) | set at publish | recorder |
| `xnu_live_abt_wdctl` | 32 | **THE NULL CONTROL**: the control VA's domain + DACR bits at the SAME abort | beside W-SYNC's `_wcva` (`entry_stubs.c:3153`, published `:3170`) | recorder |
| `xnu_live_abt_wdctlok` | 32 | **1 iff the control read was taken**; **0 ⇒ withhold every passive verdict** | — | recorder |
| `xnu_live_abt_wdsctlr` | 32 | **SCTLR raw AT THE ABORT** — `mrc p15,0,Rt,c1,c0,0` (spelling `entry_stubs.c:4164` reads a latched one; the storm's own is what is owed — perm-static cause #2) | new read | recorder |
| `xnu_live_abt_**wdatpar**` | 32 | **THE HARDWARE'S OWN ANSWER (K6/T-AT)**: `mcr p15,0,cura,c7,c8,3` (`va2pa_uw_ns`, `proc_reg.h:547` = AT, stage-1, **user write**) then `mrc p15,0,Rt,c7,c4,0` (`par`, `proc_reg.h:522`). Published **RAW** | derived: `par` | recorder |
| `xnu_live_abt_**wdatparpr**` | 32 | **the privileged cross-check (K7)**: same with `c7,c8,0` (`va2pa_pr_ns`, `proc_reg.h:544`), **on the STORM VA** | derived: `par` | recorder |
| `xnu_live_abt_**wdctlat**` | 32 | **the T-CTL NULL CONTROL, its OWN key** (ruling 2026-10-15): the same privileged `AT` (`c7,c8,0`) issued on the **W-SYNC control VA `_wcva`** (PA `0x80080000`), NOT on the storm VA. Published RAW so the reader can audit the control itself instead of trusting `_wdatok`'s one bit | derived: `par` | recorder |
| `xnu_live_abt_**wdatok**` | 32 | **1 iff the three AT/PAR pairs were executed and published AND the control passed T-CTL** — **0 ⇒ NO V-AT VERDICT** | set at publish | recorder |

**The AT cell is the strongest instrument in this design, and it is the tree's OWN idiom.** `pmap_extract`
(`pmap.c:2930`) already does exactly this: `armreg_va2pa_ur_ns_write(virt)` (`:3041`), `"xisb sy"`
(`:2432`, the ISB the AT/`PAR` sequence requires), `par = armreg_par_read()` (`:3051`),
`if (!(par & 1)) { uint32_t pa = par & L2_ADDR_MASK; ... }` (`:3056-3057`) — **`par & 1 == 1` is a
FAULT, and `par & 0xfffff000` is the PA the MMU itself uses.** Three call sites in the same file
(`:2425/:2498/:3039`) establish the sequence, so the probe copies a proven pattern rather than
inventing one. Our recorder uses the raw `mrc`/`mcr p15, 0, ...` spelling that `entry_stubs.c`
already uses for every other cp15 read (`:2308`, `:3526`).

**Why this beats a walker.** Every other witness here asks *the page tables* what should happen. AT
asks **the MMU what does happen** — it consults the TLB and the tables exactly as the faulting store
did, with the faulting thread's TTBR0/ASID still live (the recorder runs inside `__sleh_abort`, before
any context switch). If AT-user-write faults while the walked leaf is
`ap=0b11/apx=0/domain=0/nG=0`, then the translation the hardware uses **is not that leaf** — a
by-hardware confirmation of H-G, not an inference. **This is what makes 1015 decisive without a
behaviour change, and hence why the active half could be deferred (§2b).**

**(T2) is ALREADY MEASURED — no new flush probe.** `xnu_live_abt_wfkind` (1014, `pmap.c`'s probe,
`entry_stubs.c:3275`) is 0=none / 1=full TLBIALL / 2=ASID-scoped. 1015 asserts it is **2** at the
storm (F2, 1014's measured value) and re-uses it unchanged. The 1014 arm's probe stays; 1015 adds
no second flush witness.

**(T3) — the honest discriminator is the AT instruction, not a poke.** The earlier draft made T3 "take
a full flush and see if the fault recurs" — a *behaviour* read, and the reason the active half
existed. **AT replaces it with a direct question to the MMU** (`_wdatpar`, T3). There is no
"read the TLB" instruction, so AT is the closest thing the architecture offers: it performs the
translation as if it were the access, in the same context, and reports fault-or-PA. **The T3 cell is
`_wdatpar`; §3a is its table.**

**P5 (WnR/type/mode) already exists** — `xnu_live_abt_fsr` (`0x0000080f`: bit11 WnR = **1**; status
`0b01111` = `FAULT_PERM_P 0x0f`, `proc_reg.h:405`), `xnu_live_abt_type` (`4`),
`xnu_live_abt_abtcpsr` (`0x60000030`; `&0x1f = 0x10` = USER) — all published at the same ordinal as
`_pcn` (`entry_stubs.c:2048-2052`). No duplicate is added.

### 2b. DEFERRED TO 1015b — `STAGE90_XNU_USER_FULL_FLUSH` (NOT part of this rung)

**Kept here in full so 1015b is a design, not a re-think; nothing in this section is built or armed
by 1015.**

**The change.** In `pmap_flush_tlbs`, inside `if (flush_self)` (`pmap.c:1343-1352`), when the switch
is on **and `pmap != kernel_pmap`**, take the **full** arm instead of the ASID arm:

```c
1343    if (flush_self) {                                  /* current */
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

guarded `defined(STAGE90_XNU_USER_FULL_FLUSH) && STAGE90_XNU_USER_FULL_FLUSH && pmap != kernel_pmap`
⇒ `arm_tlb_flushID()` (TLBIALL `c8,c7,0`, `cpufunc-v7.s:82`), leaving the kernel pmap's behavior and
the `flush_self` decision **byte-identical**. **The `flush_self` gate stays** — 1014 measured it TRUE
at the storm (`_wfself=1`), so the widened arm is reached; if it had been FALSE no flush runs at all
and the experiment would be vacuous (§3c's V-G3).

**Why it needs its own rung (the lead's rationale, recorded).** It is a **behaviour change** — the
first the project has made since 986 — and widening the flush **compiles out the ASID arm**, which
breaches the build's `TLBIASID >= 2` object assertion; and one press cannot both change behaviour and
attribute the change. 1015b's read-out is *the storm stopping*, as the sole question.

**Read-out (1015b).** `_pcn` (the abort ordinal, `entry_stubs.c:2048`) reaching ~2^20 with
`_pcfar == 0xe6000` is the 1014 shape; the storm "stopped" iff the tail no longer carries
`far=0xe6000` rows at high ordinals (§3c's V-G1/V-G2 define this precisely).

**Where it rides (1015b).** `pmap.c` is a **kernel-pool** file
(`out/xnu_kernel_obj_d13/osfmk_arm_pmap.o`), and `pmap_flush_tlbs`'s 13 call sites are all
**intra-object** (1014 measured this), so a `--wrap` cannot fire. So the switch goes to
`tools/build_xnu_arm_kernel.sh`'s `DEFINES` (where 1012's `WRSTRIP`/1014's `FLUSH_WITNESS` go) and
the tree text is staged by **extending `tools/patch_d13_flushwitness.py`** with a second,
independently-gated insertion (its own marker, `1015b: USERFULLFLUSH`) — same idempotence /
whitespace-refusal / `--check` discipline.

**Why 1015b is still worth running after 1015.** 1015 confirms *that* the translation is not the leaf;
1015b would show *that removing the global entry removes the fault*, which is the evidence a fix
needs — and it is the only way to distinguish "a stale TLB entry" from "a translation the CPU
re-derives wrongly", which AT alone cannot separate (§5.6).

---

## 3. PRE-REGISTERED VERDICT TABLES

### 3a. PRIMARY — the AT/PAR confirmation (the deciding pass; no press-behaviour needed)

> **Citation note (2026-10-15).** This table was §3c before the ENTRY-ONLY revision renumbered the
> section (the active half moved out to §3c/§2b). `tools/check_1015_capture.py` and
> `docs/1015-harness.md` still cite it as **"design §3c"** — same table, old number. The key names
> they read (`_wdatpar`, `_wdatparpr`, `_wdctlat`, `_wdatok`, `_wdsctlr`) are the ones this section
> defines and are unaffected.

Read on tail rows with `_wdn >= 0x400` and `_wdva == 0xe6000`, with `_wdseen=1` and `_wdatok=1`.

**The instrument's own null control — and it gates everything below.** On each row the recorder also
issues the SAME AT pair against the W-SYNC control VA (`entry_stubs.c:3153`), whose leaf 1014 already
read as present at PA `0x80080000` (`_wcleaf=0x8008021a`, `_wcl1=0x80a08001`, `_wck=1`). The
control's **privileged** AT is published **on its OWN key, `_wdctlat`** (the lead's 2026-10-15 ruling
— the first draft reused `_wdatparpr` for it, giving one key two definitions; see the note below) and
must return `par & 1 == 0` with `par & 0xfffff000 == 0x80080000`; if it does not, the AT instrument
itself is broken (bad ISB, wrong register, wrong VA register) and **no V-AT verdict may be read**. A
missing or mis-executed read can never impersonate a confirmation. The recorder folds the same fact
into `_wdatok` (T-CTL: control unsatisfied ⇒ `_wdatok=0`); the reader checks `_wdctlat` **directly**
as well, because a gate published as a single bit is not auditable.

> **One key, two definitions — reported and fixed.** `_wdatparpr` is the STORM VA's privileged read
> (the table's middle column, and the only thing that separates V-AT1 from V-AT4). The control VA's AT
> is a DIFFERENT reading of a DIFFERENT address and MUST NOT share the key; it is `_wdctlat`. A row
> whose `_wdatparpr` still carries PA `0x80080000` is a recorder that has not made the change; the
> reader withholds (V-AT0) and names the collision rather than reading a control value as a storm
> value.

| # | `_wdatpar` (user write) | `_wdatparpr` (priv. read) | verdict | what it means |
|---|---|---|---|---|
| **V-AT0** | — | — | **AT_INSTRUMENT_BROKEN** — the control read (`_wdctlat`) did not come back as above, or `_wdctlat` is unpublished, or `_wdatok=0` | withhold V-AT1..4. (The `SILENCE_IS_A_READING` rule: the AT's absence is a reading only because the control proves success is silent-but-readable.) |
| **V-AT1** | `par & 1 == 1` (FAULTS) | `par & 1 == 0` | **H-G CONFIRMED AT THE HARDWARE — the translation is not the user leaf** | the MMU will not translate `0xe6000` for a user write even though the walked leaf is `ap=0b11`, `apx=0`, `domain=0`, `nG=0`. The privileged view succeeds ⇒ this is a **user-AP decision made from a translation that is not the leaf the walk reads** ⇒ a stale/other entry. With `_leafng==0` (§3b note) the culprit is a GLOBAL entry surviving TLBIASID. |
| **V-AT2** | `par & 1 == 0` and `(par & 0xfffff000) == 0x841e9000` | `par & 1 == 0` | **TRANSLATION IS CORRECT YET THE STORE FAULTED** | the hardware translates the write to exactly the leaf's PA. The store's FS=15 is therefore **not** produced by this translation ⇒ the probe's key/ordering (`g_abtpte_curva`) or the DFSR/DFAR pair describes a different access. **Not a page-table defect** — hand back to the observer. |
| **V-AT3** | `par & 1 == 0` and `(par & 0xfffff000) != 0x841e9000` | `par & 1 == 0` | **WRONG TRANSLATION IN THE TLB** | the MMU translates the VA successfully but to a PA that is not the leaf's ⇒ a TLB entry is serving a translation the tables no longer hold. Same conclusion as V-AT1, reached from the PA side, and it names the **stale PA** by value. |
| **V-AT4** | `par & 1 == 1` | `par & 1 == 1` | **BOTH VIEWS FAULT** | the user-AP story is not the whole story (the privileged view is refused too) ⇒ the entry is not merely user-denied; a `domain`/`invalid` mechanism is in play. Re-rank toward perm-static cause #2 (SCTLR/PRRR) before §7. |

**Why this table can see the stale entry at all.** The ARMv7 AT instruction performs the translation
"as if" it were the access, and on this core it **may be satisfied from TLB entries** — that is not a
flaw in the instrument, it is the whole point: it asks the same translation path the faulting store
used, in the same thread context (`__sleh_abort`, before any switch), so a stale TLB entry that
shadows the leaf will shadow AT too. **The confirmation is the *disagreement*** between the walker
(§2a: leaf present + user-RW) and AT (V-AT1/V-AT3: the hardware refuses or lands elsewhere) — neither
reading alone is a verdict, and V-AT2 is the cell that catches the case where they agree and the
fault is therefore the observer's.

**What V-AT alone decides.** V-AT1 or V-AT3 ⇒ **H-G confirmed** and §7's fix is indicated; V-AT2 ⇒
**H-G refuted** and §3b's residue (P-X1) takes over; V-AT4 ⇒ re-rank to H-X. **No behavioural
read-out and no second press is required for any of these** — which is exactly the lead's reason (1)
for making this rung entry-only.

### 3b. THE PASSIVE LADDER (same press; the residual and the null controls)

Read on the same tail rows. Withheld unless `_wdctlok=1`, `_wdseen=1`, and (for the lineage cells)
`_wdmapok=1`.

| # | condition | verdict |
|---|---|---|
| **P-CTL** | `_wdctlok=0` or `_wdseen=0` | **NO_PERM_WITNESS** — rebuild; no verdict |
| **P-AT** | `_wdatok=0` | **NO_AT** — the deciding table (§3a) is unavailable; the rung cannot conclude, regardless of everything below |
| **P-D1** | `_wddom != 0` | **L1_DOMAIN_NONZERO** — the faulting L1 names a nonzero domain |
| **P-D2** | `_wddom == 0` and `(_wddacr & 3u) != 1u` | **DACR_NOT_CLIENT** — H-P CONFIRMED (the only DACR writer is `src/mmu.c:4921`) |
| **P-D3** | `_wddom == 0` and `(_wddacr & 3u) == 1u` | **DACR_CLIENT** — H-P's domain form KILLED |
| **P-T1** | `(_wdttbcr & 7u) != 2u` | **TTBCR_NOT_N2** — every `entry_ttbr_base` derivation (`entry_stubs.c:2150`) must be re-read |
| **P-T2** | `(_wdttbcr&7u)==2u` and `_wdva >= _wdsplit` | **ROUTED_TO_TTBR1** |
| **P-T3** | `_wdttbr0` (masked `2^(14-n)`) != `0x81a86000` | **TTBR0_MOVED_AT_ABORT** |
| **P-M1** | `_wdmapok=1` and `_wdthrmap` != `_wdttbr0` | **THREAD_PMAP_NE_TTBR0** — the aborting thread's pmap is not the walked table |
| **P-L1** | `_wdsmall=1` and (`_wdapx!=0` or `_wdap!=0b11`) | **LEAF_NOT_USER_WRITABLE** — contradicts 1014's stable double-read |
| **P-L3** | `_wdsmall=0` | **NOT_A_SMALL_PAGE** |
| **P-S1** | `(_wdsctlr` bits `TRE(28)`/`AFE(1)`/`M(0)`) differs from the console-install reading | **SCTLR_MOVED** — H-X is live; read the CPU's own state next |
| **P-X1** | all of P-D3/P-T1..P-T3/P-M1/P-L1/P-L3/P-S1 killed **and** V-AT2 | **INPUTS_ALL_CORRECT_STILL_FAULTS** — the translation is right and the store still faults; the next rung is the observer, not the pmap |

**A note on `_leafng` in this table.** `_leafng == 0` (GLOBAL) is **not** by itself a verdict — it is
the *precondition* of H-G, and it is published so the reader can see it without decoding the packed
leaf. `_leafng == 1` (non-global) would **kill H-G by value** before any press: if the leaf were
ASID-tagged, TLBIASID *would* evict its stale entry. So the checker asserts `_leafng == 0` at the
storm as the H-G precondition (the 1014 leaf's bit11 is 0, §0.1).

### 3c. 1015b (DEFERRED) — the storm-stops table for `STAGE90_XNU_USER_FULL_FLUSH`

**Not part of 1015.** These cells decide the *deferred* rung, whose sole question is whether widening
the flush removes the fault. Recorded here so 1015b is pre-registered and so §3d can name what 1015
cannot reach.

The tail is read as: rows with `_pcn >= 0x400`; the storm exists iff any of them has
`_pcfar == 0x000e6000`. "Stopped" is a **definition**, not an eyeball: **fewer than 3 tail rows carry
`far=0xe6000`, AND no `far=0xe6000` row appears at `_pcn >= 0x4000`** (the 1014 storm's own rows run
to `0x40000`).

| # | reading on the 1015b arm | verdict | what it means |
|---|---|---|---|
| **V-G1** | `_pcfar=0xe6000` rows **STOP** at low ordinal, and `_wfkind == 1` is measured for the storm's pmap | **H-G CONFIRMED IN BEHAVIOUR** | the widened TLBIALL evicted the stale entry; the current RW leaf then served the store. The fix is §7. |
| **V-G2** | the storm PERSISTS (`far=0xe6000` at `_pcn >= 0x4000`) with `_wfkind == 1` | **H-G REFUTED IN BEHAVIOUR** (or the entry is not reachable from this flush) | a full TLB invalidate ran for this pmap and the store still faults ⇒ §3b's P-X1 / H-X. |
| **V-G3** | `_wfseen = 0` or `_wfkind = 0` on the storm's row | **EXPERIMENT VACUOUS** — no flush reached the widened arm (the `:1298` gate was FALSE) | the poke never happened; **no verdict** — rebuild. |
| **V-G4** | the storm MOVES (`far` changes, or a *different* FS=15 VA appears at high `_pcn`) | **SYMPTOM_MOVED, NOT FIXED** | the invalidate changed the fault's address ⇒ the entry was TLB-resident but there are siblings; enumerate them. |
| **V-G5** | the run no longer reaches the storm at all (panics or stops earlier, no `_pcfar`) | **MEASUREMENT LOST** | the widened flush broke a different path ⇒ rebuild with the flush scoped **narrower** (§7's alternative) rather than concluding. |

**The NULL CONTROL for this table** is V-G3: a row where the widened arm was never reached
(`_wfkind != 1`) **cannot** be read as V-G2. 1015b refuses to conclude "not stale TLB" unless a
`_wfkind == 1` row is the *dominant* tail row for this VA — "the flush was refused, so of course it
still faults" is a different sentence from "a full flush ran and it still faults".

### 3d. THE STITCH — what 1015 decides, and what it hands to 1015b

| V-AT (this rung, hardware) | V-G (1015b, deferred behaviour) | combined | action |
|---|---|---|---|
| V-AT1 / V-AT3 (not the leaf) | *not run* (**1015 alone**) | **H-G CONFIRMED AT THE HARDWARE — 1015's verdict** | take §7(b) — and run 1015b to show the fault leaves; §7(a) is the principled follow-on |
| V-AT1 / V-AT3 | V-G1 (**storm STOPS**) | **CONFIRMED, CONSISTENT** — the strongest cell pair in the family | take §7(b) |
| V-AT1 / V-AT3 | V-G2 (persists) | **CONFIRMED-TRANSLATION, UNFIXED-BY-ONE-FLUSH** | the stale entry is real but a *global* invalidate at `pmap_flush_tlbs` does not reach it (wrong moment / another CPU / a second entry). Enumerate the entries; do not discard H-G |
| V-AT2 (translation correct) | *not run* | **H-G REFUTED — 1015's verdict** | go to §3b's P-X1 → H-X (SCTLR/PRRR, measured as `_wdsctlr`) |
| V-AT2 | V-G1 | **CONTRADICTION** — the hardware translated it correctly yet the storm stopped when the flush widened | the stop was caused by something other than this translation; re-read the observer before trusting either |
| V-AT0 | any | **NO VERDICT** | the AT never ran; rebuild. Nothing else in the rung can carry the conclusion |

---

## 4. HOW 1015 RIDES THE EXISTING SWITCH SET (ENTRY-ONLY)

**Inherited VERBATIM — the 1014 set** (`scripts/build_1014_v1.sh`): entry side `ENTRY_TRACE` plus
`STAGE90_XNU_{ABT_SPAN,ABT_FAR,ABT_PC,ABT_PTE,VME_PROT,PMP_ENTER,WRSTRIP,PTE_WITNESS,FLUSH_WITNESS}`,
arm environment `STAGE90_XNU_{HFS,SP_ALIGN,VMHINT_GUARD,SUBMAP_PMAP_SKIP,COMMPAGE_ALIAS,IOS7_TRAPS,
DYLD_WITNESS,SHREG_PROBE,SHREG_WIRE,SHREG_GEOM,RESIDENT,USVC,STALL,CADENCE,KEXTB_LOGIN,ABORT_TRACE,
FREEZE_WITNESS}=1`.

**ONE new switch — `STAGE90_XNU_PERM_WITNESS`** (default 0) — **entry-toolchain only**, no
`--wrap`, no pool edit, no kernel source touched. Consumers: `entry_stubs.c` (the recorder) and
`entry_trace.c` (the call). Guard = the full conjunction
`ABT_FAR && ABT_PC && ABT_PTE && PMP_ENTER && PTE_WITNESS && PERM_WITNESS` (it keys on
`g_abtpte_curva`, which only `PTE_WITNESS` writes — `build_entry.sh:1431`'s lesson).

**DEFERRED with §2b:** `STAGE90_XNU_USER_FULL_FLUSH` and its whole kernel-pool plumbing —
`tools/build_xnu_arm_kernel.sh` `DEFINES`, the `tools/patch_d13_flushwitness.py` extension, the
`osfmk_arm_pmap.o` literal assertion, and the refusal chain (`0|1` only; `USER_FULL_FLUSH=1` requires
`FLUSH_WITNESS=1` and the full 1010 ladder). **None of it is part of 1015.** It is listed here only
so 1015b inherits a plumbing spec rather than re-deriving one.

**The five plumbing sites for the ONE live switch — all, or the press preflight REFUSES the arm:**

1. **`src/entry/entry_stubs.c`** — the `wd*` recorder; offsets from `entry_saved_state.h:102-115`,
   never a literal.
2. **`src/entry/entry_trace.c`** — the extern + ONE call in `__wrap_sleh_abort`, after
   `entry_note_abt_cur(far_)` (`:5774`), beside `entry_note_abt_wctx(far_)` (`:5784`), **before**
   `__real_sleh_abort`.
3. **`src/entry/build_entry.sh`** — (a) `ENTRY_ARM_KEYS` (append after `:1978`); (b) the record
   `case` arm (beside `:2249`); (c) an **explicit `echo "STAGE90_XNU_PERM_WITNESS=$PERMWIT"`** in the
   record writer (beside `:39978` — the one site a name-keyed diff does not visit and the only site
   the gate reads); (d) `STUB_DEFINES` + `STUB_DEFINES_TRACE` (`:334`, `:1392` precedent).
4. **`scripts/preflight_boot_check.sh`** — add it to **`ENTRY_CFG_KEYS`** (list at `:716-759`); the
   converse check (`:1181-1183`) refuses a record carrying a key the gate does not print.
5. **`scripts/build_1015.sh`** (new; shaped on `build_1014_v1.sh`) — `EXPORTS+=("STAGE90_XNU_PERM_
   WITNESS=1")` **and no kernel-switch export**; a stale-arm refusal naming `armed-d13-29ec9e48`
   (`build_1013.sh:607-618`); and the assertions: image-wide **`(b|bl)`** count to `entry_note_abt_wd`
   == 1 (**`(b|bl)`, never `bl`** — the 1013 tail-call reader defect); each published `wd*` expression
   contains `_wdleaf` (single-read, the 1013 V6 rule); **OFF byte-identity** for the entry objects
   (rebuild-with-0 + `cmp`, `build_1013.sh:1938-1939`); **plus a POSITIVE assertion that the kernel
   pool is untouched** — `out/xnu_kernel_obj_d13/osfmk_arm_pmap.o` must be **byte-identical** to the
   1014 arm's, since this rung leaves kernel source alone and that is a claim worth proving, not
   assuming; and a **negative control**: the frozen 1014 elf
   (`out/stage90/frozen/armed-d13-29ec9e48/xnu_arm_entry.elf`) carries **zero** `wd*`/`leafng`
   literals and **zero** `entry_note_abt_wd`.

**Also owed (1014 §6), and free here:** `entry_note_abt_wleaf` (`:2928`) publishes `_wwnoproof` from a
`pm_l1_phys` re-read **after** the enter (`:2930`, the deliberate `pmap_expand_ttb` hazard). Publish
both pre- and post-call bases (`_wwpre`/`_wwpost`) so `_wwnoproof` is a reading, not a bare 1.

---

## 5. FALSIFICATION — what proves this rung wrong, and where it falls back

1. **One question, one rung.** 1015 changes **nothing** in the kernel: every `wd*` cell is an
   entry-side read, so AT's answer is attributable to the device and not to this arm. The behaviour
   question ("does removing the global entry remove the fault?") is **1015b's**, deliberately
   separated — arming both in one build would make neither attributable
   ([[mi4-one-value-two-definitions]]).
2. **H-G is refuted by V-AT2** (the hardware translates the write to the leaf's own PA `0x841e9000`)
   — and the residual ladder (§3b) is measured in the same capture, so the fallback costs no press.
3. **H-G dies before the press if `_leafng==1`** — a non-global leaf *would* be evicted by TLBIASID.
   1014's leaf is `bit11=0`, so this is a documented precondition, not a hope.
4. **V-AT0 makes the confirmation vacuous**: if the AT pair did not run (`_wdatok=0`) or the control
   read did not come back as expected, the reading is "the instrument never fired", **not** "stale TLB
   is refuted". The table refuses the conclusion in that case — the 1013 `WITNESS_METHOD_BROKEN`
   discipline, and the reason §3a's control read gates every V-AT row.
5. **If V-AT1/V-AT3 fires**, H-G is confirmed **for this VA on this run**; it does **not** by itself
   say the mechanism is the *only* cause, nor that a global-entry eviction is the fix — that is
   §7 + 1015b.
6. **The honest gap.** There is **no "read the TLB" instruction**: AT reports the *result* of a
   translation, not its provenance, so a refusal cannot by itself distinguish "a stale entry
   shadows the leaf" from "a TLB entry the CPU re-derived for a reason the tables do not record".
   V-AT2-vs-V-AT1 separates "the leaf is used" from "it is not"; only 1015b's behaviour read-out
   separates "removing the global entry helps" from "it does not". **This is the one thing 1015
   cannot close, and the design says so rather than implying otherwise** — the lead's reason (1)
   makes 1015 *decisive for the diagnosis*, not for the fix.

---

## 6. HONEST SCOPE

* **1015 changes no kernel behaviour.** It is a pure measurement, in the same class as 1014 — an
  entry-side recorder. The first behaviour change since 986 is **deferred to 1015b** (§2b).
* **It decides the diagnosis, not the fix.** V-AT1/V-AT3 says "the CPU's translation is not the walked
  leaf"; whether the right long-term fix is (a) `nG` tagging or (b) the widened flush is §7's
  trade-off, and whether either actually removes the fault is 1015b's read-out.
* **It does not settle §0.4's three interleaved faults**; the verdicts are about `far=0xe6000`.
* **It cannot read the hardware's abort status** (the `fsr` is Apple's `arm_ctx->fsr`, `trap.c:537`;
  `sleh_abort` never re-reads `c5,c0,0`). P-D2 catches a domain abort by *consequence*, not by
  reading the register.
* **AT reportability is assumed, not yet measured on this device.** `pmap_extract` uses the AT path
  (`pmap.c:2930/:3041/:3051`), but whether it is *reached* on D13 was not verified this session —
  which is exactly what §3a's control read (`_wdctlat` on the W-SYNC VA) exists to establish
  **before** any V-AT verdict is read. If the control fails, the rung reports AT_INSTRUMENT_BROKEN
  and 1015 falls back to §3b's ladder alone.

---

## 7. CANDIDATE FIX TO PRE-SPECIFY (NOT built in 1015)

If V-AT1 or V-AT3 holds, the fix is one of two, and 1015 does **not** implement the choice — it
reports which V-AT cell fired and leaves the selection open. **(b) is also 1015b's experiment**, so
1015b would double as its validation.

**(a) Emit `L2_NG_BIT` on every user leaf** — add `L2_NG_BIT` to the user branch of
`pmap_vm_prot_to_page_flags` (`pmap.c:1113`, guarded `pmap != kernel_pmap`), and to the other user
OR sites (`:3302`/`:3494`, both guarded by `BOARD_CONFIG_* || STAGE90_XNU_COMMPAGE_ALIAS` and needing
their own review). Then every user leaf is ASID-tagged and TLBIASID evicts it.

* **For:** targets the cause (the never-emitted bit) rather than papering over it; keeps TLBIASID's
  cheapness; matches what ARM expects for per-process mappings.
* **Against (wider blast radius):** it changes the *lifetime semantics* of every user mapping and
  interacts with ASID allocation/reuse — a recycled ASID with stale non-global entries is a new
  failure class (`pmap_asid.c` would need auditing). It also does not retire entries already GLOBAL
  in the live TLB.

**(b) Make `pmap_flush_tlbs` take `arm_tlb_flushID()` for non-kernel pmaps** — the `STAGE90_XNU_
USER_FULL_FLUSH` branch of §2b, made permanent.

* **For:** one branch, one gate, no change to PTE encoding or ASID semantics; it is exactly the
  experiment 1015b runs, so confirmation and fix are the same code. It also sweeps *any* stale global
  entry, including ones already in the TLB.
* **Against:** a full TLBIALL on every user page-table update is coarser (performance), and it
  invalidates kernel-visible global translations too (the whole TLB, not just this pmap's ASID); and
  it compiles out the ASID arm.

**Recommendation (stated, not decided): (b) is the more conservative first fix** — it is the smaller
diff, it changes no encoding, and it is the experiment 1015b already pre-registers. If measurements
then show the cost, (a) is the principled follow-on. **Neither is built in 1015.**

---

## 8. APPENDIX — WHAT `docs/1015-perm-static.md` AND THE ENTRY-ONLY DECISION CHANGED

*(doc-only; both landed after the first draft. This section records the fold-in, so a reader of the
design and a reader of the forensics doc see the same cause list and the same rung boundary.)*

### 8a. C1 and C2 are RULED OUT — H-G becomes the SOLE primary

`docs/1015-perm-static.md` adjudicates the 1014 measurement item by item and **kills the two
candidates that could have displaced H-G**, each by measurement, not argument:

* **C1 — domain/DACR — RULED OUT (its Items 1-2, "Verdict: … NOT the cause").** `ap[1:0]` on the
  storm's leaf is `0b11` = `L2_ACCESS_USER|L2_ACCESS_PRW` = **user RW**; the L1 descriptor's
  `domain:4` field (bits[8:5], `pmap.h:118`) is built by `pmap_expand` (`pmap.c:3205`) with **no
  domain bits OR'd** ⇒ domain 0; and `DACR[0] = 0b01 = CLIENT` (`locore.s:78`, the only writer of
  `c3,c0,0` in the tree). A client domain **enforces** AP — and AP here permits the write. A
  denied-by-domain fault would in any case be DFSR status `0b1011` (11), not 15.
* **C2 — the wrong pmap/table — RULED OUT (its Item 5).** `_ptethr` (the faulting thread's own map
  chain), `_ptettbr` (the live TTBR0 the walk sampled), `_pmppmap` (the pmap passed to
  `pmap_enter_options`) and `_wfttb`/`_wflive` (the pmap `pmap_flush_tlbs` compared) **all name
  `0x81a86000`** on the tail rows — four independent readers, which would each have had to lie in the
  same direction. (Note this is the `_ptethr` cell §0.3 of *this* doc reports as absent from our own
  re-parse of the capture; perm-static reads it as present on the tail rows. **The two readings are
  in direct conflict and that conflict is itself owed a check** — 1015 re-reads the lineage itself
  (`_wdthrmap`, §2a) rather than depending on either, so the rung does not rest on it.)
* Its Items 3, 7, 8 (section/page type, execution-never/per-process subtleties, FSR nuance) are
  likewise ruled out, and its Item 4 rules out TTBR routing (TTBCR.N = 2 ⇒ `0xe6000` is TTBR0's).
  **Item 6 is the survivor** — and it is H-G.

Consequence for this design: **§1's ranking is a sole primary.** H-P is no longer a co-equal
candidate; it survives only as the residual/null half of this arm (§2a/§3b) and in the one form the
forensics could not close — "an input moved between the walk and the fault", which the same-instant
cells close. **The V-AT table (§3a) exists because the forensics doc's "most likely" and this
design's H-G are the same hypothesis, and AT is the one instrument that can confirm it at the
hardware rather than by inference.**

### 8b. Correction to the 1014 record — `records/1014-press-read.txt` §1 is WRONG

The record's §1 concludes: *"the ASID-matching invalidate ran. The stale-TLB explanation (F1) is
dead."* **That is a measurement defect, and it has been corrected in the record.** F2 measured a
flush — `_wfkind=2`, `arm_tlb_flushID_ASID` (`pmap.c:1349`), = **TLBIASID** (`c8,c7,2`,
`cpufunc-v7.s:66-74`). **TLBIASID invalidates only ASID-tagged (`nG=1`) entries.** Every leaf this
port installs is **GLOBAL** (`nG` = bit 11 of `arm_l2_entry_t`, `pmap.h:137`, sourced from
`L2_NG_BIT 0x800`, `pmap.h:231` — which appears **at its definition line only, tree-wide**, never
OR'd by `pmap_vm_prot_to_page_flags` or anywhere else). So F2 **does not establish** that any
translation of `0xe6000` was invalidated. **F1 is narrowed, not eliminated** — and the record now
says so. The one-line form: *`wfkind=2` (TLBIASID) cannot evict a global entry, and this port installs
every leaf global; the elimination of stale-TLB by F2 was invalid.*

**The strongest response to that correction is not another passive walk** — it is the two cells this
rung carries: the leaf's own `nG` (`_leafng`, §2a) and **the hardware's own verdict on the
translation** (`_wdatpar`, §3a). The third cell — the storm's behaviour when the flush is widened —
is the deferred 1015b (§2b/§3c).

### 8c. What the ENTRY-ONLY decision changed in §1-§7

* **Retitled and re-centred:** the primary question is now the **AT/PAR hardware confirmation**
  (§1, §2a, §3a); §3a's V-AT0..4 cells are the **deciding pass**.
* **Deferred:** §2a's old active half (`STAGE90_XNU_USER_FULL_FLUSH`) is now **§2b**, marked DEFERRED
  to rung 1015b, with its storm-stops table now **§3c** and its kernel-pool plumbing moved out of
  §4's live list into a deferred note there. §4 now carries exactly **one** live switch.
* **Still live and unchanged:** §2a's key table (the 24 `wd*`/`leafng` names + the four AT/SCTLR
  names), §3b's ladder, §4's five plumbing sites for the one switch, §5, §6, and §7.
* **Recorded rationale** for the split is in the header (three reasons) and §2b (the behaviour-change
  and `TLBIASID >= 2`-assertion arguments).

### 8d. The seven-way name contract (as landed)

The recorder, the arm-builder, the preflight gate, the checker and this design must agree on every
key name, or a press spends itself on a name the gate never prints. **As the code stands** (verified
by `grep` this session): the 24 `wd*`/`leafng` names of §2a are published by `entry_stubs.c`
(`:3638-3675`) and read by `tools/check_1015_capture.py` (`:86-93`) and `scripts/build_1015.sh`.
**The AT rows landed under the `wd*` prefix** — `xnu_live_abt_wdatpar` (storm VA, user write),
`xnu_live_abt_wdatparpr` (storm VA, privileged read), `xnu_live_abt_wdctlat` (the W-SYNC control VA,
the T-CTL null control — its OWN key by the 2026-10-15 ruling), `xnu_live_abt_wdatok` (the gate) —
read by the checker; this doc matches that spelling. FIVE cells in all, with `xnu_live_abt_wdsctlr`.
**OWED at the time of writing** (and outside this doc's lane): the five AT/control/SCTLR names are
`xnu_live_abt_wdsctlr` are **not yet emitted by `entry_stubs.c`** (`grep` = 0 there, 0 in
`build_entry.sh`, 0 in `preflight_boot_check.sh`) even though `check_1015_capture.py` and
`build_1015.sh` already read them. **This is now on the critical path**, not a nicety: with the
active half deferred, **AT is the rung's deciding instrument**, and an unpublished `_wdatpar` means
`_wdatok=0` ⇒ V-AT0 ⇒ no verdict at all. Until the recorder publishes them and
`ENTRY_CFG_KEYS` prints them, 1015 has no deciding pass. This is a plumbing gap to close in the
entry/script lanes, not a design change.

*(Sources read by value this session: `pmap.c` `:1104-1129`, `:1343-1352`, `:2432`, `:2930`,
`:3039-3057`, `:2562-2563`, `:3296-3304`, `:3488-3496`, `:3723`; `pmap.h` `:117-142`, `:207-231`;
`pmap_asid.c` `:150-172`; `proc_reg.h` `:385-405`, `:511`, `:522`, `:544-552`, `:530`;
`cpufunc-v7.s` `:49-87`, `:166-170`; `trap.c` `:510-560`; `arm_vm_init.c` `:475`; `locore.s:78`;
`src/mmu.c:4921`; `src/entry/entry_saved_state.h` `:102-115`; `src/entry/entry_stubs.c`
`:2131-2148`, `:2281-2425`, `:2464-2510`, `:2560-2574`, `:2880-2980`, `:3024-3060`, `:3125-3180`,
`:3275-3360`, `:3638-3675`, `:3817-3934`; `src/entry/entry_trace.c` `:5690-5800`, `:5960-6030`;
`src/entry/build_entry.sh` `:334`, `:1361-1449`, `:1928-1979`, `:2246-2249`, `:39977-39978`;
`scripts/preflight_boot_check.sh` `:716-759`, `:1181-1183`; `scripts/build_1014_v1.sh` `:291-530`,
`:750-830`; `scripts/build_1013.sh` `:607-618`, `:1840-1946`; `tools/check_1015_capture.py`
`:86-93`, `:128-130`; `docs/1014-design.md`; `docs/1015-perm-static.md`; `records/1014-press-read.txt`;
capture `out/stage90/captures/recovered-unknown-arm-20261015-092217-last_kmsg.txt` `:33475-34368`.)*