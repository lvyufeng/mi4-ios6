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
