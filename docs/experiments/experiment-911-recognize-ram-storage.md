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

## 1. What the port reports TODAY — **the live number is 16 MB, and it is a third definition**

**Correction (2026-10-07, host-side).** The earlier draft of this section quoted
`src/xnu_boot_args_conformant.c`'s `memSize = 0x05d00000`. **That is not the boot_args XNU is handed.**
There are **three** definitions of the handoff size in the tree, and the one on the live path is the
smallest ([[mi4-one-value-two-definitions]]):

| definition | `physBase` | `memSize` | on the live path? |
|---|---|---|---|
| `src/xnu_boot_args_conformant.c:69/80/108` | `0x0` | `0x05d00000` (93 MB) | **no** — module not called |
| `src/boot_args.c` (payload's own) | `STAGE90_BASE` | `RAM_CONSOLE_BASE-RAM_PHYS_BASE` (1.47 GB) | only as the **source**, re-derived below |
| **`src/xnu_entry_jump.c` `xnu_entry_build_args` (`:124`; `memSize` at `:150`)** | **`0x80000000`** | **`STAGE90_XNU_ENTRY_SIZE` = `0x01000000` (16 MB)** | **YES — the copy XNU runs on** |

`xnu_entry_build_args` is reached from `stage90_xnu_entry_run` (`:176`), called once at
`src/xnu_kernel.c:243` under `#if STAGE90_XNU_ENTRY` — the last thing the payload does. It builds a
**second** boot_args inside the entry window (`ENTRY_ARGS_PA = 0x80000000 + offset`) with
`physBase == virtBase == 0x80000000` ("the whole trick") and `memSize = STAGE90_XNU_ENTRY_SIZE`
(= `@ENTRY_SIZE@` from `out/stage90/xnu_arm_entry.h`, `0x01000000`). **Observed on device**:
`xnu_entry_args_memSize=0x01000000`, `physBase == virtBase == 0x80000000`
(experiment-451 `:83-84`, experiment-444 `:159`) — a **16 MB identity window `[0x80000000, 0x81000000)`**,
not 93 MB and not 1.5 GB.

So the OS is told it has **16 MB** of RAM, at a base of PA `0x80000000`. The two other numbers are real
but *other* things: `xnu_boot_args_conformant.c`'s 93 MB is a roadmap-Phase-2 module the payload never
installs, and `/defaults hw.memsize = 0x5e500000` (`src/stage90_main.c:868`) / `/memory reg =
{0x80000000, 0x5e500000}` (`:49-51`) describe the span **the payload's own RAM console sits under**
(device-tree properties), not the kernel's `memSize`.

**And the payload's own dry-run chain uses the *first* copy, not the second — by design, no mismatch.**
`src/xnu_arm_vm_init_full_pmap.c:293-296` **asserts** `args->physBase == STAGE90_BASE` (`= 0x8000`!) and
`args->memSize == (RAM_CONSOLE_BASE - RAM_PHYS_BASE)` (`= 0x5e500000`). Those are exactly the values of
`src/boot_args.c`'s `build_boot_args` — so `full_pmap` consumes the **payload's own** boot_args (via the
loader → `stage90_xnu_start_stub` → `xnu_entry_stub.c:209`), a separate object from the entry copy. The
two copies are for two consumers and are internally consistent; **what XNU itself reads is the 16 MB
entry copy**, because `start.s` builds the section map from the boot_args pointer the jump was given (§3).

Neither the live 16 MB nor the 93 MB is 3 GB. And (§3) **the section-map builder is `start.s`, not
`arm_vm_init.c`** — so which of these numbers reaches `hw.memsize` turns on what `start.s` does with
`boot_args->memSize`, which §3 traces.

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
holes are exactly why the *low* PAs `[0, 0x80000000)` are not one clean span.

**What the OS is actually told, though, is neither 3 GB nor 1.5 GB nor even 93 MB — it is 16 MB**
(§1): `xnu_entry_build_args` hands XNU a **16 MB identity window `[0x80000000, 0x81000000)`,
`physBase == virtBase == 0x80000000`**. The 3 GB is the low bank plus the high bank; the high bank is
~1.5 GB; and the running kernel is described as a 16 MB window at the *top* of the low bank. The port's
size numbers are not merely small, they are **three disagreeing definitions** (§1).

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

**The section map is built in `start.s`, not `arm_vm_init.c` — and it is `memSize`-driven.** Verified
2026-10-07 from the linked source: `external/xnu-4570.1.46/osfmk/arm/start.s:106` loads `BA_MEM_SIZE`
into `r10`, and `_start`'s `mapveqp` loop (`:198-207`) emits exactly `memSize / 1 MiB` section TTEs,
stepping `r7` by 1 MiB and emitting VA `[virtBase, virtBase+memSize)` → PA `[physBase, physBase+memSize)`.
`arm_vm_init.c` only *copies and re-protects* that boot table (`:370-376`, full 4096-entry L1 tables —
no fixed array is overflowed by a larger `memSize`; 1510 entries ≪ 4096). **So the map covers exactly
the span `memSize` names, and nothing else** — the "does the map already cover the high span" question is
answered the same way as the number: it covers 16 MB (the entry window `[0x80000000, 0x81000000)`), no
more. The window is `ENTRY_SIZE = 0x01000000` in both the linker (`build_entry.sh:28626`) and the
generated header (`xnu_arm_entry.h:7`), with `topOfKernelData` at `+8388608` (`:14`) — so `start.s`
maps the whole 16 MB the entry image, its BSS, the device tree and the boot_args tables live inside,
and **nothing above `0x81000000` is mapped at all**.

**Consequence, stated plainly:** the RAM clause cannot be met by the payload alone. Step 0 is confirming
that the value edited is the **16 MB entry copy** XNU's `start.s` reads (the `full_pmap` module's
`0x5e500000` is the payload's *own* boot_args, a separate consumer — §1). Then it needs (a) the mapped
span raised to the real high bank (≤1 GiB without touching `MEM_SIZE_MAX`),
and (b) either the holes registered as I/O so one span covers a *chosen* usable window ≤1 GiB, or a
pmap/VA change that meshes spans; and (c) `MEM_SIZE_MAX` raised if the reported number is to exceed
1 GiB and (more importantly) a **high physmap window** if 3 GB is to be *mapped* rather than merely
*reported*.

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

**Revised 2026-10-07 after the host-side traces.** The decisive fact from §2 stands — XNU's physical RAM
`[0x80000000, 0xde700000)` is **already one clean span** ~1.5 GB, and the DT holes are all below
`0x80000000` — but the *reported* number is **16 MB, and it is the entry window, not a bank** (§1), and
the section map is `memSize`-driven and covers exactly that (§3). Three ceilings gate the *number*:
the 16 MB window itself, `MEM_SIZE_MAX = 1 GiB`, and the 32-bit kernel VA.

- **911a0 — change the *right* one of the three `memSize` definitions (step 0, do this first).** The
  port has three size numbers (§1); only `xnu_entry_build_args`'s **16 MB** reaches XNU, and the
  `full_pmap` assert (`:293-296`, `physBase == 0x8000`, `memSize == 0x5e500000`) is the **other** copy,
  consumed by the payload's dry-run chain, correctly. So 911a0 is not a reconciliation but a discipline
  check: **edit the entry copy's `memSize` and nothing else**, and confirm from the linked image that the
  value XNU's `start.s` reads is that one ([[mi4-one-value-two-definitions]] — three definitions is
  exactly how a change lands on a copy nothing reads). Its verdict cell is the live
  `xnu_entry_args_memSize` (`xnu_entry_jump.c:172`).
