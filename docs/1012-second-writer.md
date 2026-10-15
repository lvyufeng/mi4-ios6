# 1012 — STATIC: is there a SECOND read-only writer for the storm's leaf?

**Status:** static (no device, no build). Reads on the working tree at HEAD (`dfc1904`).
Tree read: `external/xnu-hd2-darwin13/xnu/` (Darwin 13, `BOARD_CONFIG_QSD8250_LEO`) and `src/`.
Companion to `docs/1012-f1a-static.md` (which refuted every strip site ABOVE the apex) and to
`records/1012-press-read.txt` (which measured `prot=0x3` AT the apex).

---

## 0. Straight answers

**Q1 — Is there a REACHABLE second-RO-writer for this leaf?**
**Not on the WRITE-fault path.** The whole arm tree contains **three** places that can set
`L2_ACCESS_APX` (bit 9, "read-only") on a leaf, and **one** that only ever ADDS it and never clears
it (`pmap_page_protect`'s write-protect branch, `pmap.c:4062`). All of them are behind control paths
that a `fault_type = 0x3` WRITE fault cannot take: kernel-map-only init, pageout/lock-request
disconnect, `mprotect`, or map shrink/copy. **None of them is reached between the `PMAP_ENTER`
and the retry on this fault.** The reason is structural and is the load-bearing finding:

> `pmap_page_protect(pn, VM_PROT_READ)` (`pmap.c:3943`) takes the `case VM_PROT_READ: remove = FALSE`
> arm (`:3977`) and then the `else` branch **`*(pt_entry_t *) pte |= (L2_ACCESS_APX)`** (`:4062`) — a
> **pure re-RO, with flush, that never consults a "write" prot and never clears APX.** It is the ONLY
> writer in the tree of that shape. But its callers (`pmap_disconnect` = `prot 0`; `memory_object.c:231`;
> `vm_object.c:2901/2940/2969`) are all pageout / lock-request / disconnect paths, not the fault path.
>
> The fault path itself only ever has, for this leaf, **`pmap_enter_options` → `leo_enter`** (which
> rebuilds writable from `prot=0x3`) and **`pmap_flush_tlbs`** (which does not write leaves).

**Q2 — Is `far = 0x2e000` a shared-cache mapping?**
**No.** The shared region this boot is `[0x2c000000, 0x40000000)` (console: `IOS7LAB shared region
0x2c000000..0x40000000`; `vm_shared_region.c:626-635` sets `base_address = 0x2C000000U`,
`size = 0x14000000U` under the `IOS7_COMPAT_PLATFORM`/`STAGE90_XNU_SHREG_GEOM` gate). `0x2e000` is
**~184 KiB**, four orders of magnitude below the shared-region base. It is in the **low executable /
mach-header band** of the first exec'd image (launchd), not in the shared cache.
The console's codesign line is about a **different** address: `cs_invalid_page(0x2de7e000)` with
`0x2de7e000 = 0x2c000000 + slide(0x1e7e000)` — i.e. `base + slide`, a real shared-cache page. The
storm's `far`, `0x2e000`, is **not** that page.

---

## 1. Every raw leaf-write in the D13 build set (by value)

The arm pmap is the only build-set file that writes a page-table leaf (`grep -rn '\*pte\s*=\|… |='`
over `osfmk/arm/`; `L2_ACCESS_APX` appears **only** in `osfmk/arm/pmap.c`, at the eight lines below).

| # | file:line | write | guard / enclosing function | can it run for (user pmap, va=0x2e000) AFTER the enter? |
|---|---|---|---|---|
| 1 | `arm/pmap.c:1660` | `*pte = leo_pte_build(map,va,pn,prot,wired)` | `leo_enter`, **boot** arm: `if (!pmap_initialized)` | **NO** — dead after `pmap_init`; the storm is post-init |
| 2 | `arm/pmap.c:1668` | `*pte = leo_pte_build(...)` | `leo_enter`, same-pn re-protect arm (`oldpn == pn && entry`) | **YES — this IS the enter**: builds from `prot`; `prot=0x3` ⇒ clears APX (writable). Not a re-RO. |
| 3 | `arm/pmap.c:1671` | `*pte = 0` | `leo_enter`, break-before-make (oldpn != pn) | YES, but transient — the next statement rebuilds (line 1680) write-bearing |
| 4 | `arm/pmap.c:1680` | `*pte = leo_pte_build(map,va,pn,prot,wired)` | `leo_enter`, new-mapping arm | **YES** — the enter itself; writable from `prot=0x3` |
| 5 | `arm/pmap.c:3660` | `*pte = dpte` (may set `L2_ACCESS_APX` at `:3658`) | `pmap_init` const-DATA / data-NX rewrite, `if (doconstro)` | **NO** — kernel map, init-time |
| 6 | `arm/pmap.c:3611` | `*ptep |= L2_ACCESS_APX` | `pmap_init` `ro_kern` loop over `[vm_kernel_stext, vm_kernel_etext)` on `kernel_pmap` | **NO** — kernel text only |
| 7 | `arm/pmap.c:1376-1377` | `*cpte &= cacheability_mask; *cpte |= attributes` | `pmap_update_cache_attributes_locked` | **YES reachable** (see §2) but **cannot make it RO**: `cacheability_mask` (`
:1336`) = `~((TEX_011<<2) | 4KB_TEX(TEX_111))` = bits[C,B,TEX] only — **bit 9 APX is preserved** |
| 8 | `arm/pmap.c:4304-4306` | `*spte &= ~(L2_ACCESS_APX)` / `*spte \|= L2_ACCESS_APX` | **`pmap_protect`** (`:4225`) — `|= APX` when `!(prot & VM_PROT_WRITE)` | Reachable from `mprotect`/map-copy (§2), **not the fault path** |
| 9 | `arm/pmap.c:4056-4062` | `*(pt_entry_t *) pte \|= (L2_ACCESS_APX)` | **`pmap_page_protect`** (`:3943`), the `else` (write-protect) branch, `remove==FALSE` | **The one pure re-RO writer.** Reachable only via disconnect/pageout/lock-request (§2) |
| 10 | `arm/pmap.c:3737/3750` | `*cpte = 0` | `pmap_remove_range` | **dead on this build**: `:3699` `#if BOARD_CONFIG_QSD8250_LEO` `leo_remove_range(...); return;` |
| 11 | `arm/pmap.c:1709` | `*p=0` | `leo_remove_range` | removal (present→absent), gives FS=5/7, not FS=15 |

No writer exists outside `osfmk/arm/pmap.c`. `src/`'s only L1 writes are the boot-time
`entry_mmio_section` / `entry_section_install` (the 484 SMEM/GIC/USB 1 MB-section machinery) plus the
walk's own temporary alias (`entry_stubs.c:2203-2243`): all run **before** XNU creates the user pmap
and all target the kernel/console L1, not `0x81a86000`.

---

## 2. Reachability of the two re-RO writers for (user pmap, va=0x2e000)

### 2a. `pmap_protect` (`pmap.c:4225`) — re-ROs on `!(prot & VM_PROT_WRITE)`

Callers reachable after boot: `vm_object_pmap_protect` (`vm_object.c:2888/2912/2932/2960`),
`vm_map_protect` (`vm_map.c:3683/8370`), `vm_map_copy` (`vm_map.c:11240`). All carry
`prot = something & ~VM_PROT_WRITE` (`vm_map.c:3673`, `:8944`, `:11225`) — i.e. they re-RO the range.

But **every one of these is a map-wide operation** (mprotect, `vm_map_copy` of a whole entry, a
submap shrink). The storm is a single threaded retry loop on one instruction (`trap.c:906-910`
"Retry execution of instruction"); nothing in the retry path — `vm_fault` → `vm_fault_enter` →
`PMAP_ENTER_OPTIONS` → `leo_enter` — calls `pmap_protect` or `vm_map_protect`. To re-RO the leaf
*between* the enter and the retry you would need a **concurrent** mprotect/copy from another thread.
Nothing in the console tail shows another actor starting between aborts.

### 2b. `pmap_page_protect` (`pmap.c:3943`) — the pure re-RO

The `case VM_PROT_READ / READ|EXECUTE: remove = FALSE` arm (`:3977`) then `else {}` at `:4058`
executes `*(pt_entry_t *) pte |= (L2_ACCESS_APX); pmap_flush_tlbs(...)` (`:4062`). Note it ORs APX
in **unconditionally** for that arm — it is a re-RO of every mapping of the physical page.

Its only callers (whole tree):
- `pmap_disconnect(pa)` (`pmap.c:4497`) → `pmap_page_protect(pa, 0)` → prot 0 hits the **default**
  arm (`remove = TRUE`) → **removes**, does not re-RO. (`memory_object.c:231`; `vm_fault.c:1841`,
  `:2082`, `:2567`, `:3458`, `:4269` all call `pmap_disconnect`.)
- `vm_object_pmap_protect` with `pmap == PMAP_NULL` (`vm_object.c:2901/2939/2969`), reached from
  `vm_map.c` shrink/remove/copy paths — passes `VM_PROT_NONE` (removing) or a `& ~WRITE` prot.
- `memory_object.c:231` `pmap_page_protect(m->phys_page, VM_PROT_ALL & ~prot)` — a lock request.

Reachable only from pageout/`memory_object_lock_request`, map copy, or map shrink. None is on the
single-threaded retry.

### 2c. The one path that DID fire on this boot

`vm_fault_enter`'s codesign taint block (`vm_fault.c:2555-2575`): when the page is newly tainted on a
write fault, `must_disconnect` is set and **`pmap_disconnect(m->phys_page)`** runs at `:2567`
*before* the `PMAP_ENTER` (`:2585`). That is a **removal** (`prot 0`), not a re-RO — and it happens
**before** the enter, so the enter's `leo_pte_build(prot=0x3)` still leaves the leaf writable.
The console's `cs_invalid_page(0x2de7e000): clearing CS_VALID` is exactly this block firing
(`vm_fault_enter` → `reject_page = cs_invalid_page(vaddr)` at `:2492`), and it printed **once** and
`retval = 0`, so the fault **proceeded** (`kr = KERN_SUCCESS`). It is a decision about the page at
`0x2de7e000` — a shared-cache page — not about `far = 0x2e000`.

---

## 3. The codesign / shared-cache path — verdict

**There is no shared-cache or codesign route that maps a low VA read-only.**

- The shared region is mapped `VM_PROT_READ / VM_PROT_ALL` at `vm_shared_region.c:1285/1330/1362`
  (`init_prot = VM_PROT_READ`, `max_prot = VM_PROT_ALL`) and `mem_entry->protection = VM_PROT_ALL`
  (`:682`, `:1702`). The shared cache is entered with `VM_PROT_READ` init, and the per-page leaf
  protection comes from `vm_fault_enter`'s `prot` — the **same** path as any other fault. There is no
  "re-map the shared page RO after a write fault" step.
- The `out_prot` / entry-protection question in the design (`vm_map.c:9166 *out_prot = prot` from
  `:9061 prot = entry->protection`) is **not** a distinct shared-cache path: `vm_map_lookup_locked`
  is shared by every fault. The only demotion on this arm is `vm_map.c:9139 prot &= ~VM_PROT_WRITE`,
  which fires only for the **read** arm of a `needs_copy` entry (`(fault_type & VM_PROT_WRITE) == 0`);
  a WRITE fault takes the shadow-making branch above it (`:9114`) and reaches `:9139` with WRITE intact.
  1011 already measured `vmeprot = 0x3` here, which is the entry's protection (**not** a stripped copy).
- `far = 0x2e000` is **not** in `[0x2c000000, 0x40000000)`. It cannot be a shared-cache leaf.

So the design's §3 hypothesis — "the shared-cache/CS path re-maps the page RO while the entry prot
reads 0x3" — is **REFUTED by value**: no such re-map exists in the Darwin-13 tree.

---

## 4. The contradiction is NOT a second writer

Combining this sweep with `docs/1012-f1a-static.md`:

- Above the apex: no source line strips `VM_PROT_WRITE` on the WRITE path (§1 there).
- At the apex: `prot = 0x3` measured (1012 press).
- Below/around the apex: no second RO-writer is reachable on the fault path (§1-§2 here).
- The apex L1 identity is measured (`_pmppmap == _pmpttbr & 0xFFFFFF00`).

That leaves the two shapes `records/1012-press-read.txt` names, both of which are **measurement /
identity** questions, not a missing writer:

1. **(c1)** the apex VA is not provably the storm's `far` (probe-keying) — being checked separately.
2. **(c2)** the PTE `pmap_pte` returns is not the slot the MMU walks — a **table-identity** fault.
   The by-value reading of `pmap_pte` (`pmap.c:1786`) is:
   ```
   pte = L1_PTE_ADDR(tte);                       /* bits[31:10] of the L1 descriptor */
   ptep = phys_to_virt(pte) + pte_offset(virt);  /* the L2 table, via the kernel linear map */
   ```
   The walk's own tail (`_ptel1 = 0x81bb1001`, a COARSE L1) says the walk and the MMU should agree on
   the L2 base only if the walk's L1 base equals `TTBR0 & mask(TTBCR.N)` — and the tail `_ptel2 = 0`
   with a present L1 COARSE is **inconsistent with an FS=15 level-2 fault**, which requires a present
   L2 descriptor. So **(c2)'s identity check is unread** and should be the 1013 cell: read the leaf
   the pmap actually wrote (`pmap_pte(pmap, 0x2e000)`) and the leaf the MMU walks, and compare.

**One structural note worth a press (not a second writer, but adjacent):** `pmap_flush_tlbs`
(`pmap.c:1288-1318`) only flushes when the pmap passes the `ttb == (ttbr & 0xFFFFFF00) || pmap_is_shared`
test. On this boot `real_ncpus == 1` (`mp.c:38`) and `cpu_running == 1`
(`arm_init.c:113`; `cpu.c:1056`), so `flush_self` is set and `arm_tlb_flushID()` runs — the flush is
sound **provided the pmap's `pm_l1_phys` equals the live TTBR0**. 1012 measured that they are equal,
so the flush fired. This confirms the flush is not the loss; it does not introduce a writer.

---

## 5. Files read (by value)

- `external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.c` — `pmap_vm_prot_to_page_flags:1095`,
  `pmap_flush_tlbs:1274`, `leo_pte_build:1639`, `leo_enter:1648`, `leo_change_wiring:1683`,
  `leo_remove_range:1702`, `pmap_expand_ttb:3036`, `pmap_expand:3112`, `pmap_init` loops `:3596/:3640`,
  `pmap_remove_range:3697`, `pmap_page_protect:3943`, `pmap_protect:4225`, `pmap_nest:4337`,
  `pmap_pte:1786`.
- `external/xnu-hd2-darwin13/xnu/osfmk/vm/vm_fault.c` — `vm_fault_enter:2331` (codesign block
  `:2439-2510`, taint disconnect `:2567`, `PMAP_ENTER_OPTIONS :2585`), RetryFault strip `:3873/:3855`,
  `pmap_disconnect` sites `:1841/:2082/:2567/:3458/:4269`.
- `external/xnu-hd2-darwin13/xnu/osfmk/vm/vm_map.c` — `vm_map_lookup_locked` `:9061/:9139/:9166`,
  `vm_map_protect` `:3673/3683`, `vm_object_pmap_protect` callers, `vm_map_copy` `:11225/11240`.
- `external/xnu-hd2-darwin13/xnu/osfmk/vm/vm_object.c:2870` (`vm_object_pmap_protect`),
  `memory_object.c:231`, `vm_shared_region.c:626-635/1285/1330/1362/1853-1901`.
- `external/xnu-hd2-darwin13/xnu/bsd/kern/kern_proc.c:2742` (`cs_invalid_page`).
- `external/xnu-hd2-darwin13/xnu/osfmk/arm/trap.c:657-910` (abort → `vm_fault` → retry).
- `src/entry/entry_stubs.c:2203-2243` (the walk's temporary section alias, restore),
  `:2424` (`entry_note_abt_pte`), `:2643` (`entry_note_abt_pmp`).

---

## 6. Conclusion

**No.** There is no reachable second read-only writer for the leaf of `(user pmap, va=0x2e000)` on the
WRITE-fault path, and `far = 0x2e000` is **not** a shared-cache mapping (the shared region is
`[0x2c000000, 0x40000000)`; the storm VA is ~184 KiB). The codesign line the premise cites is about
`0x2de7e000` (= shared base + slide), a different page, and it cleared `CS_VALID` and **proceeded**.
The F1a site is therefore **not** a strip and **not** a CS/shared-cache re-map — it is the
table-identity question (c2), which is unread and is the natural 1013 cell.