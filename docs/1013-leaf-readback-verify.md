# 1013 — leaf read-back verification (static, BY VALUE)

**Scope:** the planned `--wrap=pmap_enter_options` read-back of the page-table LEAF from the pmap's
own L1 table after `__real_pmap_enter_options` returns. Every claim below is cited to
`external/xnu-hd2-darwin13/xnu/osfmk/arm/{pmap.c,pmap.h,arm_vm_init.c}` (tree HEAD `432e8e8`) or to
`src/entry/entry_stubs.c` / `src/entry/entry_trace.c`. NO device, NO build.

---

## 0. VERDICT

**The read-back is sound in FORM but WRONG in TWO NAMED PLACES as described.** Fix both before build:

1. **THE LEAF OFFSET FORMULA IN THE PLAN IS WRONG.** `pte_offset` is **not**
   `((va & 0xfff) >> 12) << 2`. It is `((va & 0x000fffff) >> 12) << 2` = `(((va >> 12) & 0xff) << 2)`.
   For `far = 0x2e000` the plan's arithmetic gives **0x38**; the correct byte offset is **0xb8**.
   The two agree only when the VA's bits[19:12] are zero — **not** "for any small-page VA below
   1 MB" (0x2e000 is below 1 MB and they DISAGREE). The plan's point-1 side-note is itself the
   error that produces the point-4 wrong number.
2. **RE-READ `pm_l1_phys` AFTER THE CALL, not the pre-call copy.** The existing recorder reads
   `*(uint32_t*)pmap` BEFORE `__real_...` (`entry_trace.c:5918`). `pmap_expand` → `pmap_expand_ttb`
   can REPLACE the L1 table and change `pm_l1_phys` (`pmap.c:3084-3086`); the enter then writes the
   leaf into the NEW table. A read-back that reuses the pre-call `pm_l1_phys` reads the retired
   table and manufactures a false reading. (Harmless for the storm VA — see §5 — but the read-back
   must be correct in general.)

Everything else in the plan is **CONFIRMED**: the two-level formula, the masks, the decode of
`0x81bb1001`, the offset 0xb8 lying inside a 1 KB L2 table, and reading the table through the raw
PA (`phys_to_virt` is the identity on this port).

---

## 1. THE TWO-LEVEL FORMULA (Apple's, by value)

`pmap_pte` (`pmap.c:1786-1826`):

```
1786  vm_offset_t pmap_pte(pmap_t pmap, vm_offset_t virt)
1788      uint32_t *tte_offset = (uint32_t *) pmap_tte(pmap, virt);
1795      tte = *tte_offset;
1810      if ((tte & ARM_PAGE_MASK_VALUE) == ARM_PAGE_SECTION) panic(...);   /* 0x03 vs 0x02 */
1817      pte = L1_PTE_ADDR(tte);
1818      if (!pte) return 0;
1824      ptep = (uint32_t *) ((phys_to_virt(pte) + pte_offset(virt)));
```

`pmap_tte` (`pmap.c:1774-1780`) returns `addr_to_tte(pmap->pm_l1_virt, virt)`.

Macros (`pmap.h`):

```
198  #define tte_offset(addr) (((addr >> 0x14) & 0xfff) << 2)
199  #define pte_offset(addr) (((addr & ~(L1_SECT_ADDR_MASK)) >> PAGE_SHIFT) << 2)
200  #define addr_to_tte(base, addr) (base + tte_offset(addr))
202  #define L1_PTE_ADDR_MASK 0xfffffc00 /* Bits [31:10] */
203  #define L1_SECT_ADDR_MASK 0xfff00000    /* Bits [31:20] */
205  #define L1_PTE_ADDR(tte) (tte & L1_PTE_ADDR_MASK)
214  #define L2_ADDR_MASK 0xfffff000
```
and `PAGE_SHIFT = 12` for the kernel (`osfmk/mach/arm/vm_param.h:29`, under
`#if defined(KERNEL) || !defined(__arm64__)`).

**Exact formula, for a given VA:**