- **911a — name the high bank (the real, testable first rung).** After 911a0, set the live
  `boot_args.memSize` to the high span's size. Two candidate values, and the honest one matters:
  - the **payload's own extent**, `0xde500000 - 0x80000000 = 0x5e500000` (1510 MiB) — the span whose
    top the RAM console occupies; **the payload does *not* currently map it** (see the caveat below), so
    both maps must be grown to it before this value means anything; or
  - the **true top**, `0xde700000 - 0x80000000 = 0x5e700000` (1512 MiB, `RAM_TOP`) — but that claims
    the 2 MiB the Android `ram_console`/ramoops sits in, so it is `memSizeActual`'s number, not a safe
    `memSize`.
  Set `memSizeActual` = `0x5e700000`, and set the DT `/defaults hw.memsize` **≥ `memSize`** (it is an
  upper clamp only — `arm_init.c:280-285`, `arm_vm_init.c:351-359`; a smaller value silently wins).
  This makes `hw.memsize` report **~1.5 GB, the device's real usable high bank**, without touching XNU.
  **This is the rung that is close**: a boot-args/DT edit (payload rebuild), same gate, no boot-chain
  change, no brick risk beyond the normal `fastboot boot`. Its verdict cell is the **live boot-args log
  key `xnu_entry_args_memSize`** (`src/xnu_entry_jump.c:172`) — **not** `xnu_ba_mem_size`, which the
  roadmap module `xnu_boot_args_conformant.c:143` emits but the live path never calls (this is the same
  three-definition trap as §1) — together with the sysctl `hw.memsize` reading.
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

**A caveat that bounds even 911a — and it is now RESOLVED (2026-10-07).** Hoisting `memSize` to the high
bank assumes the payload's identity map and XNU's section map both cover `[0x80000000, 0xde500000)`.
**They do not.** Measured:

- **XNU's section map covers exactly `memSize`** — `start.s:198-207` emits `memSize / 1 MiB` sections
  (§3). At today's live `memSize = 0x01000000` that is **16 sections `[0x80000000, 0x81000000)`**, and
  nothing above `0x81000000` is mapped. Raised to `0x5e500000` it emits 1510 sections — the *count* is
  not the blocker (the boot L1 tables are full 4096 entries), but 911a0 must first make `memSize` mean
  one thing (§1/§5.911a0).
- **The payload's own identity map does not cover the span either** (`src/mmu.c:5404` `build_identity_table`):
  low 1:1 is the **image only** (`:5416-5419`, `[0, __stage90_image_end)`); the high alias is a **64 MB
  window** capped at `STAGE90_IMAGE_ALIAS_LIMIT = 0xc4000000` (`:5434-5443`); the entry window is 16 MB
  (`:5476-5479`); and the only thing at `0xde500000` is the **2-section RAM-console window**
  (`:5462-5464`) — not RAM. So `[0x81000000, 0xde500000)` is unmapped by the payload too.

**Therefore 911a is not "raise a number": both maps must be extended to the span first**, or the kernel is
handed a size larger than the map behind it. The `stage90_l1_table` (`src/mmu.c:553`, 4096 entries) is
large enough to hold the whole L1 — the builder's loops simply stop at the image end and the fixed
windows, never at `0xde500000`.

**The clause's honest verdict (revised 2026-10-07 with the traces in §1/§3/§5-caveat):** the live port
tells XNU it has **16 MB** and maps exactly those 16 MB; `full_pmap`'s `0x5e500000` is the payload's own
boot_args, not the copy XNU reads (§1); and both the XNU section map and the payload identity map stop at
the entry window, nowhere near the 1.5 GB high bank. So **911a0 comes first** (edit the entry copy,
prove from the linked image that is the one XNU reads), then the map-extension work in **911a**
(payload identity map **and** the XNU window), then **911b** (`MEM_SIZE_MAX`), then **911c** (the
literal 「3GB内存」 — a pmap region list or a high physmap window, a port comparable to the ARM bring-up).
911a/911b remain *buildable* and are payload-plus-one-macro edits; 911c is the port. The storage half
(911d) is close behind 903/906 and fully traced (§4).

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
## 7. 911d BUILT (2026-10-07) — the storage half, host-side, NO press

Rung 911d is built and parked. It is **912a's switch set plus one key** (`STAGE90_XNU_CARD_TOTAL=1`),
so the press that sends it answers **both** the 912a memory question and this storage question in one
run. Set `armed-storage-605a43c2`, parked at `out/stage90/frozen/armed-storage-605a43c2/`.

**Design correction found at build time.** §4 named the mechanism `DKIOCGETMEDIASIZE`; that ioctl
**does not exist** in this tree's `bsd/sys/disk.h`. The OS reports media size via
`DKIOCGETBLOCKCOUNT` × blocksize, and `DKIOCGETBLOCKCOUNT` *is* handled (`stage90_root_media.c:1003-1004`)
— it answers with `st_media_blockcount[unit]`. So the rung does **not** add an ioctl; it adds a **unit**
whose block count is the card's own.

