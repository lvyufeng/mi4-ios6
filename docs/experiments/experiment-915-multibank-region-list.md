# 915 — The multi-bank region list: teaching Darwin-13's ARM32 kernel to recognize the Mi 4's 3 GB (2026-10-08)

913 §4.4 deferred the 3 GB clause to a "region-list port". This doc fixes what that port *is*, from
the measured boot-time machinery of `external/xnu-hd2-darwin13/xnu` (read this session) and the
measured bank map (911 §22). It is written before any code because the first thing the measurement
reveals is a hard architectural tension that decides the whole design — and a port designed around a
wrong reading of it would compile, boot, and still not recognize 3 GB.

## 1. The map to carry, and why `memSize` cannot carry it

Measured off the running stock Android kernel, free, no press (911 §22, `adb -s 4a2fe00b`), and
corroborated by two independent sources (`/proc/device-tree/memory/reg` and `/proc/iomem`):

| bank | base | size | span |
|---|---|---|---|
| 0 (low)  | `0x00000000` | `0x60000000` | 1.500 GiB |
| 1 (high) | `0x80000000` | `0x60000000` | 1.500 GiB |

Total `0xc0000000` = exactly 3.000 GiB. The banks are **disjoint** — the gap `[0x60000000,
0x80000000)` is not RAM.

`boot_args` has **no** region-list field for ARM (`pexpert/pexpert/arm/boot.h:66-78`: `Revision,
Version, virtBase, physBase, memSize, topOfKernelData, Video, machineType, deviceTreeP,
deviceTreeLength, CommandLine`). `memSize` is one scalar, and the kernel reads it as one span
(§2). So the map must arrive by a channel the ARM boot path does not yet read, and the kernel must
be taught to read it.

## 2. The walls, measured (each with its anchor)

These are the facts that bound the design; each was read in the tree this session, not assumed.

1. **`phys_to_virt` is a single linear offset** — `osfmk/arm/pmap.h:192-193`:
   `virt_to_phys(p) = (p - gVirtBase) + gPhysBase`, `phys_to_virt(p) = (p - gPhysBase) + gVirtBase`.
   The subtraction is `unsigned long` truncated to `unsigned int`. **Any PA below `gPhysBase`
   underflows**; bank 0 (`< 0x80000000`) is exactly that case.
2. **One interval enters the VM.** `avail_end = gPhysBase + gMemSize` (`arm_vm_init.c:402`);
   `avail_start = first_avail`, `vm_first_phys = first_avail` (`osfmk/arm/pmap.c:2668-2670`).
   `pmap_next_page` is a monotonic bump allocator over that single interval
   (`pmap.c:2017-2031`), and `pmap_startup` frees exactly `pmap_free_pages()` pages out of it
   (`vm_resident.c:786-911`). There is **no** list of ranges anywhere in the path.
3. **The interval is also the validity gate.** `pmap_valid_page` is
   `va > avail_start && va < avail_end` (`pmap.c:2152-2156`); `pmap_zero_page` guards the same
   (`pmap.c:1955`). A second bank outside `[avail_start, avail_end)` is not merely unmapped — it is
   *invalid*.
4. **The extension hooks are dead or hostile.** `pmap_add_physical_memory` unconditionally
   `panic("Forget it! You can't map no more memory, you greedy puke!")` (`pmap.c:4492-4496`).
   `mem_region_t` / `pmap_mem_regions[PMAP_MEM_REGION_MAX=26]` / `pmap_mem_regions_count` are
   **declared** (`arm/pmap.h:295-305`) and **never defined or referenced** — x86-port boilerplate;
   the ARM64 stubs only `panic("not implemented")`. So `PMAP_MEM_REGION_MAX` is a vestigial hook,
   not a working one.
5. **One mapper, one contiguous range.** `l2_map_linear_range(pa_cache_start, phys_start,
   phys_end)` (`arm_vm_init.c:174-203`) writes one 4 KB PTE per page across a **contiguous**
   `[phys_start, phys_end)` into one L2 table; it cannot skip a hole. A second bank needs a second
   call with a **different** L2 table bound into a different L1 section
   (`l2_cache_to_range`, `arm_vm_init.c:239-277`).
6. **No non-linear translation exists.** The ARM port has exactly two VA windows, both with the
   *same* linear offset: the identity window (`identityBaseVA = gVirtBase`, `arm_vm_init.c:335`)
   and the managed window (`MANAGED_BASE = 0xC0000000`, `:142`). `high_physmap`/`PHYSMAP_PTOV`
   exist only under `osfmk/i386`/`x86_64`; nothing in the ARM path maps `VA ≠ PA + constant`.
7. **The tree currently *rejects* multi-bank.** The iOS-lab port's policy requires
   `bank_count == 1` (`pexpert/arm/IOS7LeoMemoryPolicy.h:64-71`) and
   `pexpert/pexpert/arm/leo_handoff.h:34-39` documents a validated single-bank suffix. There is no
   kernel multi-bank path to extend.

## 3. The tension this port must state before it can be designed

**The kernel's virtual address space is smaller than the RAM.** `pmap_kernel_va` is
`[0x80000000, 0xFFFEFFFF]` (the kernel-base constant, `mi4-kernel-base-is-a-compile-time-constant`),
≈ **2 GiB** available to the kernel. The RAM is **3 GiB**. Therefore:

> **A full linear mapping of both banks into the kernel's address space is impossible on ARM32.**
> No `memSize`, no region list, and no page-table trick changes this: 3 GiB of physical memory
> cannot live linearly in < 2 GiB of kernel VA.

This is the whole reason the clause cannot be met by "raising a number", and it forks the design:

- **(A) Non-linear physmap + on-demand managed mapping.** Stop keeping a permanent linear map of
  all RAM; let `pmap` map physical pages into the managed VA window as they are used (what a 32-bit
  kernel with > 2 GiB normally does). This is the *general* answer, but it is a rewrite of a port
  whose every accessor assumes `phys_to_virt` is a constant offset — high risk, and it touches the
  whole kernel, far beyond 911e.
- **(B) Report + managed region + a bounded bounce window.** The kernel **recognizes** 3 GB —
  reports it (`hw.memsize`/`sane_size`/`max_mem` = the region sum) and **arms the VM with both
  banks as allocatable regions** — while it keeps the linear map on the bank it runs in and reaches
  the other bank through a **fixed, bounded VA window** large enough for driver/DMA use. The two
  banks are both *known and accounted*; only the permanent linear map is single-bank, which is
  already true of the machine.

**Recommendation: (B), and say so honestly.** The goal's clause is 「能够正确识别xiaomi 4的3GB内存」
— *correctly recognize* the 3 GB. (B) makes the kernel report and account 3 GB and gives the VM both
banks; it does **not** pretend to linearly map addresses it structurally cannot. (A) is the
"complete" answer but its blast radius is the entire kernel and is out of scope for a bring-up
rung; it stays a named, deferred follow-on. §22 of 911 already framed exactly this fork as
"(a) region-list port … the only way `hw.memsize` reads ~3 GB" vs "(b) a reported total".

## 4. The defect this port must not repeat

The reason a wrong design here would "succeed and be wrong" is the project's most-repeated class:
**one value, two definitions**. `memSize` is already three-way split
(`mi4-the-three-memsize-definitions`); `MEM_SIZE_MAX` is spelled twice (`stage90.h:5040-5050`). If
the region list is read into a *fourth* total that disagrees with `gPhysBase + gMemSize`, the
kernel will boot, print a number, and map something else. So the port's first structural rule:

> **The region list is the single source of truth for "how much RAM", and every consumer
> (`sane_size`, `max_mem`, `mem_size`, `avail_end`, `hw.memsize`) is derived from it by one
> function.** A build/check refusal binds the reported total to the measured `0xc0000000` (the "a
> claim in a comment is not a check" rule), so a drifted definition cannot ship silently.

## 5. The shape of the change (design, not yet code)

The channel: the banks arrive as a **device-tree property** the entry image injects — the same
`/memory` node with a multi-cell `reg` the stock Android kernel reads (§1) — because `boot_args`
has no field for it and the DT is already handed over (`a->deviceTreeP`). XNU's DT walk
(`pexpert/arm/common/pe_identify_machine.c`) reads it into the region list.

Then, file by file on the Darwin-13 tree (staged through the port generator, never carried as a
bare edit — the `patch_vfs_conf_hfs_row.py`/`check_hfs_staged.sh` pattern):

| where | change | why |
|---|---|---|
| `osfmk/arm/arm_vm_init.c` | read the region list; set `max_mem`/`mem_size`/`sane_size` = **sum**; keep `gPhysBase`/`gMemSize` = the **boot** bank for the linear map; register the other bank(s) | the kernel runs in bank 1; bank 0 becomes a managed region, not a linear one |
| `osfmk/arm/pmap.c` | **define** `pmap_mem_regions`/`_count`; make `pmap_next_page` walk regions; make `pmap_valid_page`/`pmap_zero_page` range-aware (a page is valid if it lies in **any** region); `phys_to_virt` for a non-linear-bank page resolves through the §6 window | the single-interval assumptions are §2.2/§2.3 |
| `osfmk/arm/pmap.h` / new `arm/mem_region.h` | the region type + accessor (model the *shape* on `mem_region_t`, which is already declared) | one definition |
| `pexpert/arm/…` / the entry image | inject `/memory/reg` with the two measured banks | the channel (§1) |
| `pmap_add_physical_memory` | replace the `panic` with a real "add a region" (or leave it and never call it, with a refusal that the region path does not) | §2.4 |

## 6. The bounce window (for option B)

The low bank needs a reachable VA for any non-managed access (driver DMA, `pmap_extract` targets).
Reserve a bounded VA region — **not** 1.5 GiB, but a fixed window (candidate: a slice of the
kernel VA below the managed base, sized by a named constant, e.g. 64–256 MB) — and map the low
bank's pages into it on demand through a second L2 table (`l2_cache_to_range`, §2.5). The window's
size and base are **named constants with a build refusal** (the entry-group page-move rule,
`mi4-entry-group-page-move-pins-two-copies`), not literals spread across files. The exact size is a
§7-open number: it must be ≥ the largest single low-bank consumer (DMA burst / network buffer), and
that maximum is not yet measured.

## 7. What this doc does NOT decide (and the next press-free step)

- **The bounce-window size and base** (§6) — needs the largest low-bank DMA footprint, unmeasured.
- **Whether option (A) is ever taken** — named and deferred; its blast radius is the kernel.
- **AMFI** — a separate rung (913 §4.5); orthogonal to memory.
- **The DT property's exact name/shape** — chosen when the entry image is written, so the *reader*
  and the *writer* cannot drift (`mi4-one-value-two-definitions`).

**Next concrete step (press-free, host-side):** measure the largest low-bank DMA footprint from the
device's own DT `memory`/reserved-memory nodes and the driver set, which sizes the §6 window; then
write the region-list reader against it. Nothing on the device changes until a designed image
exists and the operator presses.

*Provenance: `arm_vm_init.c`/`pmap.c`/`pmap.h`/`boot.h` read this session (anchors above);
911 §22 for the bank map; 913 §4.4 for the rung. Device untouched — read-only `adb shell` for the
map (earlier session), no press here.*