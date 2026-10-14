# 1010 — the L2 page-table walk (extraction, by value)

This is the reviewed artifact §3.2 of `docs/1010-design.md` depends on: the exact ARMv7
short-descriptor constants **this port** uses, and a self-contained walk the abort wrapper can call
for the fault address. Every constant is cited to `file:line`; nothing here is assumed.

## 1. Constants (this port's own names) — `external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.h`

| Name | Value | line |
|---|---|---|
| `L1_SIZE` | `0x4000` (4096 entries) | pmap.h:195 |
| `L2_SIZE` | `0x400` (256 entries) | pmap.h:196 |
| `tte_offset(addr)` | `((addr >> 0x14) & 0xfff) << 2` (L1 index = VA[31:20]) | pmap.h:198 |
| `pte_offset(addr)` | `((addr & 0x000fffff) >> 12) << 2` (L2 index = VA[19:12]) | pmap.h:199 |
| `L1_PTE_ADDR_MASK` | `0xfffffc00` (coarse table base, bits[31:10]) | pmap.h:202 |
| `L1_SECT_ADDR_MASK` | `0xfff00000` (section base) | pmap.h:203 |
| `L1_TYPE_MASK` | `3` | pmap.h:207 |
| `L1_TYPE_FAULT` / `L1_TYPE_PTE` / `L1_TYPE_SECT` | `0` / `1` / `2` | pmap.h:209-211 |
| `L2_ADDR_MASK` | `0xfffff000` | pmap.h:214 |
| `L2_ACCESS_NONE` / `PRW` / `PRO` | `0x0` / `0x10` / `0x210` | pmap.h:217-219 |
| `L2_ACCESS_USER` | `(1 << 5)` = `0x20` | pmap.h:221 |
| `L2_ACCESS_APX` | `(1 << 9)` = `0x200` | pmap.h:222 |
| `L2_SMALL_PAGE` | `0x2` (`bits[1:0]=0b10`) | pmap.h:226 |
| `L2_NX_BIT` | `0x1` | pmap.h:227 |
| `L2_C_BIT`/`B_BIT`/`S_BIT`/`NG_BIT` | `0x8`/`0x4`/`0x400`/`0x800` | pmap.h:228-231 |
| `PAGE_SHIFT`/`PAGE_SIZE` | `12`/`4096` | mach/arm/vm_param.h:21-22 |

There is **no `L2_COARSE_PAGE`**: the `L2_*` names are the *leaf* bits; the coarse-*L1* type is
`L1_TYPE_PTE` = `1` (also spelled `ARM_PAGE_PAGE_TABLE` in `pmap.c:178-181`). L2 leaf types
(`pmap.c:184-187`): `ARM_PTE_DESCRIPTOR_64K = 0x01`, `ARM_PTE_DESCRIPTOR_4K = 0x02`. The AP is
composed from `PRW`/`PRO`/`NONE` + `USER` + `APX`, not a single symbol.

## 2. The TTBR split — **CORRECTS the design's "0x80000000"**

The port programs `set_mmu_ttbcr(2)` (`arm_vm_init.c:475`), so **TTBCR.N = 2 ⟹ TTBR0 owns the first
`2^(32-2)` = 1 GiB `[0, 0x40000000)`**, TTBR1 owns `[0x40000000, 4 GiB)`. This is a hardware
*short-descriptor* split of `2^(32-N)`:

- `VM_MAX_ADDRESS = 0x40000000` (the user ceiling) — `mach/arm/vm_param.h:66-68`, comment "This ARM
  pmap uses a 1 GiB TTBR0 split."
- `VM_MIN_KERNEL_ADDRESS = 0x80000000` (`mach/arm/vm_param.h:73`) — the *address-space* boundary.
- `VM_KERNEL_BASE_ADDRESS = 0x80000000` (`arm_vm_init.c:86`) — the KASLR slide base.

**So `0x80000000` (address-space boundary) and `0x40000000` (TTBR hardware split) are two different
numbers.** The design must key the walk on TTBCR.N, not on `0x80000000`.

- TTBR0 = `mrc p15, 0, Rt, c2, c0, 0` (`pmap.c:1295` `armreg_ttbr_read`; `trap.c:451`).
- TTBR1 = `mrc p15, 0, Rt, c2, c0, 1` (`trap.c:452`).
- TTBCR = `mrc p15, 0, Rt, c2, c0, 2` (`trap.c:453`).
- TTBR0's low 14 bits are walk attributes (`arm_context_switch` ORs `0x5b`/`0x1b`,
  `armv/cpufunc-v7.s:52-54`), so the base is `TTBR & 0xffffc000` — the mask `entry_live_ttb_base`
  already uses (`entry_stubs.c:2421`).