**What was built (all in the media layer, additive to 903's mount unit):**

- `src/entry/entry_storage.c` — `entry_storage_card_sectors()` returns the live `st_ext_sec_count`
  (EXT_CSD `SEC_CNT`, offset 212, read at `:7035`; `0x01d5a000` = 30,777,344 sectors ≈ 15.76 GB on this
  16 GB part). The zero is a refusal, and `st_gpt_best_extent` is deliberately NOT a condition — the raw
  unit is the *card*, not a partition.
- `src/platform/stage90_root_media.c` — a FOURTH unit `ST_MEDIA_CARD_RAW = 3` (`ST_MEDIA_DISKS 2 → 4`),
  registered **beside** the mount unit (2) from `__wrap_mdevlookup`. `st_medium_card_bytes()` returns
  `uint64_t` (30,777,344 × 512 = 15,757,999,872 B **overflows** a 32-bit `unsigned`, so `len` in the
  strategy widens to 64-bit under this switch). `st_medium_disk_base(3)` = 0 and `st_medium_disk_bytes(3)`
  = the card total; the strategy's card branch takes `base_lba = 0` for the raw unit instead of
  `entry_storage_selected_lba()`, and `DKIOCGETBLOCKCOUNT` answers unit 3 with the card's true count.
- The registration publishes `xnu_live_rootmedia_card_raw_registered`, `_card_raw_blocks`,
  `_card_raw_bytes_hi`/`_card_raw_bytes_lo`, `_card_raw_first_lba`/`_card_raw_last_lba`, `_card_raw_dev`,
  `_card_raw_err`, `_card_raw_refused`.
- Build plumbing: `STAGE90_XNU_CARD_TOTAL` in `ENTRY_ARM_KEYS`, the record writer, the `nm`-based
  two-script agreement check (`xnu_entry_911d`, mirroring `xnu_entry_905`), and the gate's
  `ENTRY_CFG_KEYS` + artifact-grounded check.

**The mount rung does NOT move.** Unit 2 (903's selected-partition card unit) is byte-unchanged; the
raw unit is a sibling. So every storage reading already true of 903/906 stays true, and this arm adds
only the capacity reading.

**The press is the operator's; the goal is NOT met until it is read.** The decisive cell is
`xnu_live_rootmedia_card_raw_blocks = 0x01d5a000` (or the larger SEC_CNT on a 32 GB part) with the
903 mount at `0x00400000+` unchanged — the recognition the goal's newest clause asks for. The refusal
is the same one 912a names: if `xnu_entry_args_memSize` still reads the 16 MB default, neither the
window nor this arm reached the live image.

**A DEFECT THE FIRST DRAFT CARRIED, AND THE CHECK THAT CAUGHT IT.** The first 911d build
(`6d2469f0`) compiled `#if STAGE90_XNU_CARD_TOTAL` in `entry_trace.c` to `0` — because `CARD_TOTAL`
was **not** added to `entry_trace.c`'s own compile line in `build_entry.sh` (it is compiled
separately from `STUB_DEFINES`, like `ROOT_FROM_CARD`, and the first draft added the define to
`STUB_DEFINES`' siblings but not here). So `__wrap_mdevlookup` **called nothing**: the raw unit
would never register, and the arm's record would promise a capacity the image never produced —
`mi4-off-option-two-spellings` with a shorter fuse, and exactly the shape the `xnu_entry_911d`
**linked-image clause** (added in the same step, mirroring 903/905) refuses. The clause fired, the
missing `-DSTAGE90_XNU_CARD_TOTAL` was added, and the corrected build (`605a43c2`) now shows the
call in the linked body. **The draft was never committed** and is not a recorded set.

**Built:** platform object exit 0 (`HFS_ROOT_MEDIA=1 EMMC_STRATEGY=1 ROOT_FROM_CARD=1 CARD_TOTAL=1`);
entry exit 0 (37 record keys, `STAGE90_XNU_CARD_TOTAL=1` at line 30); payload exit 0; entry bin
`605a43c2…`; the linked clause `xnu_entry_911d` confirms `__wrap_mdevlookup` calls
`entry_root_media_register_card_raw` and `entry_storage_card_sectors` is a `T` symbol; park verifies
11/11; `check_set_name_rule` 0; `make check` 0; `verify_press_ready` 5/5 (row 4 names the 911d
CARD-CAPACITY arm). NO press.

## 8. 911a BUILT (2026-10-07) — the RAM half, host-side, NO press

911a is **911d's entire switch set with ONE VALUE changed**: `STAGE90_XNU_ENTRY_WINDOW` raised from
`0x04000000` (64 MB, 912a/911d) to `0x40000000` (1 GiB). It is the **payload-only** half of the goal's
newest clause — the memory side, where 911d was the storage side.

**Why 1 GiB and not the device's 3 GB.** XNU arm32 clamps the value it is handed:
`mem_size = min(args->memSize, xmaxmem, MEM_SIZE_MAX)` (`external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c:354-358`)
with `MEM_SIZE_MAX = 0x40000000` (`:134`). A 3 GB request therefore **silently clamps to 1 GiB** — the
arm would read as if it took effect while the kernel used a third of it, `mi4-measurement-defects` on the
value itself. `0x40000000` is the largest number the EXISTING constant accepts without a clamp, so it is
the honest ceiling of this arm. Raising `MEM_SIZE_MAX` itself is a PORT and is 911b/911c, not 911a.

**The window reaches only the payload, through the generated header.** `build_entry.sh` substitutes the
window into `out/stage90/xnu_arm_entry.h` as `#define STAGE90_XNU_ENTRY_SIZE`, which the payload consumes
at `src/xnu_entry_jump.c:150` (`a->memSize = STAGE90_XNU_ENTRY_SIZE`) and `src/mmu.c`'s identity-map
loop. **No entry source reads it**, so `cmp` confirms the entry bin is **byte-identical to 911d's**
`605a43c2` (and the ELF to `52ab2d0d`). Because the entry bin did not move AND is not set-unique
(911d's park carries the same bytes), the set-name suffix comes from `stage90-qcdt.img` = `aff86051` —
the **`armed-window-*` family** (like 912a), NOT `armed-storage-*` (which the set-name rule requires to
be the entry image). The one linked byte that differs is `xnu_arm_entry-config.txt` (`a0610f96…`), which
carries `STAGE90_XNU_ENTRY_WINDOW=0x40000000`.

**What it buys.** The free region XNU is handed is `(physBase + memSize) − (topOfKernelData + 10 pages)`
(`xnu_entry_jump.c`, `ENTRY_DATA_LIMIT`). At 1 GiB: `0xc0000000 − 0x8080A000` ≈ **1015 MB**, versus
911d's ~55.9 MB and the 16 MB arms' 7.96 MB — a **~127×** widening over the pre-912 state. The window
raises `memSize`, **not** `topOfKernelData`, so the framed layout does not move and the press is
non-brick-by-construction (same argument as 912).

**Why the span is clean.** The device's high bank `[0x80000000, 0xde700000)` has **no hole** (every DT
hole is below `0x80000000`); the RAM console is at `0xde500000` and MMIO at `≥0xf9000000`. `0x80000000 +
0x40000000 = 0xc0000000` sits **480 MB below the console** and 992 MB below MMIO, so the section TTEs XNU
emits (`memSize / 1 MiB` of them, `start.s:198-207`) name RAM and never a device.

**The discriminator is ONE press, ONE value.** If the 909 residence wall is free memory, the death moves
or vanishes and the idle census beats 5 calls / 4 pairs; if it is independent, the death is unmoved and
R10's 5th-pass cache-window reading stands. **The refusal is read first, always**: `xnu_entry_args_memSize`
must read `0x40000000`, or the value never reached the live `xnu_entry_build_args` and the arm is a no-op.

**Built:** platform object exit 0 (911d's full define set); entry exit 0 (`cmp` says the entry bin is
IDENTICAL to 911d's `605a43c2` — payload-only confirmed); payload exit 0 (`xnu_arm_entry.h` carries
`STAGE90_XNU_ENTRY_SIZE 0x40000000`); park `out/stage90/frozen/armed-window-aff86051` (11/11,
`verify_revert_set --set=armed-window-aff86051` VERIFIED); `check_set_name_rule` 0 (every set's suffix
names a member; both `armed-window-*` sets take theirs from the qcdt); `make check` 0;
`verify_press_ready` 5/5 — **row 4 names the 911a RAM-RECOGNITION arm**, via a branch keyed on
`CARD_TOTAL=1 && ENTRY_WINDOW=0x40000000` placed **after** the 911d branch (911a carries 911d's whole
switch set, so a window-only or key-only selection would mis-name it). NO press.

**911a supersedes 911d and 912a for the next press:** it carries the raw card unit (911d) AND the widest
window, so one press answers memory-at-the-ceiling and the card capacity together. The goal is **NOT**
met — 1 GiB is not 3 GB; `MEM_SIZE_MAX` and the high-bank physmap window are the port that 911b/911c owe.

## 9. 911b BUILT (2026-10-07) — the PHYSICAL-MEMORY CEILING, host-side, NO press

**The rung, in one sentence.** 911a raised the *window* (the payload's `memSize`) to 1 GiB, but
`arm_vm_init.c` clamps that value: `mem_size = args->memSize; … if (mem_size > MEM_SIZE_MAX) mem_size =
MEM_SIZE_MAX;` with Apple's `MEM_SIZE_MAX = 0x40000000` (1 GiB). **911a's 1 GiB window was accepted
only because it EQUALED the clamp** — a 3 GB request would have been silently truncated. 911b makes
`MEM_SIZE_MAX` a **port** and raises **both** the clamp and the window to `0x5e500000`, the largest
value this device's address map permits.

**Why `0x5e500000` and not 3 GB.** Two hard ceilings, both measured:
- **The device's own hole.** The high bank is `[0x80000000, 0xde700000)` — no hole. The Android
  **ram_console** sits at `0xde500000` and **MMIO** at `≥0xf9000000`; mapping either as RAM corrupts
  a live structure or a device register. `physBase + memSize` must stay below the console *and* not
  overflow 32 bits (with `physBase = 0x80000000`, anything above `0x80000000` already wraps, so the
  real bound is the console).
- **The single-span linear physmap.** `phystokv(a) = a − gPhysBase + gVirtBase` is one linear window;
  it cannot express the Mi 4's **second, low bank** (which is *below* `physBase = 0x80000000`). So
  even a perfect ceiling recognises only the high bank. **The full 3 GB is 911c** — a real
  bank/region-list port, a separate rung.

`0x80000000 + 0x5e500000 = 0xde500000` — **one byte below the ram_console**, ~1.47 GiB. That is the
ceiling. `tools/check_mem_size_max.py` refuses any value above it, any `physBase + memSize` past 32
bits, and any value reaching `CONSOLE=0xde500000` / `MMIO_FLOOR=0xf9000000`.

**The clamp is why 911a alone was a NO-OP, and why BOTH must move.** The observable effect of raising
the ceiling is the pmap fold: `pmap_bootstrap((gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000)`
(`arm_vm_init.c:520`) and the page-table pre-init loop (`:532`) both fold to a **different constant**,
so the pmap's `virtual_space_start` moves off `0xc0000000` to **`0xde800000`**. With the window at
`0x5e500000` the free region XNU is handed is `(physBase + memSize) − (topOfKernelData + 10 pages)` =
`0xde500000 − 0x8080A000` ≈ **1.47 GB**, versus 911a's ~1015 MB, 911d's ~55.9 MB and the 16 MB arms'
7.96 MB.

**The byte-identity safety argument (and the one form that breaks it).** `MEM_SIZE_MAX` is an
*unconditional* define in Apple's source; the port guards it with `#ifdef STAGE90_XNU_MEM_SIZE_MAX`.
For an **undefined** macro the object must be **byte-for-byte Apple's**, so the ladder's bytes since
903 do not move. That property holds **only if BOTH marker arms live INSIDE the `#ifdef` and the
outer `#else` is Apple's bare `#define`**. A marker symbol in the outer `#else` enters the symbol
table, shifts `.text` by `0x10` (`0x1f90 → 0x1fa0`) and moves every following symbol
(`arm_vm_prot_init`, `arm_vm_init`) — the **measured defect**, object hash `424a45e1`. The nested form
is byte-identical to the pristine-source build (`c12dfbfd`), and `check_mem_size_max.py`'s `selftest()`
feeds it the `424a45e1` form and asserts it is **refused**.

**The tracked-patch idiom (mirrors HFS).** `external/xnu-4570.1.46` is a gitignored, re-provisionable
checkout, so the edit is made by a **tracked idempotent patch script** — `tools/patch_mem_size_max.py`
(shape of `tools/hfs_patch_root_rw.py`), applied by `tools/stage_hfs.sh` §4d, drift-checked by
`tools/check_mem_size_max.py` in `make check`. The stager's record `src/supply/hfs_tree_status.txt`
was re-derived (idempotently — the tree's two hashes and its full 66-line porcelain status are
**unchanged**; the record gained exactly one line, `osfmk/arm/arm_vm_init.c`, which the
`xnu_compile_graph` gate requires).

**The entry group MOVED — the ELEVENTH move.** The ceiling is compiled into
`osfmk_arm_arm_vm_init.o`, which is **in the entry link**, so unlike 911a this arm **moves
`xnu_arm_entry.bin`**. The exit's `bl FlushPoU_Dcache` moved by `+0x20`;
`STAGE90_XNU_SEAM_LR` and `EXIT_POP_LR_LITERAL` were re-derived from `0x8004f2dc` to **`0x8004f2fc`**
(both the entry build and `make check` refuse until both copies move in lockstep).

**Build refusals added.**
- `build_entry.sh`: `STAGE90_XNU_MEM_SIZE_MAX` is an arm key (recorded) **and** an `nm` clause reads
  `osfmk_arm_arm_vm_init.o` for exactly one of `entry_xnu_mem_size_max_arm_on/_arm_off` and refuses a
  mismatch between the build's switch and the object — in **both** directions.
- `scripts/preflight_boot_check.sh`: `STAGE90_XNU_MEM_SIZE_MAX` is a config key; a pre-911b record
  (key absent) reads as Apple's 1 GiB clamp, like `ENTRY_WINDOW`.
- `tools/check_mem_size_max.py` (in `make check`): structure, `src/stage90.h` lockstep, the ceiling
  bound, the patcher citation, and the `424a45e1` self-test.

**Built:** entry build exit 0 with the full switch set plus
`STAGE90_XNU_MEM_SIZE_MAX=0x5e500000 STAGE90_XNU_ENTRY_WINDOW=0x5e500000` (the `xnu_entry_911b:` line
printed, the seam clause reads `0x8004f2fc`). Entry bin
`185d106919192ebb1e2b2909df31e0a48716e37816cf693f66b0cedf98ebd786`, qcdt
`351c5cea828ef5811064a0fe86a482b11e7b74191331d1f9edac4763bbb63228`. Park
`out/stage90/frozen/armed-storage-185d1069` (11/11, `verify_revert_set` VERIFIED); the set name is
the **entry bin's** hash (the entry image moved — the `armed-storage-*` family rule);
`check_set_name_rule` 0; `make check` 0; `verify_press_ready` **5/5** — row 4 names **the 911b
PHYSICAL-MEMORY-CEILING arm**. NO press. The payload was then built with
`STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1 -DSTAGE90_XNU_MEM_SIZE_MAX=0x5e500000'` (exit 0), which
consumes the entry image and the generated `xnu_arm_entry.h`.

**The press reads ONE decisive cell.** `xnu_entry_args_memSize` must read **`0x5e500000`** and the
pmap's `virtual_space_start` must read **`0xde800000`** (where every arm through 911a reads
`0xc0000000`); `topOfKernelData` must **not** have moved. The refusal is first: if `memSize` still
reads `0x04000000` the window never reached `xnu_entry_build_args`; if it reads `0x40000000` the
*window* moved but the *ceiling* did not, and the vstart stays `0xc0000000`. The discriminator: the
909 residence wall (5 idle calls / 4 pairs, ~13.4 s) — **moved** at 1.47 GB says the wall is free
memory and 911c is worth the port; **unmoved** says R10's cache-window reading stands.

**911b supersedes 911a and 911d for the next press:** it carries 911d's raw card unit AND the raised
ceiling + window, so one press answers the ceiling, the residence wall, and the card capacity together.
The goal's **full 3 GB is still NOT met** — that is 911c.

## 10. 911c — the bank layout as a MEASUREMENT, not an inference (BUILT 2026-10-07, no press)

**The finding that reshaped 911c.** A repo-wide sweep (including `external/`, which plain `grep -rIn`
does not descend) found that **the repository contains NO base/size for the low bank at all.** The only
concrete bank it holds is the high bank `[0x80000000, 0xde700000)` (`src/stage90.h:29-30`,
`RAM_PHYS_BASE`/`RAM_TOP`); every "low bank" reference (§2, §5, `verify_press_ready.sh`) is an
*inference* from "3 GB total minus ~1.5 GB high", and the `80000000-de6fffff : System RAM` string is
**prose in `docs/history/stage0-payload-plan.md:53` and `stage1-boot-wrapper-plan.md:94`**, not a capture
(the `out/` capture it came from is gitignored, `records/baseline-readings.txt:24-27`). So 911c **cannot
be designed against an assumed low-bank layout** — its first act must be to *measure* it.

**Where the measurement is — and it is in-repo.** The authoritative bank source is the Qualcomm SMEM
**RAM-partition table**:
- Structure: `smem_ram_ptable { magic[2]; version; reserved1; len; parts[32]; buf; }` with
  `_SMEM_RAM_PTABLE_MAGIC_1 = 0x9DA5E0A8`, `_SMEM_RAM_PTABLE_MAGIC_2 = 0xAF9EC4E2`, each part
  `smem_ram_ptn { char name[16]; u32 start; u32 size; u32 attr; u32 category; u32 domain; u32 type;
  u32 r2..r5; }` — `external/android_kernel_xiaomi_cancro/arch/arm/mach-msm/memory_topology.c:28-58`.
- The kernel reads it via `smem_alloc(SMEM_USABLE_RAM_PARTITION_TABLE, ...)` (`memory_topology.c:131`),
  which resolves a TOC entry: `struct smem_shared` puts `heap_info` at `0xD0` and `heap_toc[512]` right
  after (`smem_private.h:22-49`), each `smem_heap_entry { u32 allocated; u32 offset; u32 size; u32 }`.
  **The ptable is at `SMEM + 0xD0 + id*16 + (toc[id].offset & 0xFFFFF)`** — but 911c does **not** need
  `id`: it can **scan every TOC slot and validate by magic**, which is more robust than computing a
  deep-enum index (`SMEM_USABLE_RAM_PARTITION_TABLE = SMEM_SMD_FIFO_BASE_ID + 64`,
  `msm_smem.h:126-128`).
- SMEM is the SoC shared RAM at PA **`0x0fa00000`, size `0x200000`** (`msm_iomap.h:92`,
  `msm_iomap-8974.h:26` `MSM8974_MSM_SHARED_RAM_PHYS`; DT `soc/qcom,smem@fa00000/reg = <0x0fa00000
  0x200000>`; `src/stage90.h:16-25`, `src/xnu_arm_vm_init_full_pmap.c:463-475`). **`0x0fa00000` is the
  project's `MSM_IMEM_BASE_PHYS` — the address the epilogue's `RESTART_REASON` store targets.**

**How the entry image reaches a LOW physical address (the empirical question, now answered).** The entry
runs on **XNU's** page tables with **TTBCR.N = 2** (`osfmk/arm/start.s:75-76`,
`TTBCR_N_1GB_TTB0`), so VA `< 0x40000000` walks TTBR0 and `>= 0x40000000` walks TTBR1; on this device
**TTBR0 == TTBR1** (live `xnu_live_ttbr0 = xnu_live_ttbr1 = 0x8080004a`), which is why the entry's
`entry_mmio_section` — whose `entry_live_ttb_base` returns TTBR1 for `n != 0` (`entry_stubs.c:2186`)
— has worked for every high MMIO it maps. `entry_mmio_section`/`entry_section_install` have **no VA
guard** (`index = va >> 20`, `entry_stubs.c:2113`; the only guard is on the *table base* `>= 0x80000000`,
`:2245`), so a low VA installs into the same live table. Two mappings are available:
- **the high alias `0xe0000000 → 0x0fa00000`** (index `0xe00`, free, above the `0xde500000` window) — the
  ROBUST choice, unambiguous under a TTBR0/TTBR1 split; reads SMEM as a Strongly-ordered section
  (`g_live_attr = 0xc`), which is what `entry_gic.c:393`/`entry_usb.c:149` already do; or
- **the low identity `0x0fa00000 → 0x0fa00000`** (index `0xfa`, free — XNU clears entries
  `[2048, 3557)` only) — which **additionally un-faults the epilogue's `RESTART_REASON` store**, closing
  the owed clean self-end (`mi4-906-return-is-a-watchdog-bite`).

**The `RESTART_REASON` fault is the same door.** The epilogue's store to `0x0fa0065c` faults today
(section fault `fsr 0x805`, `far = 0x0fa0065c`, `docs/experiments/experiment-745-*.md:76`) **because
nothing maps the `0x0fa` megabyte in the entry's context** — the payload maps it (`mmu.c:5458`) but the
payload's table is gone once XNU's `start.s` overwrites TTBR0/1/TTBCR. **So one mapping serves two
purposes**: the bank-layout read AND the run's ending.

**The rung, stated as a buildable plan (BUILT — see the correction and the built block below).**
1. A new `src/entry/entry_smem.c` whose `entry_smem_probe()` (a) calls
   `entry_mmio_section(0xe0000000u, 0x0fa00000u, ...)` and refuses if it returns 0 (`g_live_state != 1`
   or a table below the window), (b) walks the TOC slots at `SMEM+0xE0 + i*16` reading `{allocated,
   offset, size}` (`heap_info` is `SMEM+0xD0`), (c) validates a candidate ptable by
   `magic[0]==0x9DA5E0A8 && magic[1]==0xAF9EC4E2` **and** `len` in 1..32, (d) walks `parts[]` at
   **stride 56 B** and publishes each bank as `xnu_live_smem_bankN_start`/`_size` plus a count
   `xnu_live_smem_banks` and `xnu_live_smem_ptable_found`. The **sum of the bank sizes is the device's
   real total** — the number the goal's 「3GB内存」 names, measured rather than assumed.
2. Hook `entry_smem_probe()` into `__wrap_Idle_load_context` beside the USB probes
   (`entry_trace.c:2148-2160`) — the one site every idle pass reaches on every arm (`IDLE_NO_SLEEP`
   makes the exit wrapper unreachable; the 910a dead-code lesson, `mi4-a-lower-rungs-side-effect-
   poisoned-the-rung-above`) — and **make it a build refusal** when the switch is on and the call is not
   from that symbol.
3. A `STAGE90_XNU_SMEM_PROBE` arm key (like `STAGE90_XNU_USB_PROBE`), threaded through `build_entry.sh`
   (`ENTRY_ARM_KEYS`, the compile line, the link list, the record writer) and
   `scripts/preflight_boot_check.sh`; the witness (an `entry_smem.c`-defined symbol in the linked `.text`)
   lets the build tell a probe that ran from one compiled out (533's defect / `mi4-a-claim-in-a-comment-
   is-not-a-check`).
4. Optionally map the **low identity** too, so the same run closes the `RESTART_REASON` self-end.

**Two corrections the source forced on the design, found while building (2026-10-07).** The design
above carried two numbers that a reading of the owning structs refutes, and both would have made the
walk a reading of nothing:

- **The stride is 56, not 48.** `sizeof(struct smem_ram_ptn)` is `16 (name) + 6×4 (start, size, attr,
  category, domain, type) + 4×4 (reserved2..5) = 56`, `__packed`. A 48-byte stride walks into the middle
  of every second partition and reads garbage `start`/`size` pairs — [[mi4-stand-in-size-is-not-value]]
  applied to a stride. `entry_smem.h` spells the stride and every field offset from the struct's own
  field list, and `build_entry.sh` **refuses** a build whose `STAGE90_SMEM_PART_STRIDE` is not `56u` (a
  claim in a comment is not a check, [[mi4-a-claim-in-comment-is-not-a-check]]).
- **The bank filter is the device's own rule, not `size != 0`.** `memory_topology.c`'s
  `meminfo_init(type, min)` keeps a partition iff `parts[i].type == type && size >= min_bank_size`, and
  every board calls it `meminfo_init(SYS_MEMORY, SZ_256M)` (`board-8064.c:3702`). So a bank is
  `type == SYS_MEMORY (1) && size >= 256 MB`. `size != 0` would have swept in the modem/IMEM carve-outs
  and made the "total" larger than the RAM.

**What 911c is NOT.** It does **not** make XNU *own* the low bank: XNU's physmap is single-span
(`phystokv(a) = a − gPhysBase + gVirtBase`, `vm_param.h:196`; `vm_first_phys = gPhysBase`,
`pmap.c:2859`; the tables are sized by `atop(mem_size)`, `:2829-2834`), so 3 GB cannot be linearly
physmapped from `virtBase = 0x80000000` (PA `0xC0000000` → VA `0x140000000`, past `VM_MAX_KERNEL_ADDRESS
= 0xFFFEFFFF`). Recognising the full 3 GB in the kernel needs a **region-list / high-physmap port**;
**911c's job is to MEASURE the layout that port must be designed against** — and, per the goal's wording
(「正确识别」), to make the device's true banks *reported*. The port proper is 911e.

**Status: BUILT 2026-10-07 (no press).** Park `out/stage90/frozen/armed-storage-1d3ae364` (11/11,
`verify_revert_set` VERIFIED with `--set`); the set name is the **entry bin's** hash (the entry image
moved — `entry_smem.o` is in the link), the `armed-storage-*` family rule. Entry bin
`1d3ae3649c174668f0b0fe5177b767fb9a1f80b288c046316d08e9e7abcccdfe`, config
`9f2d3302ca51415c5df44c73ed16dfe50954634e96e3091946ea3d7ce218e87e` (now 35 arm keys, the new one
`STAGE90_XNU_SMEM_PROBE=1`), sources `96fea9f4…` (`entry_smem.c`/`entry_smem.h` add two files, 33 → 35).
`build_entry.sh` exit 0 with the full 911b switch set plus `STAGE90_XNU_SMEM_PROBE=1`; the linked image
calls `entry_smem_probe` **once** from `__wrap_Idle_load_context` (`bl 800157cc` at `0x804d3f24`) and the
new nm clause reads `N:1:1:1:1:1`; the three globals `g_stage90_smem_ptable_found`/`_total_bytes`/
`_banks` are in the image. Payload `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1
-DSTAGE90_XNU_MEM_SIZE_MAX=0x5e500000'` exit 0 (`stage90-build-config.txt` byte-identical to 911b's
`6c2b6038`). `check_set_name_rule` 0; `make check` 0; `verify_press_ready` **5/5** — row 4 names **the
911c SMEM RAM-BANK-MEASUREMENT arm**. **NO press.**

## 10a. THE FIRST PRESS OF THE 911 FAMILY (2026-10-08) — the raised window KILLS the payload's own MMU selftest; the arm never reaches XNU

Arm `armed-storage-1d3ae364` (911c) was **pressed non-persistently** (`fastboot boot`, nothing flashed) on
2026-10-08 00:13 UTC from a live Android/adbd state. **The device returned** (runner exit 0; adb lists
`4a2fe00b` as `device`). Capture: `out/stage90/captures/911c-press-20261008-001300-last_kmsg.txt` (211
lines). **No brick.**

**The run never entered XNU.** It died in the payload's OWN pre-jump MMU selftest:

```
MI4IOS6_STAGE90_XNU memSize=0x5e500000                                  (the 911 window IS applied)
MI4IOS6_STAGE90_XNU mmu_alias_base=0xc0000000
MI4IOS6_STAGE90_XNU mmu_alias_probe_phys=0x006e4648
MI4IOS6_STAGE90_XNU mmu_alias_read_after_identity_write=0x4d000000
MI4IOS6_STAGE90_XNU mmu high alias selftest failed: alias read mismatch
MI4IOS6_STAGE90_XNU kernel_entry bad: MMU high alias selftest
```
then the payload's clean reboot (`platform_reboot`: RESTART_REASON + PS_HOLD=0 + hw_watchdog immediate
bite). **`xnu_entry_status` is absent; no `xnu_live_*` record at all; XNU's `_start` was never reached.**

