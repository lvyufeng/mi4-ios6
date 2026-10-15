# 1013 probe — static audit (would a null/zero be a REAL reading or a PROBE DEFECT?)

Scope: the four recorders in `src/entry/entry_stubs.c` (W-APEX `entry_note_abt_wleaf`, W-WALK
`entry_note_abt_walk`, W-SYNC `entry_note_abt_wsync`, W-CUR `entry_note_abt_cur`) and their call sites
in `src/entry/entry_trace.c`. Every claim below is read BY VALUE from the D13 source; every value is
read from the 1012 capture `out/stage90/captures/recovered-unknown-arm-20261015-020222-last_kmsg.txt`
(the live configuration, since 1013 changes the tree but not the boot).

---

## Q1 — W-SYNC's CONTROL VA: does it read a real leaf, or a null? **(THE decisive question)**

**The code.** `entry_stubs.c:3059`:

```
    va = (uint32_t)gPhysBase + ABTWS_WINDOW_OFF;
```

with `#define ABTWS_WINDOW_OFF 0x00080000u` (`entry_stubs.c:2820`).

**Runtime `gPhysBase`.** The stand-in `unsigned long gPhysBase = 0x00200000ul;` at `entry_stubs.c:100`
is under `#ifndef STAGE90_ENTRY_REAL_ARM_INIT`. That switch is **ON** in this build
(`out/stage90/xnu_arm_entry-config.txt:8  STAGE90_ENTRY_REAL_ARM_INIT=1`), and the real XNU object owns
the symbol (`nm out/xnu_kernel_obj_d13/*arm_vm_init*.o` → `0000000c B gPhysBase`), so the stand-in is
compiled out. XNU assigns it once in `arm_vm_init` (`external/xnu-hd2-darwin13/xnu/osfmk/arm/arm_vm_init.c:331
gPhysBase = args->physBase;`). The MEASURED boot args (`recovered-…-last_kmsg.txt:3903-3904`):

```
MI4IOS6_STAGE90_XNU xnu_entry_args_physBase=0x80000000
MI4IOS6_STAGE90_XNU xnu_entry_args_memSize=0x1e400000
```

And the walk itself republishes the value it bound with, at the storm —
`xnu_live_abt_ptewbase=0x80000000` (`…:16152` and every storm row). So **`gPhysBase = 0x80000000` at
the storm**, and the control VA is

```
    va = 0x80000000 + 0x00080000 = 0x80080000
```

**Does TTBR0 answer it?** The walker picks the table by `TTBCR.N` (`entry_stubs.c:2264-2280`):

```
    n = ttbcr & 7u;
    if (n == 0u)
        base = ttbr0;
    else if (va < (1u << (32u - n)))
        base = ttbr0;                             /* the low window */
    else
        return 0u;                                /* the high window (TTBR1) - out of scope */
```