## 3. An existing walk? `pmap_pte` — present, but not usable here

`pmap_tte(pmap_t, vm_offset_t)` (`pmap.c:1774`, decl `pmap.h:279`) and `pmap_pte(pmap_t, vm_offset_t)`
(`pmap.c:1786`) exist and link into the entry image (`osfmk_arm_pmap.o`). **But `pmap_pte` takes a
`pmap_t` (not derivable from a raw FAR), never reads cp15, is not TTBR-split-aware, and `panic`s on a
section mapping (`pmap.c:1810`).** The wrapper needs an *address-driven* walk that reads the live
table and fails open. **Write our own.**

## 4. The walk (`entry_stubs.c`, pure C, no libc, `uint32_t`)

```c
uint32_t stage90_read_l2_pte(uint32_t va, uint32_t *out_l1_desc, uint32_t *out_ttbr0_used);
```

Reads TTBR0/TTBCR; picks the table by TTBCR.N (`va >> (32-N)`); indexes L1 by `VA[31:20]`; requires a
COARSE L1 descriptor (`& 3 == 1`); indexes L2 by `VA[19:12]`; returns the raw **small-page** L2 word
(`& 3 == 2`), else `0`. `*out_l1_desc` receives the raw L1 word; `*out_ttbr0_used` is 1 if TTBR0
answered. Full body is in the extraction transcript; the constants above are its only inputs.

**Gotchas this port exhibits** (each handled by the walk returning 0):

- **Sections bypass L2** — `arm_vm_init`/boot code put `L1_TYPE_SECT` in L1; `pmap_pte` panics on them.
- **No supersections** — grep empty; sections here are plain 1 MB.
- **4 KB granule** — L1 4096, L2 256 entries (`ARM_PGBYTES 4096`).
- **`TTBR0 == TTBR1`** on this device in the handed-off state (`entry_smem.h:53`; recorded
  `xnu_entry_abort_first_ttbr0 = _ttbr1 = 0x8030404a`) — so the low address's table is unambiguous.
- **phys==virt deref** — the table base is dereferenced *as a VA*, valid only because the entry
  image's window has `physBase == virtBase` (`entry_stubs.c:2545-2551`).
- **`entry_live_ttb_base` is a trap** — it returns TTBR1 whenever `n != 0`, which is **wrong for
  `far=0x9a000`** (the MMU walks it via TTBR0). The new walk must do the boundary check.

## 5. `far = 0x9a000`

`0x9a000 < 0x40000000`, so it is walked via **TTBR0** (N=2 low side). L1 index `= (0x9a000 >> 20) &
0xfff = 0`, L2 index `= (0x9a000 >> 12) & 0xff = 0x9a`; `*out_ttbr0_used` reads 1. The owning pmap is
`current_cpu_datap()->user_pmap` — for pid 1 (launchd) its user `pm_l1_phys`. The kernel pmap is only
reachable via TTBR1 (≥ `0x40000000`), so this low address cannot be a kernel_pmap mapping.
## 6. UPDATE 2026-10-14 (1010b) — the walk as BUILT had §1's mask in §2's slot

The 1010 press read (`records/1010-press-read.txt`, arm `armed-d13-0f39207c`) exposed a defect in the
walk **as built** (`src/entry/entry_stubs.c:2146`), not in this document. The code masked the TTBR0
base with `ABTPTE_L1_PTE_MASK` (`0xfffffc00`, §1 — the **coarse-L1-descriptor → L2-table** mask) where
§2 says — and the working `entry_live_ttb_base` does — to use `TTBR & 0xffffc000`.

The consequence, read off the machine: for the storm's tail (`far=0x5d000`, `fsr=0x0000080f` = FS=15
"permission fault, level 2") every walk row returned `ptel2=0` / `pteu=0` — **no L2 leaf** — which an
FS=15 fault *cannot* produce (an absent L2 descriptor yields FS=5/6/7, a *translation* fault). The walk
read the L1 table **+0x2000** high (`0x81a8605b & 0xfffffc00 = 0x81a86000` vs the real, 16 KB-aligned
`0x81a84000`), so `far=0x5d000` (L1 index 0) landed on the real L1 word for VA `0x80000000` (the
kernel), whose L2 lookup missed.

**1010b fix.** Two named masks, one per role:

| role | mask | name | why |
|---|---|---|---|
| TTBR0 **base** | `0xffffc000` (bits[31:14]) | `STAGE90_TTBR_BASE_MASK` | an L1 table is `L1_SIZE` = 0x4000 = **16 KB** aligned (§1) |
| L1 descriptor → **L2 table** | `0xfffffc00` (bits[31:10]) | `ABTPTE_L1_PTE_MASK` | an L2 table is `L2_SIZE` = 0x400 = 1 KB aligned (§1) |