**THE CAUSE — a VA-window overlap 911's design missed.** The payload maps, in `src/mmu.c`'s
`build_identity_table()`: a high-alias window `[0xc0000000, +image) -> PA`, and (under
`STAGE90_XNU_ENTRY`) an entry-window **identity** loop `[0x80000000, +STAGE90_XNU_ENTRY_SIZE)`. When the
window is raised to `0x5e500000`, the identity loop covers `[0x80000000, 0xde500000)` — which **overlaps
the alias `[0xc0000000, 0xc4000000)`**. Both target L1 index `0xc06`; the identity loop runs LAST and
overwrites the alias descriptor, so the selftest's `0xc06e4648` read resolves to the identity PA
`0xc06e4648` (= DRAM content `0x4d000000`) instead of the expected `0x6e4648`. **The selftest was right;
the window was wrong.**

**This retires the §8 claim that "the span is clean".** §8 checked the PA *span* (no device / no
console) but not the **VA windows**: 911a's `0x40000000` (identity to `0xc0000000`, touching the alias)
and 911c's `0x5e500000` (deep overlap) both clobber it; only 912a's `0x04000000` (identity to
`0x84000000`) is clear — and 912a was never pressed either. **No 911-family arm was ever observed
reaching XNU before this press. The whole family is gated behind this fix.**