`TTBCR.N = 2` at the storm (the port programs it: `arm_vm_init.c:475 set_mmu_ttbcr(2)`, confirmed by
`pmap.c:2357` "TTBCR split means that commonpage is at 0x40000000"; confirmed by value in the capture —
the storm's `ptettbr=0x81a8605b` derives base `0x81a86000` only under the N=2 mask `0xfffff000`, which
equals the thread's own `ptethr=0x81a86000`, `…:16146/16154`). So the split is `1u << 30 = 0x40000000`.

`va = 0x80080000` is **not** `< 0x40000000` ⇒ the `else return 0u` branch (entry_stubs.c:2280) is taken
**before any table is touched**. `*out_ttbr0_used` was zeroed at `:2262` and is never set to 1
(`:2350-2351` is unreachable). `g_abtpte_walk_oow` is **not** set on this path (only `:2311`/`:2339`
set it), so `_wcoow` publishes 0, not the "out of window" flag.

**Published control row** (`entry_stubs.c:3066-3073`): `wck = u = 0`, `wcleaf = 0`, `wcoow = 0`,
`wcapx = stage90_w_apx(0) = 0`.

**What the checker does with it** (`tools/check_1013_capture.py:4902`):

```
        if r["wcoow"] == 1 or r["wck"] != 1 or r["wcapx"] != 0:
            ctl_bad.append(r)
```

`wck != 1` fires ⇒ `WIT_CONTROL_OK = False` ⇒ the short-circuit at `:5010`:

```
        WIT_CLASS = "WITNESS_METHOD_BROKEN"
```

and V1–V3 (the apex/walk leaves) are **withheld**.

**VERDICT: DEFECT.** The control VA is `0x80080000`, which is measured (gPhysBase) to lie above the
`TTBCR.N=2` split `0x40000000` — TTBR1 territory. The walker is a single-table TTBR0 instrument that
refuses every VA at/above the split (`entry_stubs.c:2280`), so W-SYNC publishes `wck=0` on **every**
ladder row. The checker's control rule (`wck != 1`) then fires **WITNESS_METHOD_BROKEN** and withholds
V1–V3. **The 1013 press, as built, cannot produce a leaf verdict.**

This is not a "may": `design §2` warned the outcome was *possible* ("the managed window is TTBR1
territory"), but it is *certain here* because the control uses `gPhysBase` (the boot's high phys==virt
base, `0x80000000`), not a low identity VA. The alias argument is a red herring: the refusal happens in
the split test (`:2277-2280`) before any alias is considered, and W-SYNC passes `allow_alias=0` anyway
(`:3062`). A control VA below `0x40000000` (e.g. a page TTBR0 actually maps for this VA range) would be
required for the control to read at all.

---

## Q2 — W-WALK's double-read (`_wkleaf2`/`_wkstable`): same address, same alias, after a barrier?

**Source** (`entry_stubs.c:2971-3005`):

```
    g_abtpte_walk_oow = 0u;
    l2 = stage90_read_l2_pte(va, &l1, &u, onladder);      /* FIRST read  */
    ...
    oow = g_abtpte_walk_oow;                              /* latched before the second read */
    ...
    __asm__ volatile ("dsb" ::: "memory");                /* :3003 - THE BARRIER */
    l2b = stage90_read_l2_pte(va, &l1b, &ub, onladder);   /* SECOND read, SAME va, SAME allow_alias */
    stable = ((l1b == l1) && (l2b == l2) && (ub == u)) ? 1u : 0u;
```

* **Same address:** both calls pass the identical `va` (the function parameter, unchanged; the only
  reassignments in the body are `l1b/ub`/`l2b` out-params, not `va`).
* **Same `allow_alias`:** both pass `onladder` (computed once at `:2962`). The comment at `:2997-3001`
  states the reason and the code matches it.
* **Barrier present:** `dsb` at `:3003`, between the two calls.
* **`_wkstable` semantics:** `1` iff all three outputs agree (`l1b==l1 && l2b==l2 && ub==u`); `0` if any
  differs. Correct — 1 only when the reads agree.
* **`_wkoow` integrity:** `g_abtpte_walk_oow` is ASSIGNED (never OR'd) by the walker (`:2311`), so the
  second walk would clobber it; the value is latched into the local `oow` at `:2980` before the second
  call and published from the local (`:3019`). Correct.

**VERDICT: SOUND.**

---

## Q3 — W-APEX's `pm_l1_phys = *(const volatile uint32_t *)pmap`: is `pm_l1_phys` offset 0?

**Struct** (`external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.h`):

```
93: #pragma pack(4)
95: typedef uint32_t paddr_t;       /* Physical address */
98: struct pmap {
99:     paddr_t pm_l1_phys;         /* L1 table address */
100:    vaddr_t pm_l1_virt;         /* L1 virtual table address */
```

`paddr_t` is `uint32_t` (`:95`), `#pragma pack(4)` (`:93`) — so `pm_l1_phys` is the first field, offset
0, 4 bytes, no initial padding. Cross-checked by Apple's own use: `pmap.c:1280
pmap_paddr_t ttb = pmap->pm_l1_phys;`.

**The read** (`entry_stubs.c:2869`): `pm_l1_phys = (pmap != 0) ? *(const volatile uint32_t *)pmap : 0u;`
— offset 0, 4-byte load, NULL-guarded.

**VERDICT: SOUND.**

---

## Q4 — W-APEX index math vs Apple's `tte_offset`/`pte_offset`.

**Apple** (`pmap.h`):
```
198: #define tte_offset(addr) (((addr >> 0x14) & 0xfff) << 2)     /* addr[31:20] * 4 */
199: #define pte_offset(addr) (((addr & ~(L1_SECT_ADDR_MASK)) >> PAGE_SHIFT) << 2)  /* addr[19:12] * 4 */
202: #define L1_PTE_ADDR_MASK 0xfffffc00 /* Bits [31:10] */
203: #define L1_SECT_ADDR_MASK 0xfff00000 /* Bits [31:20] */
```
(`L1_SECT_ADDR_MASK = 0xfff00000`, so `addr & ~L1_SECT_ADDR_MASK = addr & 0x000fffff`, and
`>> PAGE_SHIFT (12)` yields `addr[19:12]`.)

**W-APEX** (`entry_stubs.c`):
```
2898:    l1p   = pm_l1_phys + (((va >> 20) & 0xfffu) << 2);
2903:    leafp = l2pa + (((va >> 12) & 0xffu) << 2);
```
* `((va >> 20) & 0xfff) << 2` ≡ `((va >> 0x14) & 0xfff) << 2` = `tte_offset`. **Identical.**
* `((va >> 12) & 0xff) << 2` = `va[19:12] * 4`; Apple's `pte_offset` = `((va & 0xfffff) >> 12) << 2` =
  `va[19:12] * 4`. **Identical** for every `va` (the `& 0xfffff` is redundant with `>> 12 & 0xff`).
* L1→L2 base mask: W-APEX `l2pa = l1e & ABTPTE_L1_PTE_MASK` with `ABTPTE_L1_PTE_MASK 0xfffffc00`
  (`:2147`) = Apple `L1_PTE_ADDR_MASK 0xfffffc00`. **Identical.**

**For `va = 0x2e000`:** `tte_offset = (0x2e000 >> 20) & 0xfff = 0` → **L1 slot 0, byte 0x0**.
`pte_offset = (0x2e000 >> 12) & 0xff = 0x2e = 46` → **L2 slot 46, byte 0xb8 (184)**. Exactly what the
design expects for the storm VA.

**Caveat (not a defect):** Apple's `pmap_tte` indexes `pmap->pm_l1_virt` (`pmap.c:1780`, `:1786-1787`),
while W-APEX indexes `pm_l1_phys`. On this image phys==virt (`ptewbase/xnu_entry_args_physBase = virtBase
= 0x80000000`), so the word read is the same; and reading physically is the documented design (§2). The
*index arithmetic* matches Apple's exactly.

**VERDICT: SOUND.**

---

## Q5 — W-CUR ordering and key match with W-APEX.

**Call site** (`entry_trace.c`, `__wrap_sleh_abort`):
```
5761:    entry_note_abt_cur(far_);        /* BEFORE */
...
5780:    __real_sleh_abort(regs, type);   /* the handler */
```
`entry_note_abt_cur(far_)` runs at `:5761`, before `__real_sleh_abort` at `:5780`. The nested
`pmap_enter_options` this witness observes happens inside the handler ⇒ the key is set before the enter.
**Ordering correct.**

**The key variable** (`entry_stubs.c`):
```
2853:    g_abtpte_curva = va;             /* W-CUR sets */
2879:    cur = g_abtpte_curva;            /* W-APEX reads */
2880:    if (cur == 0u || va != cur) return;
```
W-CUR writes `g_abtpte_curva`; W-APEX filters on the SAME variable `g_abtpte_curva`. **Names match** —
no silent no-op. (W-APEX's `va` is its own argument, `pmap_enter_options`' `va`, which for the storm's
fault equals the abort `far`; that identity is the design premise, and the row self-publishes `_wwva`
so a mismatch is readable, `:2936`.)

**Cross-check — W-WALK's key** (`entry_stubs.c:2958`): `if (g_abtpte_pmpva == 0u || va !=
g_abtpte_pmpva) return;`. This is 1012's `g_abtpte_pmpva`, set by `entry_note_abt_pte`
(`:2465`), which the call site orders **before** W-WALK (`entry_trace.c:5822` then `:5832`), so
`g_abtpte_pmpva == far_` for this abort when W-WALK runs. **Correct by ordering.**

**VERDICT: SOUND.**

---

## Q6 — key-name collisions.

Enumerated every `xnu_live_abt_w*` literal in the tree (`grep -o '"xnu_live_abt_w[a-z0-9]*"' src/`):
38 keys in three disjoint namespaces — `ww*` (W-APEX), `wk*` (W-WALK), `wc*` (W-SYNC) — plus a
separate `wr*` family (`wrn/wrnx/wrprot/wrsite/wrva`) belonging to the **WRSTRIP** instrument, a
different switch (`STAGE90_XNU_WRSTRIP`), not this rung. No key appears in two namespaces; each literal
appears exactly once.

**Prefix hazard checked.** `xnu_live_abt_wk` is a byte-prefix of `xnu_live_abt_wkn`/`_wkl1`/`_wkl2pa`…/
`_wknx`. The row assembler matches keys by **exact equality**, not `startswith`
(`tools/check_1013_capture.py:1666-1670`: `if key == WIT_C_N_KEY` … `elif cur is not None and key in
fkeys`), so `_wk` and `_wkn`/`_wknx` remain distinct. (`family_count` uses `startswith` at `:1149`, but
its results feed only OBS count lines, never a verdict.) Same for `_wwn` vs `_wwnoproof`/`_wwnx`.

The `w*` keys do not collide with the 1012 `pmp*` keys, the (A) `pte*` keys, or any other family.

**VERDICT: SOUND.**

---

## VERDICT LIST

| # | probe | verdict |
|---|---|---|
| 1 | W-SYNC control VA | **DEFECT** — `va = gPhysBase+0x80000 = 0x80080000` is above the `TTBCR.N=2` split `0x40000000`; the walker refuses it at `entry_stubs.c:2280` (`wck=0`), the checker's `wck != 1` rule fires **WITNESS_METHOD_BROKEN**, V1–V3 withheld. **The press cannot produce a leaf verdict.** |
| 2 | W-WALK double-read | SOUND |
| 3 | W-APEX `pm_l1_phys` offset | SOUND (offset 0, `pmap.h:93/95/99`, `#pragma pack(4)`) |
| 4 | W-APEX index math | SOUND (matches `tte_offset`/`pte_offset`/`L1_PTE_ADDR_MASK` exactly) |
| 5 | W-CUR ordering + key name | SOUND (`entry_trace.c:5761` before `:5780`; `g_abtpte_curva` shared by writer and reader) |
| 6 | key collisions | SOUND (three disjoint namespaces; exact-match row assembly) |

## THE ONE LOUD DEFECT

**W-SYNC (Q1).** The control reads `gPhysBase + 0x80000 = 0x80080000`. On this device `gPhysBase =
0x80000000` (measured: `xnu_entry_args_physBase=0x80000000`, and the storm's own `ptewbase=0x80000000`),
and the port runs `TTBCR.N = 2` (split `0x40000000`). The single reused walker answers only VAs strictly
below the split with TTBR0 (`entry_stubs.c:2277`) and returns `0` for everything above (`:2280`). So
W-SYNC publishes `wck = 0` / `wcleaf = 0` on every ladder row, `check_1013_capture.py:4902` classifies
the control as wrong, and `:5010` short-circuits the rung to **WITNESS_METHOD_BROKEN** — the apex and
walk leaves (which are otherwise SOUND, Q3–Q6) are never read as a verdict. The rung will honestly
report its method as broken; it will not report a leaf. If a real reading is wanted from this hardware,
the control VA must be moved BELOW `0x40000000` (a VA the storm's own TTBR0 actually maps), not
`gPhysBase + 0x80000`.