| Step | Expression | Bytes / bits |
|---|---|---|
| L1 descriptor byte address | `L1_base + (((va >> 20) & 0xfff) << 2)` | bits[31:20] → L1 index, ×4 |
| L1 descriptor word | `*(uint32_t*)(L1_base + L1_index*4)` | must have bits[1:0]==0b01 (coarse) |
| L2 table PA | `l1e & 0xfffffc00` | bits[31:10] |
| Leaf byte offset in L2 | `((va & 0x000fffff) >> 12) << 2` | bits[19:12] → L2 index, ×4 |
| Leaf byte address | `L2_base + L2_index*4` | one 4-byte word |

### `~(L1_SECT_ADDR_MASK)` vs a plain `& 0xfff` — the plan's premise is FALSE

`~L1_SECT_ADDR_MASK = ~0xfff00000 = 0x000fffff`. So

```
pte_offset(addr) = ((addr & 0x000fffff) >> 12) << 2 = (((addr >> 12) & 0xff) << 2)
```

This selects **bits[19:12]**. A plain `((addr & 0xfff) >> 12) << 2` selects bits[11:0], shifts by 12
→ **identically 0 for every VA**. The two agree **only when bits[19:12] of the VA are zero**
(`addr & 0x000ff000 == 0`) — i.e. only for VAs below 0x1000 or exactly on a 1 MB boundary. They
**differ for essentially every real VA**, including every sub-1 MB VA this rung cares about:

| VA | correct `((va>>12)&0xff)<<2` | plan's `((va&0xfff)>>12)<<2` |
|---|---|---|
| `0x2e000` | **0xb8** (idx 46) | 0x38 ✗ |
| `0x61000` | 0x184 (idx 97) | 0x0 ✗ |
| `0x9a000` | 0x268 (idx 154) | 0x0 ✗ |
| `0x5d000` | 0x174 (idx 93) | 0x0 ✗ |

So the read-back MUST use `(((va >> 12) & 0xff) << 2)`.

---

## 2. THE L1 BASE: `pm_l1_phys` vs `pm_l1_virt`, and `phys_to_virt`

`struct pmap` (`pmap.h:98-100`, `#pragma pack(4)` at `pmap.h:93` — no leading padding, so
`pm_l1_phys` **is offset 0**):

```
 99      paddr_t pm_l1_phys;         /* L1 table address  (PHYSICAL) */
100      vaddr_t pm_l1_virt;         /* L1 virtual table address      */
```

Apple's `pmap_pte` walks through **`pm_l1_virt`**. The wrapper reads **`pm_l1_phys`**. Conversion:
`phys_to_virt(p) = ((unsigned long)(p) - gPhysBase) + gVirtBase` (`pmap.h:193`).

**By value on this port this is the IDENTITY**, so the wrapper's raw read is numerically correct:

- `gPhysBase = args->physBase`, `gVirtBase = args->virtBase` (`arm_vm_init.c:331-332`), and this
  port's entry hands XNU `a->virtBase = a->physBase = STAGE90_XNU_ENTRY_BASE` (`xnu_entry_jump.c:141-142`),
  with `STAGE90_XNU_ENTRY_BASE = 0x80000000` (`out/stage90/xnu_arm_entry.h:5`). So
  `phys_to_virt(p) = p - 0x80000000 + 0x80000000 = p`.
- Corroborated by the run itself: `arm_vm_init.c:364` "`gPhysBase == gVirtBase` on this arm", and the
  1012 log "boot_args (physBase == virtBase)".
- The tables measured live at `0x81a86000` (`records/1012-press-read.txt:52-53`) sit inside the
  identity/managed map `[0x80000000, 0x80000000+0x1e400000)` (`mem_size = gMemSize = 0x1e400000`),
  so both the L1 word and the L2 word are readable through that same address with no translation.

**Verdict:** `phys_to_virt` is **REQUIRED IN PRINCIPLE** (Apple's code uses it; the L1/L2 pointers
are PAs) but is a **numerical no-op here**. Two consequences for the implementation:

- Do **not** leave the conversion out *and* call it correct "because V==P"; either apply the identity
  explicitly or, exactly as the existing walk does (`entry_stubs.c:2299-2337`), read through the raw
  PA and **bound-check both words against `[gPhysBase, gPhysBase + mem_size)`** so an out-of-window
  table is a published refusal, never a fault.