**The goal is NOT met and nothing in the 911 family should be pressed again until the fix lands.** The
residence wall (909) is untouched and still gates observability. The 909 event arms (`cabba670` etc.)
carry `RESIDENT=1` but NO window change, so they are NOT affected by this regression.

## 11. THE FIX — the identity map is bounded by the WINDOW, clamped below the alias base (2026-10-08)

**§9's cause is fixed, and the fix was pressed twice — once too small, once correct.**

The payload's entry-window identity loop (`src/mmu.c`'s `build_identity_table`) ran
`window_off < STAGE90_XNU_ENTRY_SIZE` (= `memSize`). That is the wrong quantity: `memSize` is what XNU
is **told**; the identity map is what the payload and `_start` **dereference** before XNU installs its
own tables. A raised window therefore walked the loop into the alias descriptors.

**Change.** A new macro `STAGE90_XNU_ENTRY_IDENTITY_LIMIT` in the generated `xnu_arm_entry.h`
(`build_entry.sh` substitutes it), consumed as the loop bound in `src/mmu.c`, plus two refusals: a
`#error` in `mmu.c` and a `layout_fail` in `build_entry.sh` that both fire if
`ENTRY_BASE + IDENTITY_LIMIT > STAGE90_HIGH_ALIAS_BASE`. **This is PAYLOAD-side only** — `src/mmu.c`
is not linked into the entry image, so `xnu_arm_entry.bin` stays `1d3ae364…` byte-identical while
`stage90.bin` / `stage90-qcdt.img` move.

