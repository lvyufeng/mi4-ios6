# 911 — the added clause: XNU must recognize the Mi 4's 3 GB RAM and 16/32 GB storage

Date: 2026-10-07. **A host-side design + reconnaissance, no device action.** The goal gained a clause:
「安装好的xnu，能够正确识别xiaomi 4的3GB内存，16GB/32GB存储」. This doc maps what the port reports
today, what the device actually has, and what the OS can and cannot be told — because the answer is
not "change a number": the arm32 kernel in this tree is **single-span**, and 3 GB is not one span.

## 0. Where the boot is, so the clause is read in order

The clause sits **behind** the booting clauses. Residence (909) is still the wall every current arm
hangs on (~13.4 s, the 5th idle pass), and the storage clause (「挂载存储」) is already met (903/906).
So this is a *recognition* clause about numbers the OS reports, and it is the second time the goal has
been about a number rather than about a path (the first was 903's mount). Its two halves are **very
different in difficulty**, and that difference is the point of this doc.

## 1. What the port reports TODAY (host-side, read)

- **`boot_args.memSize = 0x05d00000`** (93 MB) and `memSizeActual = 0x05d00000`, from
  `src/xnu_boot_args_conformant.c:108` (the value), `:177`/`:184` (the two stores). The rationale
  (`:83-107`) says plainly it is the **contiguous span from PA 0 to the first memory hole**
  (`0x5d00000`), 1 MB aligned so XNU's 1 MB-stepping section loop "stops cleanly at the hole". It is
  *sourced* from the device's own map but is **only the first span**, ~3 % of the device.
- **`/defaults hw.memsize = 0x5e500000`** (≈1.47 GB), from `src/stage90_main.c:868`, defined as
  `RAM_CONSOLE_BASE - RAM_PHYS_BASE` = `0xde500000 - 0x80000000`. It is the extent of RAM the
  **payload's own RAM console sits under**, not the device's memory.
- `/memory reg = {0x80000000, 0x5e500000}` (`src/stage90_main.c:49-51`) — the same 1.47 GB.

Neither number is 3 GB, and (see §3) **neither one controls the answer by itself.**

## 2. What the device ACTUALLY has — and the fact that changes the shape

MSM8974Pro-AC (`cancro`, Mi 4, 3 GB; Mi 3 is the 2 GB 8974AB — same five DTBs, same holes; the split
is aboot's DDR/smem table, `board-8974.c` `smem_ram_ptable_init`, not a DT property).

**The device tree declares NO RAM size at all.** Its only `/memory` seed is
`device_type = "memory"; reg = <0 0>;` (`skeleton.dtsi:12-16`, "The bootloader will typically populate
the memory node"); **aboot injects the RAM banks** (its strings include *"Failed to add the primary
memory information addr"* / *"secondary banks memory addresses"*). The DT only *subtracts* — via
`qcom,memblock-remove` (format `<address size>`,
`Documentation/devicetree/bindings/arm/msm/msm_memory_hole.txt`):

    base  msm8974.dtsi:2390      <0x5d00000 0x7d00000  0xfa00000 0x500000>
    Pro   msm8974pro.dtsi:1756   <0x5a00000 0x7800000  0xfa00000 0x500000>   /* cancro uses THIS */

So cancro removes `[0x05a00000, 0x0d200000)` (120 MB) and `[0x0fa00000, 0x0ff00000)` (5 MB = SMEM,
`soc/qcom,smem@fa00000`). **Both holes are BELOW `0x80000000`.** The project's own `memSize` cites the
*base* `0x5d00000`, not the Pro `0x5a00000` — a known 3 MB discrepancy (`xnu_boot_args_conformant.c:88`,
`stage90.h:108`), harmless for the high bank but worth naming.

**XNU's usable RAM is one contiguous HIGH span**: the observed kernel `iomem` is
`80000000-de6fffff : System RAM`, i.e. `[0x80000000, 0xde700000)` (top = `RAM_TOP` in `stage90.h:30`),
≈1.51 GB, with the top 2 MiB the Android `ram_console` (`= RAM_CONSOLE_BASE 0xde500000`). **Because
every DT hole is under `0x80000000`, the high bank XNU runs in is *not* fragmented by them** — the
holes are exactly why the *low* PAs `[0, 0x80000000)` are not one clean span, and why the payload cut
`memSize` at the first hole (93 MB). The 3 GB is the low bank plus the high bank; the OS is told only
the low bank's first 93 MB.

## 3. What XNU arm32 (this tree) can be told, and what it cannot — the hard boundaries

Traced through `external/xnu-4570.1.46/` (read-only), each line load-bearing:

1. **`hw.memsize` (the sysctl) = `max_mem` = `mem_size` = `min(boot_args->memSize, xmaxmem,
   MEM_SIZE_MAX)`.** `arm_vm_init.c:351-359` sets `mem_size = args->memSize` and only *lowers* it
   against `memory_size` (the `xmaxmem` from `maxmem`/`hw.memsize`, `arm_init.c:280-285`) and the
   hard cap `MEM_SIZE_MAX`; `arm_vm_init.c:493-494` `max_mem = mem_size`;
   `bsd/kern/kern_mib.c:400` exposes `max_mem` as `hw.memsize`. **So `boot_args->memSize` is the
   decisive field; `hw.memsize` in the DT is an upper clamp only and can never raise it; and
   `memSizeActual` is irrelevant to the sysctl** (its one consumer is `ml_get_booter_memory_size()`,
   `machine_routines_common.c:579`).
2. **`MEM_SIZE_MAX = 0x40000000`** (`arm_vm_init.c:134`), enforced at `:357-358`. **1 GiB is a hard
   compile-time ceiling.** `hw.memsize` cannot exceed it without editing this macro.
3. **The kernel is single-span by construction.** There is no region list anywhere — `struct boot_args`
   (`pexpert/pexpert/arm/boot.h:45-59`) is flat, and the allocator uses one `first_avail`/`avail_end`
   pair (`pmap.c:3045-3048`) over one range (`arm_vm_init.c:399-400`
   `avail_end = gPhysBase + mem_size`), with a linear `pa_index(pa) = atop(pa - vm_first_phys)`
   (`pmap.c:373-380`). **A device whose RAM has holes cannot be described by one `memSize`** — any
   non-span address is treated as I/O (`pmap.c:2539`).
4. **The 32-bit kernel VA is the wall.** `VM_MIN_KERNEL_ADDRESS = 0x80000000`,
   `VM_MAX_KERNEL_ADDRESS = 0xFFFEFFFF` (`osfmk/mach/arm/vm_param.h:169-170`), and
   `phystokv(a) = a - gPhysBase + gVirtBase` is a **pure linear offset** (`vm_param.h:196`). With
   `virtBase = 0x80000000`, mapping PA `0xC0000000` yields VA `0x140000000` — beyond 32 bits. **3 GB
   cannot be linearly physmapped below `0xFFFEFFFF` on this tree.** This is the boundary that makes
   the RAM half a port project rather than a constant.

**Consequence, stated plainly:** the RAM clause cannot be met by the payload alone. It needs (a) either
the holes registered as I/O so one span covers only a *chosen* usable window ≤1 GiB, or a pmap/VA
change that meshes spans; and (b) `MEM_SIZE_MAX` raised if the reported number is to exceed 1 GiB and
(more importantly) a **high physmap window** if 3 GB is to be *mapped* rather than merely *reported*.

## 4. The storage half is nearly free — and the exact reason it is not free yet

**The card's real capacity IS already read, and IS already the source.** `src/entry/entry_storage.c:7035`
reads the whole card from EXT_CSD `SEC_CNT` (`st_ext_sec_count = ST_EXT_CSD_WORD(ST_EXT_CSD_OFF_SEC_CNT)`;
offset 212, `:3107`). The observed runtime value is `0x01d5a000` = 30,777,344 sectors ≈ **15.76 GB**
(`:3221-3222`) — this tree's card is the **16 GB** part; a 32 GB part reads ~2× that. **The 16-vs-32
distinction is therefore already *measured*.**

**What the OS sees today is the selected GPT partition's true extent — not a staged window, and not the
card.** The chain carries no clamp: the parser picks the entry with the largest `EndingLBA - StartingLBA`
(`:8367-8371`), `entry_storage_selected_count()` returns that extent verbatim (`:10191-10197`, refusing
only if the card read failed), and `stage90_root_media.c:1249-1250` publishes it to the card unit
(`ST_MEDIA_DRIVER = 2`) as `st_media_blockcount[2]`, reported through `DKIOCGETBLOCKCOUNT` (`:1003-1004`).
The strategy serves every block of it (`:787-799`), so there is **no "first N MB" cap** and no
`MAX_BLOCK` / staging-window constant on the capacity path. (The `2 GiB`/512 comparisons at `:8076`/`:8217`
are *readings* logged, not clamps; `ST_GPT_ARRAY_SCAN_MAX = 32u` caps GPT-array *sectors walked*, and this
disk's 7 is under it.)

**So the gap is exactly three things, all host-side and all in the media layer:**

1. **`st_ext_sec_count` never reaches any `st_media_blockcount[]`.** It feeds only range sanity cells and
   `entry_storage_selected_pages()` (`:10171-10179`, which reaches only the *staged* unit's `mi_size`).
   Every unit is RAM-disk (0), one staged sector (1), or the selected partition (2) — **there is no
   whole-card unit.**
2. **`DKIOCGETMEDIASIZE` is not implemented** — it is absent from the `st_media_ioctl` switch, so it falls
   to the `ENOTTY` default (`:1024-1025`). None of the handled ioctls (`:956-1026`) returns the card total.
3. **The card strategy is partition-locked at the byte level.** `st_medium_disk_bytes(2) = selected_count
   × 512` (`:539`), `st_medium_disk_base(2)` returns 0 (`:515-516`), and the strategy *always* adds
   `entry_storage_selected_lba()` (`:765-766`, `:788-789`). So even answering `DKIOCGETBLOCKCOUNT` with the
   card total would serve wrong bytes for any LBA outside the selected partition — the door itself
   (`entry_storage.c:7661`) is LBA-agnostic and could read any sector, but the layer above it cannot
   currently address one.

**The clause's storage half is thus an extension of the read path 903 built, not a new number invented
host-side**: add a whole-card unit (or un-partition-lock unit 2) whose `st_media_blockcount` is
`st_ext_sec_count`, implement `DKIOCGETMEDIASIZE`, and let its strategy address raw LBAs. That is rung
**911d** below, and it is the natural next step after the RAM verdicts — no boot-chain change, no new
device behaviour.

## 5. The design, as rungs (each a separate, pressable step — no arm built here)

The decisive fact from §2: XNU's RAM `[0x80000000, 0xde700000)` is **already one clean span** ~1.5 GB,
and the DT holes are all below `0x80000000`. So the obstacle is not fragmentation in the *usable* bank
— it is that `boot_args` carries only **93 MB** (the low bank's first span), and the high bank is never
named. Two further ceilings then gate the *number*: `MEM_SIZE_MAX = 1 GiB`, and the 32-bit kernel VA.

- **911a — name the high bank (the real, testable first rung).** Set `boot_args.memSize` to the high
  span's size. Two candidate values, and the honest one matters:
  - the **payload's own extent**, `0xde500000 - 0x80000000 = 0x5e500000` (1510 MiB) — the span the
    payload already maps and whose top it owns (the RAM console); or
  - the **true top**, `0xde700000 - 0x80000000 = 0x5e700000` (1512 MiB, `RAM_TOP`) — but that claims
    the 2 MiB the Android `ram_console`/ramoops sits in, so it is `memSizeActual`'s number, not a safe
    `memSize`.
  Set `memSizeActual` = `0x5e700000`, and set the DT `/defaults hw.memsize` **≥ `memSize`** (it is an
  upper clamp only — `arm_init.c:280-285`, `arm_vm_init.c:351-359`; a smaller value silently wins).
  This makes `hw.memsize` report **~1.5 GB, the device's real usable high bank**, without touching XNU.
  **This is the rung that is close**: a boot-args/DT edit (payload rebuild), same gate, no boot-chain
  change, no brick risk beyond the normal `fastboot boot`. Its verdict cell is the **boot-args log
  key `xnu_ba_mem_size`** (`src/xnu_boot_args_conformant.c:143` — the same family as `xnu_ba_phys_base`
  / `xnu_ba_virt_base`; note it is `xnu_ba_`, *not* the `xnu_live_*` console channel) and the sysctl
  reading.
- **911b — `MEM_SIZE_MAX` ≥ the real span** (`arm_vm_init.c:134`) so 911a's ~1.5 GB is not clamped to
  1 GiB. A one-line XNU compile-time edit.
- **911c — the 3 GB itself (the real work).** Reporting/mapping 3 GB needs the **low bank** too, i.e.
  either a region list through `arm_vm_init`/`pmap` (the kernel is single-span by construction —
  no `mem_region_t` anywhere, `pmap.c:373-380`/`3045-3048`/`arm_vm_init.c:399-400`), or a **high
  physmap VA window** — and `MEM_SIZE_MAX` raised to `0xC0000000`. The 32-bit VA ceiling
  (`vm_param.h:169-170`, linear `phystokv` at `:196`) makes the >1 GiB physmap a pmap change, not a
  constant. **This is a port in its own right.**
- **911d — storage size**: expose the full 16/32 GB. **Host-side verified (§4):** the card total is
  already read (`entry_storage.c:7035`, `st_ext_sec_count` = `0x01d5a000` on this 16 GB part) but never
  surfaced — the OS sees the selected partition's true extent, and `DKIOCGETMEDIASIZE` is unimplemented
  (`stage90_root_media.c:1024-1025`, `ENOTTY`). The rung is three media-layer changes: (i) a whole-card
  unit (or an un-locked unit 2) whose `st_media_blockcount` is `st_ext_sec_count`; (ii) implement
  `DKIOCGETMEDIASIZE`; (iii) a strategy that addresses raw LBAs without adding `entry_storage_selected_lba()`
  (`:765-766`, `:788-789`). No boot-chain change, no new device behaviour — the read path 903/906 built,
  extended.

**A caveat that bounds even 911a:** hoisting `memSize` to the high bank assumes the payload's identity
map and the section map both cover `[0x80000000, 0xde500000)`. The payload maps the whole image by
identity sections (`STAGE90_HIGH_ALIAS_BASE`), but **whether XNU's section map already covers all
1510 MiB, or only the 8 MB/93 MB window the runs have exercised, must be verified from the linked
`arm_vm_init`/`pmap` before the number is raised** — otherwise the kernel would be handed a size larger
than the map behind it, the mirror-image of 911a's own hazard. **That verification is the first step of
911a, not an assumption.**

**The clause's honest verdict: 911a/911b are buildable soon and would take `hw.memsize` from 93 MB to
~1.5 GB (the real usable high bank); 911c is the rung that meets the literal 「3GB内存」 and is a port
comparable to the ARM bring-up.** The storage half (911d) is close behind 903/906.

## 6. Risk, gates, and what this does NOT do

- **Nothing here touches the device.** No arm is built, no press, no partition write.
- The RAM change is a **boot-args / device-tree edit** (a payload rebuild), so it rides the normal
  `fastboot boot` gate; it does not touch the boot chain. Raising `memSize` past a real hole would
  make XNU map MMIO as RAM — **that is the brick-shaped risk**, and it is why 911a must set the value
  to a **real, hole-free span** and 911c must mesh rather than stretch.
- `MEM_SIZE_MAX` and the VA changes are **compile-time XNU edits**, so they are rebuild-heavy and slow
  to iterate — the same cost class as the ARM layer, not the payload.
- **The obligations are unchanged**: stage to master, never brick, `fastboot` reachable, presses are
  the operator's. Residence (909) still gates whether any of this can be *observed* on a device.