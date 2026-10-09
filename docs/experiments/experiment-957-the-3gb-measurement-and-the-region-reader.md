# 957 — The 3 GB measurement, and the region reader it sizes (2026-10-09)

915 named its own next step: *measure the low-bank footprint, then write the region-list reader*. This
rung takes the measurement (read-only `adb` against the running device — no press, inside the gate) and
resolves the two things 915 could only guess: **what channel carries the banks**, and **what D13's pmap
can actually accept**.

## 1. The measurement (live, `adb -s 4a2fe00b`, Android 10 / kernel 3.4)

Authoritative `/memory`, two independent readers agreeing:

| source | value |
|---|---|
| `/proc/device-tree/memory/reg` | `(0x00000000, 0x60000000)` + `(0x80000000, 0x60000000)` = **`0xC0000000`** |
| `/proc/device-tree/memory/#address-cells` / `#size-cells` | `1` / `1` (one cell base, one cell size) |
| `/proc/meminfo` `MemTotal` | 2935868 kB ≈ 2.80 GiB (3.00 GiB minus reservations/kernel) |

So **3.000 GiB total**, and 915 §1's simple picture is right for the `/memory/reg` — but the *reserved*
substructure is real and was not in 915:

- Android's `/proc/iomem` shows the **low bank is not one run**: `[0x00000000,0x06000000)` +
  `[0x0d200000,0x0fa00000)` + `[0x0ff00000,0x60000000)`, with `[0x06000000,0x0d200000)` and
  `[0x0fa00000,0x0ff00000)` carved out (the second is the SMEM window 911c already maps).
- `/memory` carries three CMA children (device DT, `linux,contiguous-region`): `adsp_region`
  `{base 0, size 0x04100000}` (65 MB), `secure_region` `{0, 0x0fc00000}` (252 MB), `qsecom_region`
  `{0, 0x00110000}` (1.0625 MB) — **~318 MB of low-bank carveout**, which is most of the 3.00 → 2.80
  GiB gap.
- The high bank `/proc/iomem` run is `[0x80000000, 0xde700000)` — Android stops below `0xde700000`
  (the ram_console/「top」 the project already knows, `0xde500000`-ish; the goal's "3 GB" is the
  **DT-declared** `0xC0000000`, not what any particular OS reserves).

A second identity corroborates the goal's own memory note: `/proc/device-tree/memory/reg` is exactly
the 911c SMEM bank sum. Two independent channels, one number (`mi4-one-value-two-definitions` = *don't
let them drift*).

## 2. The channel — CORRECTED: the tree is OURS, and it carries ONE bank

**First draft of this section was wrong; this is the correction, kept visible.** The first reading took
the running device's `/proc/device-tree/memory/reg` (two banks) as "the tree XNU gets". It is not:
that is **Android's** tree, written by aboot for Android's kernel. XNU does not get it.

XNU gets a **synthetic tree the payload builds itself**: `build_stage90_apple_dt`
(`src/stage90_main.c:47-138`) emits `g_apple_dt` in Apple's `DeviceTreeNode` format, `build_boot_args`
(`src/boot_args.c:5`) puts it in `boot_args->deviceTreeP`, and `xnu_entry_copy_device_tree`
(`xnu_entry_jump.c:105-120`) copies exactly that (`args->deviceTreeP = ENTRY_DT_PA`, `:161`). Its
`/memory` node's `reg` is a **one-bank** constant:

```c
static const uint32_t memory_reg[] = { RAM_PHYS_BASE, RAM_CONSOLE_BASE - RAM_PHYS_BASE };
/* = { 0x80000000, 0x5e500000 } - the high bank only, up to the ram_console */
```

So the channel the port was going to add **is the payload's own builder**, and it must be made to emit
both banks. The DT *format* is settled by the payload's own reader: `DTGetProperty` returns bytes with
**no swap** (`device_tree.c:410-425`), and `apple_dt_prop_u32_array` writes **host (LE) order** — so a
reg the payload writes is read back by D13's `DTGetProperty` verbatim, no endianness work.

Two definitions of the same quantity now exist and must not drift (the class this project keeps hitting,
`mi4-one-value-two-definitions`): the payload's **`PE_state_stage90.memorySize`** — parsed back from this
very reg by `src/pe_state.c:32-39` (`apple_dt_find_child(dt,…,"memory")` → `apple_dt_get_prop(…,"reg")`,
word 1) and asserted against `RAM_CONSOLE_BASE - RAM_PHYS_BASE` in five places (`mmu.c:2335`,
`xnu_early_pmap_platform_init.c:287-288`, `xnu_pe_init_platform_false.c:351-352`, …) — and the D13
kernel's `max_mem`. A two-bank reg changes the payload's word-1 reading and breaks every one of those
asserts, so the port updates the payload's own checks in lockstep.

**The flashed `dt.img` is irrelevant to this path** (it is QCDT for Android's boot; the payload's
`stage90-qcdt.img` merely carries it for `fastboot boot` framing). Its having no `/memory` node and the
running device's having two are both about Android, not XNU.

## 3. What D13's pmap can accept, and the split the port must make

Measured in the tree this session:

- **The pmap is one interval.** `avail_end = gPhysBase + gMemSize` (`arm_vm_init.c:402`),
  `avail_start = first_avail`, `vm_first_phys = first_avail` (`pmap.c:2668-2670`); `pmap_next_page` is a
  monotonic bump allocator (`pmap.c:2017-2031`); `pmap_valid_page`/`pmap_zero_page` gate on
  `[avail_start, avail_end)` (`pmap.c:1955,2152-2156`). No region list.