**THE VALUE — and the first edit was the WRONG value, measured.** Bound to
`ENTRY_DATA_LIMIT + ENTRY_TABLE_BYTES` (9 MB), pressed as `armed-window-b97fcd2a`: the alias selftest
PASSED (`kernel_entry ok`, `memSize=0x5e500000`) but the run faulted at the boundary itself —
`data abort: dfar=0x80900000 dfsr=0x805 lr=0x800086c4`. **A too-small bound is its own fault**: XNU's
`_start` writes bootstrap page tables upward from `topOfKernelData`, and the payload's own globals live
past the image. **The correct bound is the WHOLE WINDOW, clamped below `0xc0000000`**:
`IDENTITY_LIMIT = min(ENTRY_SIZE, 0xc0000000 - ENTRY_BASE)`. At window `0x5e500000` that is
`0x40000000` — VA identity `[0x80000000, 0xc0000000)` — every byte `_start` touches, and not one byte of
the alias.

**PRESSED — `armed-window-aac38c50` (2026-10-08), and the fix holds:**
```
MI4IOS6_STAGE90_XNU mmu high alias selftest ok
MI4IOS6_STAGE90_XNU xnu_entry_args_memSize=0x5e500000
MI4IOS6_STAGE90_XNU xnu_entry_status=0x90000001
MI4IOS6_STAGE90_XNU xnu_entry_entering_at=0x80000074
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
```
The payload walks the entire pre-jump path — MMU selftest, PE discovery, the arm_vm_init full-pmap
window, the loader, and the copy to `0x80000000` — and jumps. **The 911 family is unblocked.** (The
`data abort: dfar=0xdeadc000` / `0xc0800000` lines are the payload's **deliberate** high-VA handler
probes at magic addresses, published as `..._data_abort_handler_dfar` and handled; they are not fatal.)

**OPEN, and it is the next question:** this window arm produces **no XNU console output after the jump**
(the log ends at the jump; no `BSD root`, no `md0`, no `xnu_live_door_seq`). The 16 MB arms reached the
OS; a `0x5e500000` `memSize` does not. So the raised window is now *reachable* but does not (yet) boot
XNU to the OS — the `memSize`/window value and the free-region question (912) are back on the table, and
the next arm isolates the window at a value that both clears the alias and boots. **The 3 GB reading
(§10) is NOT delivered by this arm** — `entry_smem_probe` publishes no `xnu_live_smem_*` here, which is
itself the reading that no idle pass ran.

## 12. THE RESIDENCE+SMEM PRESS, USB LADDER OFF (2026-10-08) — the run does NOT return; and the case against "the USB stream was the hang"