This is the class `[[mi4-stand-in-size-is-not-value]]` / `[[mi4-one-value-two-definitions]]` — one mask
literal, two roles, the wrong one substituted. `tools/check_1010_capture.py` now makes the falsifier a
check: a `_ptel2 == 0` row paired to an FS=15 fault is a **HARD anomaly** naming the walk (§3 above),
and the (A) gate is refused rather than certified. The 1010b build clauses pin the mask **by value** at
both the definition and the use, and assert the old (wrong) use is gone.

**Still owed after 1010b:** the leaf (APX=1 vs APX=0 at the tail) — the decisive cell for
`docs/1011-design.md` (F1a prot-strip vs F1b entry-denies-write) — remains unread until a correct walk
is pressed.

## 7. UPDATE 2026-10-14 (1010c) — the corrected walk reached OUT OF ITS OWN WINDOW and faulted

The 1010b press (`records/1010b-press-read.txt`, arm `armed-d13-64391319`) fixed §6's mask defect — and
the fix **exposed a second, deeper defect in the same walk**: it dereferences an L2 table that lies
*outside* the entry image's identity window, with **no bounds check**, and the deref faults. The run
died at launchd-start, before the storm; the leaf is still unread.

**By value.** Abort #4 (`_pcn=4`, `far=0x2bd89f7c`) has its `_pcn` row but **no `_pten` row** — and
`entry_note_abt_pte` is the LAST statement of `__wrap_sleh_abort` (`entry_trace.c:5779`), whose walk
call precedes every write, so a fault *inside the walk* truncates the (A) block. The very next abort
(#5) is **the walk itself**: `pc=0x80002644` = `ldr r0,[r0]`, the L2 leaf load (`entry_stubs.c:2184`),
with `far=0x46300224`, `fsr=0x00000005` (FS=5 section translation fault), `cpsr=0x20000193`
(supervisor). And the arithmetic is exact: `0x46300224 = 0x46300000 + (((0x2bd89f7c>>12)&0xff)<<2)` =
`l2base + L2_index×4` — bit-identical to `entry_stubs.c:2183`.

**Why the mask fix is *correct* and still faults.** With the live word `TTBR0=0x81a6e05b`: 1010's mask
`0xfffffc00` gave base `0x81a6e000` (**not** 16 KB aligned) so the L1 read landed on the wrong slot and
returned a stale coarse pointer whose L2 was *in-window* — hence 1010 returned `ptel2=0` **without
faulting**. 1010b's mask `0xffffc000` gives `0x81a6c000` (**16 KB aligned**, `L1_SIZE`), so the L1 read
now lands in-window (`0x81a6caf4`) and **succeeds** — but the *true* L1 descriptor points at an L2 table
at PA **`0x46300000`**, in the device's **low RAM bank** `[0, 0x60000000)`, which this port's
single-span physmap does not map (`phys_to_virt(p)=p - gPhysBase + gVirtBase` is the identity and only
reflects `p ≥ 0x80000000`, `pmap.h:193`; the multi-bank region list is the owed 911-B/911e port). The
entry window is `[0x80000000, 0x80000000+memSize=0x9e400000)`, so `0x46300224` is unmapped → FS=5.

**The lesson.** The walk is a *reader* with two dereferences (`entry_stubs.c:2170` the L1 slot,
`:2184` the L2 leaf) and **neither is bounded**. §4's own note assumed "every address it is about is
below the TTBCR split" — true of `far_`, **false** of the L2 pointer the walk chases *from* `far_`. A
walk that cannot reach a table must say so (return 0, as it already does for every other bad shape),
never fault. `tools/check_1010_capture.py` now makes this the falsifier-as-check: a `_pcn` ordinal with
**no** `_pten` row is a HARD anomaly naming the walk, and the (A) gate refuses to certify. (Its
`s_cadence` empty-sample crash, which hid this, is also fixed.)

**1010c plan.** (1) Bound both derefs to `[0x80000000, 0x80000000 + memSize)`, using the window the
image was handed (`xnu_entry_args_memSize`) — fail open when a table is out of range. (2) For the L2
leaf the window refuses, do **not** give up — reach it the way the port already reaches a low-RAM page:
`entry_smem.c` (rung 911c) installs a **section** VA→PA for the low bank (`0xe0000000` → `0x0fa00000`)
via `entry_mmio_section`/`entry_section_install` and **reads it live** (capture `cb4e17f1`). The walk
can install a temporary section for the L2 table's 1 MB, read the leaf through the alias, and restore
the slot — that is the read that finally names the leaf, using only machinery this port has already
pressed.