- **Do NOT bind to `entry_stubs.c`'s stand-in globals.** `entry_stubs.c:100-102` defines
  `gPhysBase = gVirtBase = 0x00200000ul` under `#ifndef STAGE90_ENTRY_REAL_ARM_INIT` — a stale
  stand-in that must not be linked into the arithmetic. (It only bites if `phys_to_virt` is
  re-spelled locally, which is another reason to use the raw PA on this port.)

---

## 3. THE MASKS (the 0xfffffc00 trap)

- **L1 → L2 table base = `L1_PTE_ADDR_MASK = 0xfffffc00`** (bits[31:10]). This is the **RIGHT** mask
  for a coarse L1 descriptor (`bits[1:0]=0b01`). Cited: `pmap.h:202`, macro `L1_PTE_ADDR` at
  `pmap.h:205`, used by Apple at `pmap.c:1817`, `pmap.c:3163` (`*tte = (... & L1_PTE_ADDR_MASK) | L1_TYPE_PTE`),
  `pmap.c:3840`. **CONFIRMED.**
- **Leaf page PA from an L2 entry = `L2_ADDR_MASK = 0xfffff000`** (`pmap.h:214`). **CONFIRMED.**
  (Note: for reading the *L2 table base* out of the L1 the correct mask is `0xfffffc00`; `0xfffff000`
  is what you apply to the *leaf* to get the page PA. For this tree's `0x81bb1001` both give the same
  table base `0x81bb1000`, but they are different fields and the L1 one is `L1_PTE_ADDR_MASK`.)
- **The TTBR0 base mask is a third, different mask** — `2^(14-N)` alignment, `0xfffff000` at this
  port's `TTBCR.N = 2` (`set_mmu_ttbcr(2)`, `arm_vm_init.c:475`; `entry_ttbr_base`, `entry_stubs.c:2133-2140`).
  **This mask does NOT apply to `pm_l1_phys`**: `pm_l1_phys` is already a table base, stored as
  `new_l1->phys_page << PAGE_SHIFT` (`pmap.c:3923`) or `pages->phys_page << PAGE_SHIFT`
  (`pmap.c:3085`) — page-aligned, no knee attribute bits. Applying `0xffffc000` (16 KB) or
  `0xfffff000` to it is a **no-op for `0x81a86000`** (`0x81a86000 & 0xfffff000 == 0x81a86000`), but
  the 16 KB mask would be wrong for any base not 16 KB aligned — the plan should simply **not mask**
  `pm_l1_phys`.

**The 0xfffffc00-vs-TTBR trap from memory does not apply to this read-back**, because the base comes
from a struct field, not from TTBR0. The one place `0xfffffc00` *is* correct here is the L1→L2 step.

---

## 4. THE DECODE, AND THE OFFSET FOR `far = 0x2e000`

`_ptel1 = 0x81bb1001` (`records/1012-press-read.txt:105`), read from the TRUE L1 whose base
(`_pmppmap = _pmpttbr & 0xFFFFFF00 = 0x81a86000`) is the pmap's own `pm_l1_phys`.

```
bits[1:0]         = 0x1  = 0b01  = L1_TYPE_PTE  (coarse)  ✓ (ARM_PAGE_MASK_VALUE=0x03, pmap.c:181)
L2 table PA       = 0x81bb1001 & 0xfffffc00 = 0x81bb1000                        ✓
                  (0x81bb1001 & 0xfffff000 = 0x81bb1000 — same value)
```

Leaf offset for `va = far = 0x2e000`:

```
L1 index  = (0x2e000 >> 20) & 0xfff = 0        → L1 descriptor at 0x81a86000 + 0x00
L2 offset = ((0x2e000 & 0x000fffff) >> 12) << 2
          = (0x2e000 >> 12) << 2 = 0x2e << 2 = 0xb8   (= index 46)
leaf addr = 0x81bb1000 + 0xb8 = 0x81bb10b8
```

