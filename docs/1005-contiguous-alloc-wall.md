# 1005 — is a large *contiguous physical* allocation the next wall? (READ-ONLY analysis, 2026-10-13)

Status: analysis, evidence-first. No source, no `out/stage90` file touched; this file is the only
artifact. Answers 5 questions about whether the four `IOS7LAB cache mapping` regions
(mapping 0 va=0x2f7f6000 size=0x0c266000 … total ~301 MiB) need a contiguous physical allocation,
and what the failure mode would be if something DID.

Sources read BY VALUE this pass:
- `external/xnu-hd2-darwin13/xnu/bsd/vm/vm_unix.c` (`shared_region_copyin_mappings`,
  `shared_region_map_and_slide_np`, `_shared_region_slide`)
- `external/xnu-hd2-darwin13/xnu/osfmk/vm/vm_shared_region.c` (`vm_shared_region_map_file`,
  `vm_shared_region_create`, `vm_shared_region_enter`, `vm_shared_region_slide_init`)
- `external/xnu-hd2-darwin13/xnu/osfmk/vm/vm_fault.c` (the file/pager fault path)
- `external/xnu-hd2-darwin13/xnu/bsd/vm/vnode_pager.c` (`vnode_pagein`)
- `external/xnu-hd2-darwin13/xnu/osfmk/vm/vm_kern.c` (`kmem_alloc`, `kmem_alloc_contig`)
- `external/xnu-hd2-darwin13/xnu/osfmk/vm/vm_resident.c` (`cpm_allocate`, `vm_page_find_contiguous`)
- `external/xnu-hd2-darwin13/xnu/osfmk/arm/arm_vm_init.c` (the PHYSICAL RAM window)
- `external/android_kernel_xiaomi_cancro/` (CMA/contiguous in the vendor kernel)
- `docs/1003-dyld-stall-design-draft.md`, `records/1003-stall-forensics-preliminary.txt`,
  `docs/970-experiments/experiment-970-the-window-is-ram-not-just-address.md`, and the live 1002
  capture `out/stage90/captures/recovered-unknown-arm-20261013-110813-last_kmsg.txt`

---

## 0. VERDICT (plain)

**(a) No.** Large-contiguous physical allocation is **NOT** the real next wall, and it is **not**
on the critical path at all. Every one of the four shared-cache regions is mapped as a **file-backed
memory object** (`map_port = file_object->pager`, `vm_shared_region.c:1058/1119`) whose pages are
brought in by **normal demand paging over scattered 4 KiB pages** (vnode pager → UPL ≤ 8192 pages →
one `vm_page_grab()` per page). Nothing in the cache path — map, slide, fault, page-in — asks for a
contiguous range, a superpage, a CMA pool, or a physical-address hint.

**(b)** The minimal rung that would *test* it is a read-only instrument (not a behaviour change):
publish `vm_page_free_count` at the 1003 stall sample and log every `cpm_allocate`/`kmem_alloc_contig`
return value — see §6. It is expected to read "plenty free, zero failed contiguous calls", i.e. a
negative control that closes the contiguity hypothesis for good.

**(c)** The physical headroom, measured in the live 1002 capture, is **~1505 MiB** (`avail_start
0x80016000 .. avail_end 0xde500000`, modeled free `0x5e4ea` = 385,258 pages), and only ~27% of the
301 MiB cache has even been touched when the boot stalls (537 × 512 B reads). So there is no memory
pressure and no fragmentation story that could fail a contiguous request, even if one existed.

---

## 1. Where are the shared-cache regions physically backed? Page-by-page, scattered.

The path, by value, from the wire line to the pages:

1. **The wire.** `shared_region_copyin_mappings` (`vm_unix.c:1185-1218`) copies dyld's
   `shared_file_mapping_np` array and prints the four `IOS7LAB cache mapping …` lines (`:1218`).
   These are *address/size/offset/prot* — no physical hint of any kind.