**Arm `armed-storage-21086959`** = 9edaa3b3's exact switch set at the 16 MB window, with the whole USB
ladder OFF (`USB_PROBE=0 USB_DEV=0 USB_DEV_FORCE=0 USB_ENUM=0 USB_STREAM=0`) and `SMEM_PROBE=1
RESIDENT=1 IDLE_NO_SLEEP=1`. The 911-family fix is in it, so the 16 MB window's identity map
(`[0x80000000, 0x81000000)`) is provably clear of the alias. **This is the first USB-off press of the
family, and the first smaller-entry-group build** — dropping the USB ladder removes
`entry_usb_enum.c`'s ON body (the `+0x1000` 910b added), so the linked exit seam moves **back** from
`0x8004f2fc` to **`0x8004e2fc`** (the twelfth seam move, and the first in the other direction;
`STAGE90_XNU_SEAM_LR` and the runner's `EXIT_POP_LR_LITERAL` both re-derived). Entry bin `21086959`,
payload `e9a24717`, parked 11 members, `verify_press_ready` 5/5, `check_set_name_rule` 0.

**THE OUTCOME — EXIT 2, the device did NOT come back.** Pressed non-persistently (`fastboot boot`, the
bytes `e9a24717…` sent = the bytes the gate read). The bounded 180 s wait expired with `adb: serial
4a2fe00b not listed`, `host log 58 -> 58 enumerations`, `port 3-10: 58 -> 58, any id` — **no re-entry in
any mode**. A further ~4 minutes of polling (adb and fastboot) stayed empty, and `lsusb` shows no
`cancro` device at all. **No brick** (nothing was flashed), but the device is dark until the operator's
power press. **The log is NOT lost by that press** — see the correction below; it is lost only by a
power *cycle*, and this arm's log is recoverable, so this arm's readings are provisionally the *absence
of a return*, not a captured log, and the recovery (owed) can upgrade them.

**⚠️ CORRECTION (2026-10-08): "lost its log" is wrong, and it was leaning on the wrong reading.** The
previous paragraph asserted the log is lost by the operator's power press. The runner's own exit-2 text
says the opposite (`run_and_capture.sh:3594`): the payload's log "lives in the top of DRAM and is lost
on a **cold boot**" — a power *cycle*, not a press — and 910's map names the recovery door explicitly
(`experiment-910-usb-debug-map.md:51`): the escape is the operator's **VolDown+Power → fastboot →
`fastboot boot <twrp>` → `cat /proc/last_kmsg`**, which reads the *previous* run's RAM console
(`ram_console`, base `0xde500000`). So the log from this arm is still in DRAM and is **recoverable**;
recovering it is **free** (a read) and costs no run, and it is the cheap step that would answer what the
exit-2 outcome cannot. Nothing in this arm's record should be read as "the log is gone".

**And that correction falsifies §12's headline.** The section title and the paragraph below say "the
wedge is the live explanation". **Without the recovered log that is a hypothesis, not a reading** — a
run that does not return tells us only that it did not return; it does not tell us it reached XNU's
idle loop at all, let alone that it wedged there. The one thing the exit-2 outcome rules out is that
`entry_usb_stream_poll` *alone* was the hang (the USB ladder is gone and it still did not return) —
everything past that, including whether the SMEM probe ran and whether the 3 GB bank list was produced,
is **unobserved until the log is recovered**. Worse, the "same wedge" identification is *doubtful on its
face*: this arm carries `IDLE_NO_SLEEP=1`, which **skips `platform_cache_idle_enter` entirely**, while
909's R14 localized the wedge *inside* that enter's cache-off window or its `wfi`. So if this arm wedged
in the idle path, it wedged in a **different** path than R14 named (the door-1 loop's shared tail), not
"the same" one — a distinction the exit-2 outcome alone cannot draw and the recovered log can.

**RECOVERY IS THE OWED FIRST STEP, BEFORE THE A/B ARM.** Recovering this log costs a read and no run;
spending `armed-storage-cb4e17f1` (a new press) before recovering this one would overwrite the DRAM
console and destroy the only evidence this arm produced. So the order is: (1) operator VolDown+Power →
fastboot → TWRP → `cat /proc/last_kmsg` for `21086959`; (2) read whether it reached the SMEM probe
(`xnu_live_smem_*`) and where it stopped; (3) *then* decide whether the no-sleep A/B arm is still the
right next press. **§15 answers (3) from the arms' own records: `cb4e17f1` carries the same
`IDLE_NO_SLEEP=0` / `IDLE_CACHE_ENABLE=0` config the 909 wedging arms carry, so it is predicted to wedge
the same way — and `armed-storage-cabba670` (909 arm 6, `IDLE_NO_SLEEP=1`) is the built, parked arm that
actually removes the wedging window.**

**WHAT THIS DOES AND DOES NOT SAY.** It **does not confirm** the pre-press hypothesis that
`entry_usb_stream_poll` was the hang. The USB-off arm does not return *either*, and it reaches further
into the wrapper (`__wrap_Idle_load_context` calls `entry_smem_probe` **after** `entry_usb_stream_poll`,
and `entry_wdt_pet` after that), so a hang at the stream poll would have left the SMEM probe and the pet
un-run. Removing the stream did **not** restore a return, which is the opposite of the hypothesis's
prediction — so the hang is **not the stream poll specifically**. **The residence wedge (909 R9/R10: the
run stops between the 4th `wfi` return and the 5th pass, ~13.4 s in, with no fault, no panic, no further
publishes) is the live explanation, and the whole USB ladder was not its cause.**

**A RECORD-KEEPING CAVEAT, stated because the earlier draft leaned on it.** The prior session's summary
described the 9edaa3b3 press as having *returned* with five `xnu_live_usb_stream_*` records and no SMEM /
residence keys. **That claim is NOT independently verifiable**: no 9edaa3b3 capture was saved
(`out/stage90/captures/` holds only `911c-press-20261008-001300`), and the parked log this run preserved
(`/tmp/cancro-last_kmsg.txt.prev.54`) is **neither** that arm — it carries `xnu_live_usb_*` probe keys but
**no** stream/enum/dev keys — **nor** this arm. **The conclusion does not rest on it**: it rests only on
this arm's own reading — the USB ladder is gone and the run still does not return. The 9edaa3b3 return/
stream-keys detail is treated as unproven and owed a re-press.

**HONEST UNKNOWNS.** (a) Whether the run reached the SMEM probe at all is **unobserved** until the log
is recovered (see the correction at the top of this section) — the 3 GB reading is still not delivered.
(b) *Corrected:* the earlier text said the exit-2 result "narrows it to the idle path itself". **It does
not** — an exit-2 outcome with no log says only that the run did not return, so this is open until the
recovery. What *is* established is the negative: the whole USB ladder was removed and the run still did
not return, so the USB stream poll is not the hang. (c) **The one structural difference from 909's own
arms** (which are recorded as reaching ~13.4 s and returning; that is the prior record, cited not
re-measured): they carry `RESIDENT=1` with `IDLE_NO_SLEEP=0` (the deep-idle path), while this arm carries
`RESIDENT=1` at the same window with `IDLE_NO_SLEEP=1` (the no-sleep path) — and only the latter does not
return. **So the next arm is the 16 MB window with `IDLE_NO_SLEEP=0`** (909's own idle path, which
returned), to separate "the wedge" from "the no-sleep path" — but **only after the recovery**.

**OWED:** (1) the operator's VolDown+Power → fastboot → TWRP → `cat /proc/last_kmsg` to recover
`21086959`'s log (free; must precede any new press); (2) reading that log for `xnu_live_smem_*` and the
stop point; (3) then the `IDLE_NO_SLEEP=0` A/B arm; and (4) the SMEM 3 GB reading.

## 13. The runner's exit-2 text contradicted itself on log recoverability (2026-10-08)

Reviewing §12's "log lost" claim against the tools found the *source* of the bad reading: the runner's
own exit-2 block (`scripts/run_and_capture.sh`, the `no-return` branch) said two opposite things in
four consecutive sentences. It tells the operator "Do NOT power-cycle before considering this: the
payload's log lives in the top of DRAM and is lost on a cold boot" — correct — and then, one sentence
later, "a failure to return means the log is likely **unrecoverable** anyway". The first sentence
invites a read; the second tells the operator not to bother. §12 was written from the second sentence,
which is why it declared the log lost one line after noting the WarmTWRP door.

**The wrong half is the "unrecoverable anyway" sentence.** It is true only in the *watchdog* case (a
payload-armed reset that reboots the SoC — though even then the buffer is cleared by the payload re-entry,
not by the reset). It is false for the general case: what destroys the RAM console is a **cold** power
transition (regulators off) or a **new `fastboot boot`** (the payload's `log_init` clears the buffer on
entry). A **warm** reset does not, and the project's own recovery door —
`VolDown+Power` → fastboot → `fastboot boot <twrp>` → `cat /proc/last_kmsg` — reads the *previous* run's
console (this is the same door `experiment-910-usb-debug-map.md:51` names as "the escape").

**The fix.** The exit-2 block now prints the recovery door explicitly, and names what actually destroys
the log ("a cold power transition or a NEW `fastboot boot` — not every reset"), so a dark device is not
mistaken for an empty one and a spent run's only evidence is not forfeited. This is the harness fixing
the misreading at its source rather than only correcting the doc that inherited it. `rehearse_live_path.sh`
cell `no-return` (which asserts the exit-2 block's own text) is the regression test. **Landed as
`50a2186`** (the §13 finding as `69c0f95`, the §12 correction as `5f2458a`).

## 14. The rehearsal's `no-fastboot-after-reboot` cell was failing on the harness's clock, not on the runner (2026-10-08)

Running `tools/rehearse_live_path.sh` after the §13 fix returned **19 ok, 1 failed**, the failure being
`FAIL no-fastboot-after-reboot  exit 124, promised 1 / did not say (either stream): device did not appear
in fastboot`. `124` is `timeout`'s own status, and the harness runs each live cell under `timeout 120`.

The row is a **measurement defect**, not a runner defect — the number it reported was an artifact of how it
was taken (`mi4-measurement-defects`). Measured this session:

- **The gate alone is ~59 s**, compute-bound (`user` ≈ `real`), spread across the linked-disassembly clauses
  (the biggest single wall-clock gaps are ~4 s; 33,799 traced commands, no one step dominant). It is
  re-run for *every* live cell and it grows each time a linked clause is added.
- The `no-fastboot-after-reboot` state (device in adb, `no_fastboot`) takes the `MODE == adb` branch, whose
  reboot-bootloader wait is a **hardcoded** `for _ in $(seq 1 30); do … sleep 2; done` = **60 s**, which no
  env override shortens (unlike `RETURN_TIMEOUT` / `FB_AMBIG_WAIT`, which this battery *does* shorten).
- So the cell costs ~59 + 60 + a few ≈ **122 s**, just past 120 → SIGTERM → `exit 124`.

**Run standalone with the cap lifted, the cell is correct**: `exit 1`, stderr `run_and_capture: device did
not appear in fastboot` — the exact text the cell promises. It dies at `run_and_capture.sh:3184`, far above
the §13 exit-2 block at `:3596`, so the §13 edit is not merely unreachable in this cell — the cell's own
promised behaviour reproduces with the edit present. The cap is raised to **`timeout 180`** with the
measurement recorded in place, and the cell now passes. **A `FAIL … exit 124` in this battery is this
budget until proven otherwise.**

**The full battery is green again**: `20 ok, 0 failed` (live path, was 19/1), `15 ok, 0 failed` (the
reader), `4 ok, 0 failed` (the path argument), `EXIT=0`. Landed as `112767c`.

## 15. The owed A/B arm is predicted to wedge like 909's — it carries the same idle config (2026-10-08)

Auditing the ladder before the press, from the arms' own records, found that the owed arm **`cb4e17f1`
carries the *same* deep-idle configuration the 909 residence arms 2–4 wedged with**, so correcting the
A/B comparison below is not a nit — it changes what the next press can conclude.

**The measured config pair.** `out/stage90/xnu_arm_entry-config.txt` (the live arm) reads
`STAGE90_XNU_RESIDENT=1`, `STAGE90_XNU_IDLE_NO_SLEEP=0`, **`STAGE90_XNU_IDLE_CACHE_ENABLE=0`**,
`STAGE90_XNU_SMEM_PROBE=1`. That is exactly the triple §12's paragraph was comparing against 909 — and
909's arms 2–4 (§ R12/R14) put the death **inside the 5th `platform_cache_idle_enter`'s cache-off window**
(`platform_cache_disable()` + `CleanPoU_Dcache()`) **or its `wfi`**, *because* `IDLE_CACHE_ENABLE=0` leaves
that window open. R14 names the window as `IDLE_CACHE_ENABLE=0` in so many words
(`experiment-909-residence-rung.md:577`).

**And `IDLE_CACHE_ENABLE=1` has never been pressed.** Grepping every arm record in `records/revert-set.txt`
finds only `IDLE_CACHE_ENABLE-0` — no arm in this project's history carries `=1`. So the lever 522 built to
**close** that window (the enter-side re-enable, `entry_trace.c:2385-2387`) has an armed code path, a
switch, and a doc, and has never been through a boot.

**What this corrects in §12.** §12 read the owed arm as "909's own deep-idle path … if it returns like 909,
`entry_smem_probe` runs on idle pass 1 and delivers the 3 GB bank reading." That comparison is **off by one
arm**: 909's *returning* runs (the 05:32/07:00 captures) ended normally — the 07:00 one shows
`idlestack_calls=0x5` / `pcx_entered=0x4`, i.e. it **wedged on the 5th pass**, it did not "return like 909"
in the sense the sentence meant. `cb4e17f1` shares 909's config, so it is **predicted to wedge the same way**
— which means a press of it now is a **second instrument on an already-observed wedge**, not the residence
fix §12 assumed.

**What the wedged arm still does.** `entry_smem_probe` is called **before** the pet and before the deep-idle
tail in `__wrap_Idle_load_context`, so it runs on **idle pass 1** and publishes `xnu_live_smem_*` there —
*before* the ~13 s wedge. So even a wedging `cb4e17f1` press delivers the 3 GB bank reading this session
cannot otherwise obtain. The press is still worth its cost; it is just mislabelled as a residence test.

**The genuine residence lever is already built and parked — `armed-storage-cabba670` (909 arm 6).** It is
`IDLE_NO_SLEEP=1`, which **removes the window** rather than repairing it: `cpu_signal_handler_internal(FALSE)`
is not called, `SIGPdisabled` stays set, `cpu_idle` leaves by door 1, and
`platform_cache_idle_enter`/`wfi`/`exit` are **never entered** (`experiment-909-residence-rung.md:611-616`).
If the wedge is the window, this arm is `RESIDENT=1` + pet that returns for as long as the pet holds the
watchdog off — the closest arm in the set to 「保持在xnu里」. It is owed a press and would be a stronger
residence test than `cb4e17f1`. **The naming is inverted**: `cb4e17f1` (`IDLE_NO_SLEEP 1→0`) is labelled the
residence A/B but tests the *sleeping* path; `cabba670` (arm 6, `IDLE_NO_SLEEP=1`) is labelled an instrument
but is the *residence* attempt.

**Honest scope.** The wedged arm's *cause* is still a hypothesis: R14's window-or-`wfi` is unproven (arm 5
was never pressed), and a code audit this session did not pin a single faulting instruction. What this
section asserts is a **config identity between the owed arm and the wedging arm**, read from the records —
which is enough to predict the outcome and to re-order the owed presses, and not enough to name the fault.
