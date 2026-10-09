# 964 — the 915-B region-list port, corrected against 958: the total rides `max_mem` alone (2026-07-…)

915 designed the multi-bank region list before 958 existed. 958 then implemented the "reported total"
half a different way (`/defaults hw.memsize` → `max_mem`), and **915 §5's design row is now stale in a
way that would produce the exact defect this project names most often.** This rung records the
correction before any code, because a 915-B builder following 915 §5 verbatim would build the
one-value-two-definitions bug on purpose.

## 1. The defect 915 §5 now carries

915 §5, line 121:

> `osfmk/arm/arm_vm_init.c` | read the region list; set **`max_mem`/`mem_size`/`sane_size` = sum**; keep
> `gPhysBase`/`gMemSize` = the boot bank for the linear map; register the other bank(s)

Two problems, both measured against 958 §3:

1. **`max_mem` would be double-defined.** 958 already sets `max_mem` from `/defaults hw.memsize`
   (`arm_vm_init.c`, the guarded `PE_get_default` block). A port that also sets `max_mem` = the reg sum
   gives one quantity two writers — [[mi4-one-value-two-definitions]]. (They agree on this device, which
   is exactly what makes the bug silent: the day the reg and the DT property disagree, the kernel
   reports one and the region path believes the other.)
2. **`mem_size`/`sane_size` must NOT be raised.** 958 §3 measured them as a *different thing* from
   `max_mem`: `mem_size` sizes the pmap's page tables (`pmap.c:3492-3526`) and `sane_size` sizes
   kalloc and the zones (`kalloc.c:350`, `vm_init.c:154`). Setting them to the 3 GB sum while the
   allocator still owns ONE region is the allocator over-promising — and 917's lesson already says a
   multi-region pmap **sizes its tables from the first region**, so `sane_size` = the sum is wrong even
   in a full region-list design. 915 §5 was written without that measurement.

## 2. The corrected 915-B shape

The reported total is **done** (958). The region-list port is *not* about the total at all — it is about
two other things, and this is the correction:

| where | change | what it is FOR (not the total) |
|---|---|---|
| `osfmk/arm/arm_vm_init.c` | read the region list; **do NOT touch `max_mem`/`mem_size`/`sane_size`**; use the list to **select** the boot region and keep `gMemSize` = that region | selection, not reporting |
| `osfmk/arm/pmap.c` | make `pmap_next_page` / `pmap_valid_page` / `pmap_zero_page` **region-aware** (a page is valid in any region) | allocation from the *other* region |
| `osfmk/arm/pmap.h` / new `arm/mem_region.h` | the region type + accessor | one definition |
| the entry image | inject the region list in a channel XNU reads | the data |
| `phys_to_virt` (bounce window) | map a non-boot-region page into a bounded VA window on demand | **mandatory** — see §3 |

So the goal's clause 「能够正确识别 3 GB」 is already **met** (958 reports it). What 915-B adds is the
next word the goal does not strictly use but the *port* implies: **the low bank's pages become
allocatable**. That is a distinct, harder claim, and it must not re-open the total.

**A build refusal binds it:** with the region list present, `max_mem` has exactly ONE writer (the DT
property, 958), and `mem_size`/`sane_size` are set from the selected region alone. A region port that
writes `max_mem` is a refusal, not a preference ([[mi4-a-claim-in-a-comment-is-not-a-check]]).

## 3. Why there is no smaller safe slice than "the whole region port"

The tempting bounded rung is *just* the reader: walk `/memory/reg`, publish the two banks. But **958
already publishes the total** — a second reader writing a second total is the §1 defect. And the
*allocator* half cannot be sliced, because:

> Any page the allocator hands out must be resolvable by `phys_to_virt`, which is a **single linear
> offset** (`pmap.h:192-193`, `p - gPhysBase + gVirtBase`). A page in a second region underflows that
> subtraction, so it maps to a wrong VA with no fault — unless the **bounce window** exists first.

So the order is forced: (1) region-aware `pmap_valid_page` + a second-region allocator, (2) the bounce
window that makes those pages reachable, (3) only then can any consumer use a low-bank page. Step 2 is
`l2_cache_to_range` into a reserved VA slice (915 §6), sized by a named constant with a build refusal
([[mi4-entry-group-page-move-pins-two-copies]]). There is no rung that hands a caller a low-bank page
before step 2, and no rung that makes step 2 meaningful before step 1.

## 4. What blocks the *build*, and what would unblock it

915 §7 named the open input: **the largest single low-bank DMA footprint** (which sizes the bounce
window). Measured this session, read-only `adb -s 4a2fe00b`: this device's DT has **no
`reserved-memory` node** (`/proc/device-tree` lists `memory` but no `reserved-memory`), so the
footprint cannot be read from the DT — the low-bank carveouts 957 found are in Android's `/proc/iomem`
and the SMEM RAM-partition table 911c already reads, not a separate DT node. So the window's size is
still a design input, and the honest statement is: **the port is designable, not buildable, until the
window size is chosen** — and choosing it wrong (too small for a DMA burst) is a runtime failure, not a
build one, which is precisely the class 958 avoided by not building this at all.

## 5. Status

- Clause 「正确识别 3 GB」: **met** (958 + 911c), host-verified end-to-end this session
  (`max_mem` → `machine_info.max_mem` at `startup.c:198` → userland `host_basic_info.max_mem` at
  `host.c:182`; the kernel object carries the `bl PE_get_default` → store).
- Clause 「16/32 GB」: **met** (911d card-raw unit), host-verified.
- The region port (low bank *allocatable*): **designed here, corrected against 958; not built.** It is
  whole-kernel, its one open input (bounce-window size) is unmeasurable from this device's DT, and it
  must be built on a *pressed* 958 so the reported total is observed before anything is layered on it.

**PRESS IS THE OPERATOR'S.**

*Provenance: `external/xnu-hd2-darwin13/xnu` `arm_vm_init.c`/`pmap.c`/`pmap.h`/`startup.c`/`host.c`
read this session; 958 §3/§6 and 915 §5/§6/§7 read this session; the DT absence of `reserved-memory`
read live via read-only `adb -s 4a2fe00b` (no press, inside the gate). Device unmodified. Follows
[[mi4-915-multibank-region-list-design]] and [[mi4-958-3gb-rides-the-report]].*