2. **The map.** `shared_region_map_and_slide_np` → `_shared_region_map` →
   `vm_shared_region_map_file` (`vm_shared_region.c:969`). For each mapping the port is set from the
   **file's pager**, not a physical range:

   ```c
   /* vm_shared_region.c:1053-1059 */
   if (mappings[i].sfm_init_prot & VM_PROT_ZF) {
       map_port = MACH_PORT_NULL;              /* anonymous, only for VM_PROT_ZF */
   } else {
       map_port = (ipc_port_t) file_object->pager;   /* the shared cache's UBC pager */
   }
   ...
   /* :1119 */
   kr = vm_map_enter_mem_object(
           sr_map, &target_address, vm_map_round_page(mappings[i].sfm_size),
           0, VM_FLAGS_FIXED | VM_FLAGS_ALREADY,
           map_port,                       /* <- the vnode pager port */
           mappings[i].sfm_file_offset,    /* <- a FILE offset, not a PA */
           TRUE, mappings[i].sfm_init_prot & VM_PROT_ALL,
           mappings[i].sfm_max_prot & VM_PROT_ALL, VM_INHERIT_DEFAULT);
   ```

   The flags are `VM_FLAGS_FIXED | VM_FLAGS_ALREADY` — **no** `VM_FLAGS_SUPERPAGE_MASK`, **no**
   `VM_FLAGS_ANYWHERE`, **no** physical hint, **no** `phys_contiguous`. (The anonymous branch
   `:1100-1115`, `vm_object_allocate`, is taken only for `VM_PROT_ZF` zero-fill mappings; the four
   cache mappings are all file-backed — `init`=5/0x23/1/1, the ZF bit is not among them.)

   The physical VA reservation is a **submap**, not a pinned run: `vm_map_create(pmap_create(NULL,
   0, is_64bit), 0, size, TRUE)` (`vm_shared_region.c:665`) with `size = 0x14000000` for the iOS7
   geometry (`:634`). A submap reserves address space only; no physical pages are allocated by
   creating it.

3. **The slide.** `_shared_region_slide` (`vm_unix.c:1513`) → `vm_shared_region_slide_init`
   (`vm_shared_region.c:1420`) copies the slide-info blob (`uap->slide_size`; measured
   `0x89000` = 548 KiB) via **`kmem_alloc`** (`:1502-1504`) and `copyin`. `kmem_alloc` is the
   *scattered* allocator, not the contiguous one (see §2). So even the one real kernel allocation in
   the slide path is non-contiguous. It is wired kernel VA, not the cache's own pages.

4. **The page-in.** A fault on a shared-cache page is an ordinary file-backed fault:
   `vm_fault.c:1535` `memory_object_data_request(pager, …)` → `vnode_pagein` (`vnode_pager.c:497`).
   `vnode_pagein` bounds the I/O by `MAX_UPL_SIZE`:

   ```c
   /* vnode_pager.c:531 */
   if (size > (MAX_UPL_SIZE * PAGE_SIZE)) { result = PAGER_ERROR; ... }
   /* memory_object_types.h:401 */
   #define MAX_UPL_SIZE 8192                     /* = 32 MiB per I/O, split into 4 KiB pages */
   ```

   It builds a UPL of up to 8192 pages and commits them one page at a time
   (`vnode_pager.c:556, 613, 616`). The pages themselves come from the **global free list** via
   `vm_page_grab()` (single page, no contiguity — `vm_fault.c:1340`), exactly as in `kmem_alloc`'s
   own page loop (`vm_kern.c:337-339`). The pager path never calls `cpm_allocate`.

**Conclusion Q1: page-by-page, scattered.** A 207 MiB mapping (mapping 0) is 54,502 independent
4 KiB pages, pulled on demand, each from wherever the free list hands it out. No contiguous range is
reserved or required anywhere.

---

## 2. The contiguous / CMA machinery in both trees

### D13 tree (`external/xnu-hd2-darwin13/xnu`)

- **`cpm_allocate`** — `vm_resident.c:4251`. The contiguous physical allocator's core:
  ```c
  pages = vm_page_find_contiguous(npages, max_pnum, pnum_mask, wire, flags);
  if (pages == VM_PAGE_NULL) return KERN_NO_SPACE;   /* the failure mode */
  ```
  It scans the free list for a run of `npages` **physically adjacent** pages and returns
  `KERN_NO_SPACE` if none exists. This is the ONLY thing that would care about a fragmented pmap.
- **`kmem_alloc_contig`** — `vm_kern.c:110`. Calls `cpm_allocate` (`vm_kern.c:166`).
- **`kmem_alloc`** — `vm_kern.c:~240`. The scattered allocator: a per-page `vm_page_grab()` loop
  (`vm_kern.c:318-339`), `vm_page_alloc(object, offset)` (`:748`). **No contiguity.**
- **Superpages** — `vm_map.c:2107` `cpm_allocate(SUPERPAGE_SIZE, …)` with
  `sp_object->phys_contiguous = TRUE` (`vm_map.c:2115`); `vm_map.c:2881-2887` (the `kmem_alloc_contig`
  companion). `VM_MEM_SUPERPAGE` (`pmap.h:530`).
- **`phys_contiguous`** — set in exactly two places: `device_vm.c:252` (device/"physically-contiguous"
  memory entries) and `vm_map.c:2115` (superpages). `vm_fault.c:932-939` short-circuits to
  `phys_contig_object` only for an object with `!pager_created && phys_contiguous` — i.e. device
  memory or a large-page object. A UBC file object is neither.