- **`phys_to_virt` is a single linear offset** (`pmap.h:192-193`) — any PA below `gPhysBase`
  underflows. So bank 0 (`< 0x80000000`) cannot be linearly mapped from bank 1.
- **The kernel VA (< 2 GiB) is smaller than the RAM (3 GiB)** — 915 §3: a full linear map is
  impossible on ARM32, full stop.

So the port keeps **`gMemSize` = the boot bank** (the high bank, where the kernel runs) for the linear
map and the allocator, and moves only the **reported** total. That is option (B), and it is exactly
what makes the goal's clause 「能够正确识别xiaomi 4的3GB内存」 true: the kernel **recognizes** 3 GB.

- `max_mem` / `sane_size` are the **reported** total → `host_basic_info.max_mem` / `hw.memsize`
  (`host.c:182`, `model_dep.c:149`, `startup.c:198`).
- `gMemSize` / `avail_end` are the **linear bank** → the pmap and the allocator.

915 §2.4 said the region machinery was "x86 boilerplate, never defined". That is true of **`osfmk/arm`**,
but D13's **i386** side *does* define the loop the port needs — `i386_vm_init.c:368-401` accumulates
`sane_size += region_bytes` across regions, and `startup.c:555` already has a
`if ((uint64_t)sane_size >= 3 * GiB)` branch. **The template is in the same tree**, one architecture
over; the port is to give the ARM path the accumulation the i386 path already has, keeping the ARM
allocator single-interval.

## 4. The shape of the reader (design, staged)

Staged like every D13 edit (`stage_d13_board.sh` / `stage_hfs.sh` + a `patch_*.py` + a
`check_*_staged.sh`), never a bare edit to the nested tree:

1. **`osfmk/arm/arm_vm_init.c`** — after `gMemSize = args->memSize`, read `/memory/reg` with the DT API
   and sum the `(base,size)` pairs into a **new `max_mem`/`sane_size`**, while `gMemSize` stays the
   boot bank. The DT walk must run where the DT is mapped (the entry window covers `ENTRY_DT_PA`), or
   defer to a point after `PE_init` — the anchor is the one open question (see §5).
2. **`osfmk/arm/pmap.c`** — unchanged for the allocator (bank 1 only); optionally teach `pmap_valid_page`
   the second bank if any caller extracts a bank-0 PA. Bounded by measurement (§5).
3. **The refusal** — bind the reported total to the **measured `0xC0000000`**: a build/check refusal
   that the sum the reader computes from the committed reg equals `0xC0000000`
   (`mi4-a-claim-in-a-comment-is-not-a-check`), so a drifted reg or a mis-summed loop cannot ship as
   "3 GB". This is the single-source-of-truth rule of 915 §4.

## 5. The anchor, resolved (the exact one line)

`arm_init.c` settles the boot order: `PE_init_platform(FALSE, args)` at `:146` runs `DTInit(...)`
(`pe_init.c:134`) and `pe_identify_machine`, **before** `arm_vm_init(maxMem, args)` at `:168`. So the
DT is already initialized and addressable when `arm_vm_init` runs, and the read at `arm_vm_init.c:322`
(just after `gMemSize = args->memSize`) is through the entry window's own mapping — the same window
`arm_vm_init` dereferences `args` through at `:321`. No fault risk, no deferral.

And the split is already structural: `gMemSize` is a **local** (`arm_vm_init.c:307`,
`uint32_t gMemSize;`) feeding the linear map / `avail_end` / `pmap_bootstrap`, while
`max_mem = mem_size = sane_size = gMemSize` at `:326` sets the **globals** that become
`machine_info.max_mem` (`startup.c:198` → `host.c:182` / `model_dep.c:149` = `hw.memsize`). So the
whole change is:

```c
gMemSize = args->memSize;                                   /* the linear bank - UNTOUCHED */
...
max_mem = mem_size = sane_size = stage90_memory_total(args); /* the reported total - the DT sum */
```

one line swapped, where `stage90_memory_total` walks `/memory/reg` and **falls back to `gMemSize`** if
the node or the property is absent (so 4570, and a DT without the reg, are unaffected). No pmap change
is required for the reported total; only if a *driver* must touch a bank-0 PA (the waived §6 window).

## 6. What this rung does NOT decide

- **Whether bank 0 needs allocatable pages at all.** B says no: the goal's verb is *recognize*. A is
  named and deferred (915 §3).
- **The bounce window** (915 §6): needed only if a driver must *touch* a low-bank PA. Not measured yet,
  not in this rung.

**Next concrete step:** stage the reader into the D13 kernel (a `patch_d13_memory_total.py` + a
`check_*_staged.sh`, the `stage_d13_board.sh` pattern), build it press-free
(`tools/build_xnu_arm_kernel.sh XNU_TREE=…/xnu-hd2-darwin13/xnu`), then a D13 press whose log shows
`max_mem = 0xC0000000` with the linear map still on one bank.

*Provenance: `external/xnu-hd2-darwin13/xnu` `arm_vm_init.c`/`pmap.c`/`pmap.h`/`pexpert/…/boot.h`/`i386_vm_init.c`
read this session; the live map read read-only over `adb` (`4a2fe00b`, no press, inside the gate). The
device was not modified. Rung follows [[mi4-915-multibank-region-list-design]] and
[[mi4-956-d13-entry-window-ceiling]].*