**0xb8 + 4 = 188 ≤ 0x400 (1 KB): the leaf is well inside the L2 table.** ✓
(For comparison: the plan's wrong formula gives `(0x2e000 & 0xfff) >> 12 = 0xe000>>12 = 0xe`, ×4 =
`0x38` = index 14 — four times too low, and reading a different page's descriptor.)

Sanity on the other measured VAs (all < 0x100000, so the SAME L1 slot 0): `far=0x61000 → 0x184`,
`far=0x5d000 → 0x174`, `far=0x9a000 → 0x268` — all in the same L2 table `0x81bb1000`, all < 0x400.

The L2 table `0x81bb1000` sits inside the identity window `[0x80000000, 0x9e400000)`, so the read is
in-window (unlike the 1010b low-RAM table `0x46300000`, which was the artifact of the WRONG TTBR
mask and is gone once the correct base is used).

---

## 5. OTHER CHECKS THE PLAN IMPLIES

- **Presence check must not panic.** `pmap_pte` panics on a section descriptor (`pmap.c:1810-1811`).
  A read-back must test `(l1e & 0x3) == 0x1` (coarse) FIRST, else publish "no L2" — never reproduce
  the panic. Same for the leaf: a valid leaf is `(l2e & 0x3) == 0x2` (`L2_SMALL_PAGE`, `pmap.h:226`).
- **`pmap_pte` returns 0 for kernel-user commpage** (`pmap.c:1802-1803`: `virt >= _COMM_PAGE_BASE_ADDRESS`
  on a non-kernel pmap). Not relevant to `far=0x2e000`, but the read-back must not treat a 0 result
  as "table absent".
- **The expand hazard (§0.2).** For the storm VA (0x2e000) `tte_offset = 0`, so
  `expansion_size = (0 & ~0xfff) + 0x1000 = 0x1000` and `pmap_expand_ttb` returns immediately
  (`pmap.c:3044`) — `pm_l1_phys` cannot move for THIS VA. But the wrapper serves every VA, so the
  re-read-after-call rule is required for correctness of the general cell.
- **Which table the wrapper reads == which table the MMU walks.** 1012 already certified this for the
  storm: `_pmppmap & 0xFFFFFF00 == _pmpttbr & 0xFFFFFF00 == 0x81a86000` (`records/1012-press-read.txt:67`),
  and `entry_ttbr_base(0x81a8605b, 2) = 0x81a86000`. So the read-back from `pm_l1_phys` lands on the
  same L1 the hardware walks. ✓
- **Reading through `phys_to_virt` of the L2 base** (`pmap.c:1824`) is likewise identity here
  (`0x81bb1000`).
- **What the leaf will look like.** `leo_pte_build` (`pmap.c:1641-1645`) builds
  `(pn<<12 & 0xfffff000) | 0x2 | pmap_vm_prot_to_page_flags(...) | cache_attrs`, and
  `pmap_vm_prot_to_page_flags` (`pmap.c:1095-1120`) sets `L2_ACCESS_PRW (0x10)` always and **clears
  APX (bit 9) iff `prot & VM_PROT_WRITE`**, else sets it; plus `L2_ACCESS_USER (1<<5)` for a non-kernel
  pmap and `L2_NX_BIT` unless `prot & VM_PROT_EXECUTE`. So the F1a/F1b discriminator on the read-back
  word is **bit 9 (APX): clear ⇒ writable leaf ⇒ F1a-consistent; set ⇒ read-only leaf ⇒ F1b/loss**.
  That is the value the rung is after.

---

## 6. WHAT MUST CHANGE BEFORE BUILD

1. Replace the leaf-index arithmetic with `(((va >> 12) & 0xff) << 2)` — **NOT**
   `((va & 0xfff) >> 12) << 2`. (For `far=0x2e000` this is the difference between reading index 46
   and index 14.)
2. Re-read `*(uint32_t*)pmap` **after** `__real_pmap_enter_options` returns; do not reuse the
   pre-call `pm_l1_phys` local.
3. Use `L1_PTE_ADDR_MASK (0xfffffc00)` for the L1→L2 step and `L2_ADDR_MASK (0xfffff000)` for the
   leaf's page PA; do **not** apply any TTBR-style mask to `pm_l1_phys`.
4. Type-check the L1 descriptor (`& 0x3 == 0x1`) before dereferencing the L2, and bound both reads
   to `[gPhysBase, gPhysBase + mem_size)` exactly as the existing walk does; fail open with a
   published refusal rather than faulting.
5. `phys_to_virt` is the identity on this port (`gPhysBase == gVirtBase == 0x80000000`), so reading
   through the raw PA is correct; but do not source `phys_to_virt` from `entry_stubs.c`'s
   `0x00200000` stand-in globals.