**Callers of the contiguous allocator** (`grep kmem_alloc_contig|cpm_allocate`): `bsd_stubs.c:72`
(`mbuf` clusters), `iokit/Kernel/IOLib.cpp:401`, `iokit/Kernel/IOMapper.cpp:216`,
`arm/pmap.c:3060` (L1/L2 page-table expansion), `vm_map.c` (superpages), and `vm_resident.c:1786`
(a single `KMA_LOMEM` page). **None is on the shared-cache / shmem path.**

### Vendor kernel (`external/android_kernel_xiaomi_cancro`)

The Android kernel has the Linux **CMA** (`mm/page_alloc.c:5932/5993` `zone->cma_alloc`,
`migratetype == MIGRATE_MOVABLE && !zone->cma_alloc` at `:1144`) and `dma_alloc_from_contiguous`
family. But that CMA pool belongs to the **vendor (Android) kernel**, which is a *separate image* —
it is not the running kernel when XNU boots. XNU does not use the vendor kernel's CMA; the two trees
do not share a heap. So the vendor CMA is irrelevant to XNU's shared-cache backing.

---

## 3. What `vm_map_enter`/`vm_fault` on a shared-cache vnode actually does

**Normal demand paging over scattered pages.** Walked in §1: the mapping is a file-backed memory
object (`map_port = file_object->pager`), the fault issues `memory_object_data_request`
(`vm_fault.c:1535`), the vnode pager reads up to `MAX_UPL_SIZE` (8192) pages as a UPL and commits
them into the object's 4 KiB page slots (`vnode_pager.c:556-616`). The `phys_contiguous` branch
(`vm_fault.c:932`) is **not taken** — the object has a pager and is not contiguous. The superpage
flag is **not set** in the enter. No contiguous physical RAM is needed: a 207 MiB cache region
demand-faults into 54,502 scattered 4 KiB pages.

The only kernel allocation in the whole path is the 548 KiB slide-info buffer (`kmem_alloc`,
`vm_shared_region.c:1502`), which is itself **scattered**.

---

## 4. If something DID need contiguity: exact symbol and failure mode

For completeness (since nothing on this path does): the contiguous request would be
`cpm_allocate(size, …)` (`vm_resident.c:4251`), returning **`KERN_NO_SPACE`** when
`vm_page_find_contiguous` finds no run (`vm_resident.c` in `cpm_allocate`). `kmem_alloc_contig`
(`vm_kern.c:110`) propagates that return to its caller (e.g. `mb_init`/`mbuf` in `bsd_stubs.c:72`).
If the request came through the shmem syscall path (`shared_region_map_and_slide_np`) the
`KERN_NO_SPACE` maps to `ENOMEM` (`vm_unix.c:1454-1456`: `case KERN_NO_SPACE: error = ENOMEM;`), so
a hypothetically-contiguous cache map would fail with `shared_region_map_and_slide_np` returning
`ENOMEM`.

**Would D13's pmap actually fail such a request?** By value, no fragmentation pressure exists:
`avail_start = 0x80016000`, `avail_end = 0xde500000` (capture @:1924/@:1925), and the boot models
`free_page_count = 0x0005e4ea` = **385,258 pages ≈ 1505 MiB free** (@:1964; cross-check:
`0xde500000 - 0x80016000 = 0x5e4ea000` bytes = `0x5e4ea` pages exactly). A 301 MiB contiguous run
would need only ~77k pages — well within a pristine ~1505 MiB free region on a fresh boot. So even a
genuinely-contiguous request would succeed today; the wall is not here. (This is the *physical*
free pool; it is unrelated to the *VA* managed-window ceiling of 956/970, which is a separate
`MANAGED_BASE`/L1 concern, not addressed here.)

---

## 5. Prior conclusions in the 1003 artifacts (not re-derived)

- `records/1003-stall-forensics-preliminary.txt` §1/§2 reads the four mappings and the slide
  (`base 0x2c000000, slide 0x37f6000, slide_start 0x3f9a2000, size 0x89000`), confirms
  `map_slide ret=0`, and establishes the mapping is file-backed with m0's first page
  `0x2f7f6000` = the `cs_invalid_page` address. It never raises contiguity.
- `docs/1003-dyld-stall-design-draft.md` describes the cache as **pages being paged in**
  ("the machine is paging the shared cache LINEARLY … one 512 B read per ~10 KV lines"), i.e.
  demand paging, and it explicitly rules out a "blocked on a page-in" story (§4 of the forensics).
  Neither prior artifact names any contiguous-allocation requirement.
- `docs/970-experiments/experiment-970-the-window-is-ram-not-just-address.md` is about the *VA
  window* (`gMemSize`/`avail_end`), and §7 explicitly says the 484 MiB window is about
  address-space reach, not about a contiguous physical run for the cache. The current boots run a
  **1509 MiB** window (`memSize=0x5e500000`, capture @:43).

**No prior conclusion asserts or depends on contiguity.** The 301 MiB cache is treated throughout as
demand-paged.

---

## 6. Conclusions

**(a) Is large-contiguous allocation the real next wall? NO — it is already handled by scattered
demand paging.** The four `IOS7LAB cache mapping` regions are file-backed memory objects faulted in
page-by-page through the vnode pager (UPL ≤ 8192 pages, one `vm_page_grab` each). The contiguous
allocator (`cpm_allocate`) is not on this path, the superpage/`phys_contiguous` machinery is not
engaged, and the only kernel allocation in the path (the 548 KiB slide buffer) is scattered
`kmem_alloc`. The real frontier remains the 1003 **dyld stall** (commpage `mach_absolute_time`
fallback + `ImageLoader::link`), which is a control-flow/ABI question, not an allocator question.

**(b) The minimal rung that would TEST it (read-only, no behaviour change).** Mirror 1003's
instrument shape (gated by a `STAGE90_XNU_*` switch, verified by value):
1. In the per-tick stall sample (the 1003 `entry_note_stall_sample` site) publish
   `xnu_live_vm_free_count` = `vm_page_free_count` (and, optionally, `vm_page_free_target`), so the
   next press reads the free pool **at the stall**.
2. Wrap `cpm_allocate` (`vm_resident.c:4251`) and `kmem_alloc_contig` (`vm_kern.c:110`) with a
   `--wrap` probe that publishes the return `kr` (`xnu_live_cpm_kr`, `xnu_live_cpm_n`).
Expected reading (the negative control that closes the hypothesis): free count ≈ 1.4 GiB and
`xnu_live_cpm_n` = 0 or all-`KERN_SUCCESS` on the shared-cache path — i.e. contiguity never even
attempted, never failed. A non-zero `KERN_NO_SPACE` count would be the falsifier that reopens this.
Note: this is a strictly additive instrument; it must NOT add a `cpm_allocate` call of its own (that
would change behaviour and defeat the test).

**(c) Exact evidence.**
- File-backed map, no physical hint: `vm_shared_region.c:1058` (`map_port = file_object->pager`),
  `:1119-1131` (`vm_map_enter_mem_object`, `VM_FLAGS_FIXED | VM_FLAGS_ALREADY`).
- Submap (address-only) reservation: `vm_shared_region.c:665` (`vm_map_create`, `size 0x14000000`).
- Scattered slide alloc: `vm_shared_region.c:1502` (`kmem_alloc`); scattered page loop
  `vm_kern.c:318-339,748`.
- Demand-page fault: `vm_fault.c:1535` (`memory_object_data_request`); `vnode_pager.c:531,556,613`
  (`MAX_UPL_SIZE 8192` = `memory_object_types.h:401`).
- Contiguous failure mode (unused here): `vm_resident.c:4251` (`cpm_allocate` → `KERN_NO_SPACE`);
  shmem errno mapping `vm_unix.c:1454-1456` (`KERN_NO_SPACE → ENOMEM`); `kmem_alloc_contig`
  `vm_kern.c:110`.
- `phys_contiguous` only for device/superpage: `device_vm.c:252`, `vm_map.c:2115`; fault
  short-circuit `vm_fault.c:932`.
- Free headroom: capture `recovered-unknown-arm-20261013-110813-last_kmsg.txt` @:43
  (`memSize=0x5e500000`), @:1924/1925 (`avail_start=0x80016000`/`avail_end=0xde500000`), @:1964
  (`modeled_free_page_count=0x0005e4ea`).
- Vendor CMA is a different image: `android_kernel_xiaomi_cancro/mm/page_alloc.c:5932,1144`.

## UNFINISHED / unverified
- "No shared-cache-path caller of `cpm_allocate`" is established by grep of the call sites listed in
  §2; a runtime wrap (§6b) would make it a *reading* rather than a static claim. UNFINISHED until
  the 1005 arm is pressed.
- The "~1505 MiB free" figure is the pmap's *modeled* free count from the entry bootstrap
  (@:1964); the *live* `vm_page_free_count` at the stall is not in any capture yet — that is exactly
  what §6b's probe would publish. UNFINISHED.
- Whether the *stall itself* is a page-in wait is out of scope here and is the open 1003 question
  (forensics §3 argues against it: no storage reads in the tail).