# HD2 reference study — reading the iOS7 port for APPROACH, not for one macro (critic item M4)

**Status:** 📖 **READ-ONLY STUDY. No source, script, or device state was changed by this doc.**
**Date:** 2026-10-11.

**Why this doc exists.** The goal names 「参考hd2的ios7移植工程」 and the operator's standing
instruction is 「一定要参考https://github.com/HTC-Leo-Revival-Project/xnu」. Every prior use of the HD2
tree in this project (`docs/experiments/experiment-966-…`, `experiment-970-…`, `experiment-914-…`)
read it for **one mechanism at a time** (a COW file, an `arm_vm_init` line, one `#define`). It was
never read for **approach**. This study reads the three bring-up problems our goal most needs and
maps each to a Mi4 workstream, quoting the HD2 file and line **by value** so a follow-up can go
straight there.

**The single most important finding, stated first.** The local checkout
`external/xnu-hd2-darwin13/` is **not a pristine reference sitting beside our tree. It is a working
tree of the same `xnu`, on branch `codex/hd2-source`, that our own build harness edits in place and
compiles as the D13 tree.** `scripts/build_975.sh:19` sets `XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu`.
Its git status carries **uncommitted Mi4 edits** (`xnu/osfmk/arm/arm_vm_init.c` line ~353 has our
971 `bcopy` patch, with our commit numbers in its comment). This has two consequences, and both
matter for any future "consult the reference" step:

1. **To read the HD2 lab's actual approach you must read the baseline, not the working tree.** The
   pristine HD2 baseline is commit `8137aa2` (`Align HD2 touch observer source with 070 plan`) — the
   last commit before our `958: guarded /defaults hw.memsize read into max_mem` (`77fa165`) and our
   uncommitted patches. `git show 8137aa2:xnu/osfmk/arm/arm_vm_init.c` is the HD2 code; the file on
   disk is **ours**.
2. **The HD2 board code was never touched by us.** `git log -- xnu/pexpert/arm/pe_qsd8250_leo.c`
   returns exactly one commit (`8137aa2`, a touch-header alignment). So `pe_qsd8250_leo.c`, the
   `leo_*` files under `iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/`, and `leo_handoff.h` are
   **genuine HD2 lab code**, safe to read by value as the reference's approach. This doc reads those
   files directly and reads `arm_vm_init.c` / `locore.s` from the `8137aa2` baseline.

**What the HD2 target is.** Per `external/xnu-hd2-darwin13/README.md`, the target is
`QSD8250_LEO` (HTC HD2, Qualcomm QSD8250, ARMv7 Thumb) — a **different SoC** from the Mi 4's
msm8974. `grep` for `BOARD_CONFIG_MSM8974` in this checkout returns **nothing**; the msm8974
Platform Expert (`pexpert/arm/pe_msm8974.c`) lives only in the remote
`HTC-Leo-Revival-Project/xnu` upstream, per `mi4-reference-xnu-repo.md`. So this checkout gives us
the **HD2 iOS7 lab's approach to booting an iOS 7 userspace on a scavenged Qualcomm phone**, which
is the transferable thing, not msm8974 board values.

---

## Area 1 — USB debug / console path

### What HD2 does

HD2 has **no in-kernel USB at all**. A full-tree search
(`grep -rli 'chipidea\|dwc\|otg\|ehci\|ci13\|f9a55000\|udc'`) finds no QSD8250 USB controller
driver, and no `iokit/Drivers/**/*usb*` file exists. HD2's debug channel is a **three-stage
console hand-off plus an out-of-band SD-card log**, not a device-side USB console:

1. **A bounded RAM ring as the earliest sink.**
   `pexpert/arm/pe_qsd8250_leo.c:57-58`
   ```
   static volatile unsigned int leo_console_count;
   static volatile char leo_console[1024];
   ```
   `:774` — `void ios7leo_debug_putc(char c) { if(leo_console_count<sizeof(leo_console)) leo_console[leo_console_count++]=c; }`
   This is installed as the table's UART method and is **explicitly not claiming a UART**:
   `:775-776` — `static int leo_getc(void){ return -1; }` / `static void leo_uart_init(void){ /* no UART success claim */ }`.
   The dispatch install is `:813-822`:
   ```
   gPESocDispatch.uart_putc=ios7leo_debug_putc;
   gPESocDispatch.uart_getc=leo_getc;
   gPESocDispatch.uart_init=leo_uart_init;
   ```
2. **A real console that is only switched TO once the RAM sink exists**, guarded by a test that
   the current sink is still the ring. `:199-202` (inside the fatal path):
   ```
   if(gPESocDispatch.uart_putc==ios7leo_debug_putc)
     (void)switch_to_serial_console();
   ```
   and the fast-console arm at `:824-843`, which builds a `PE_Video`, calls `initialize_screen(...)`
   and then, only if the DT `/chosen/ios7leo-fast-console` property is present and 4 bytes `[1,0,0,0]`,
   calls `switch_to_serial_console()`. The generic dispatch itself is
   `pexpert/arm/common/pe_serial.c:41-57`, which `panic()`s if `gPESocDispatch.uart_putc` is NULL.
3. **The out-of-band, SURVIVES-A-HANG log is the SD card.** This is the HD2 answer to "get a
   debug channel out of the device", and it is the one that actually delivered:
   - `iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/leo_log_locator.c` (183 lines) — a bounded
     **FAT32 sector locator**: reads sector 0, validates `0x55AA` and the MBR partition table
     (`:20-40`), finds the single `0x0b`/`0x0c` FAT32 partition by type, validates the BPB
     (`le16(b+11)!=512`, `spc`, `nf 1|2`, `clusters>=65525`), then walks the FAT chain.
   - `IOS7LeoSDLogger.cpp` (567 lines) + `IOS7LeoSDCC2.cpp` + `leo_sdcc.c` (14632 bytes) carry the
     record framing. `docs/experiments/experiment-966-…` measured the on-card framing as
     `LEOLOG01`/`LEOREC01` in a 1 MiB ring (`BOOTLOG.BIN`), and it is **how the HD2 lab's results
     were ever read back** — `experiment-966` §1 reads the launchd/SpringBoard lines out of
     `BOOTLOG.BIN`, not out of RAM.
   - `leo_sdcc.h` shows the SD contract carries **`now_ns`, `delay_us`, and `poll_bound`** as
     caller-supplied callbacks (`struct leo_sd_io`), i.e. the vendor timeouts are a parameter, not a
     hardcoded spin — the same discipline this project states in `mi4-vendor-bounds` /
     `mi4-poll-count-lattice`.

HD2's **callsite-tagging is also part of the debug approach**, and it is reusable. Every checkpoint
carries the return address of its own `bl`:
`pexpert/arm/pe_qsd8250_leo.c:188` — `static unsigned int leo_callsite(void *returned){ return (((unsigned int)returned) & ~1U) - 4U; }`
used by `ios7leo_checkpoint` (`:753-772`) and by the fatal report (`:193-206`), which paints a
4-field fault tuple `PC/LR/FAR/FSR` with a `valid_mask` onto a known-good framebuffer region. This is
exactly this project's `mi4-stub-reports-its-call-site` (`caller - 4`) rule, arrived at independently.

### How our port diverges

- We have a device-side **USB ladder** (`src/entry/entry_usb.c`, `entry_usb_dev.c`,
  `entry_usb_enum.c`, `entry_usb_stream.c`, arms 959–962) with `tools/usb_console_read.py` on the
  host. **HD2 has no counterpart** — this is a place we are strictly ahead, not a gap.
- Our RAM console is `src/ram_console.c` at VA `0xde500000` (`src/stage90.h:13`); HD2's is
  `leo_console[1024]` in `.bss`. Same idea, different home — and ours is the one involved in the
  956/970 window-ceiling and 971 `bcopy` work.
- Our seam for late-run reachability is `__wrap_machine_idle` (`src/entry/entry_trace.c:2235`),
  which HD2 does **not** have: `osfmk/arm/machine_routines_asm.s:59-77` is a plain `wfi` with no
  hook. HD2's liveness proof is delivered from `arm_init.c` checkpoints + the GPT timer IRQ (Area 3).
- **We do not log to storage.** Our debug channel is RAM (dies with the hang — the repeated
  "log LOST" failures: 911, 975, 977) + USB (needs the ladder resident). HD2's is RAM + SD.

### Mi4 workstream informed

- The **`__wrap_machine_idle` seam** (951/977/979) stays ours; nothing to import.
- The **SD out-of-band log** is the missing half of the "USB 调试 / log survives a hang" clause:
  `leo_log_locator.c` + `IOS7LeoSDLogger.cpp` are portable plain-C/C++ prior art for a Mi4
  `src/entry/entry_storage.c`-based ring log. Target: a new storage-log rung beside the 959–962 USB
  ladder (the ladder is live-only; the SD log survives the reset). See ranked list, item **#2**.
- The **console hand-off discipline** (`pe_qsd8250_leo.c:199-202`) is directly adoptable into
  `src/platform/darwin13/pe_msm8974.c`'s `uart_putc` and `src/entry/entry_trace.c` — see ranked
  list, item **#3**.

---

## Area 2 — RAM / memory recognition

### What HD2 does

HD2 recognises its RAM through a **single 128-byte validated hand-off struct**, not through a
device-tree `/memory/reg` list, and it never reads `hw.memsize`.

- **The contract is a header.** `pexpert/pexpert/arm/leo_handoff.h` defines
  `struct ios7lab_leo_handoff` (128 bytes exactly; compile-time asserted at
  `pe_qsd8250_leo.c:27` — `typedef char leo_handoff_size[(sizeof(ios7lab_leo_handoff)==128)?1:-1];`),
  with `magic 0x4c454f31`, `version 1`, and fields `memory_base`, `memory_bytes`, `kernel_base`,
  `kernel_extent`, `top_of_kernel_data`, `loader_start/end`, `initrd_start/end`, `entry_atags`,
  `atags_end`. The header's own comment states the invariants: *"physBase/memSize are nonzero 1MiB
  multiples, memSize<=1GiB; the chosen suffix excludes still-needed loader/initrd/ATAG ranges and
  does not claim totalRAM."* The struct is passed via DT `/chosen/ios7lab-leo-handoff`.
- **Every field is revalidated at use, with a named refusal.** `pe_qsd8250_leo.c:382-398`
  (`leo_memory_context_header_ok`) checks the magic/version/bytes/flags, that `marsize`-style ranges
  are well-formed, and crucially the memory fields:
  ```
  if(!leo_memory_range_end(h->memory_base,h->memory_bytes,&end) ||
     (h->memory_base&0xfffffU) || (h->memory_bytes&0xfffffU) ||
     h->memory_bytes>0x40000000U)return 0;
  ```
  i.e. 1 MiB alignment and a **1 GiB ceiling** (matching our 956 finding that the D13 entry window
  is ceiling-bounded at 1 GiB — HD2 asserts the same bound for a different reason).
- **The bank list is validated, not trusted.** `:400-485` (`leo_memory_capture_functional`) reads a
  retained functional-context blob out of RAM via `ml_io_map` (`:321-330`, `leo_memory_map_copy`,
  bounded to `map_bytes<=8192U`), then proves: header magic/version, `c.plan.phys==h->memory_base`,
  `c.plan.mem==h->memory_bytes`, **no two banks overlap** (`:440-446`), and that the ATAG range, the
  loader, the initrd, and the plan are each **contained in a declared bank** (`:447-462`). The result
  is published as a `LeoMemoryObservation` with a `status` enum and a `known` bitmask
  (`:363-371`, `:472-486`).
- **`memSize` itself is only ever re-checked, never rewritten.** `:513-519` (`leo_video_ok`)
  asserts the boot_args conform (`Revision==1`, `Version==2`, `virtBase==0x80000000`, `memSize`
  non-zero 1 MiB-aligned, `<=0x40000000`, `ramend<=0x100000000`) before any of it is used.
- **HD2 declares a region list but never uses it.** `osfmk/arm/pmap.h:296-305`:
  ```
  typedef struct mem_region { vm_offset_t start; struct phys_entry *phys_table; unsigned int end; } mem_region_t;
  #define PMAP_MEM_REGION_MAX 26
  extern mem_region_t pmap_mem_regions[PMAP_MEM_REGION_MAX];
  extern int pmap_mem_regions_count;
  ```
  A whole-tree grep for a **writer** of `pmap_mem_regions` / `pmap_mem_regions_count` returns
  **only the `#define`**. There is no definition, no initialiser, no consumer — `pmap_bootstrap`
  (`pmap.c:2653`) works from the scalar `first_avail`/`avail_end` only. So the "region list" name in
  our 915-B port is *our* design; the HD2 tree carries the *declaration* of that shape but no
  implementation of it.

### How our port diverges

- **The whole 957/958/915-B line is our divergence, and HD2 has no counterpart.** 958 makes
  `max_mem` ride a DT `/defaults hw.memsize` read (`kern_mib.c`); 915-B makes the low bank
  navigable via a DT `/memory/reg` **region list** with a base-match accessor
  (`src/regions/xnu_memory_regions.c`, 978). HD2 does none of this — it validates one bank pair and
  stops. **HD2 is simpler, and simpler is not better here**: it never faced a multi-bank 3 GB phone,
  so it has no answer to the goal's memory clause.
- **Where HD2 IS better: the refusal is a named, structured contract.** Our region work has strong
  invariants (978's accessor refuses a low-bank-first list), but HD2's pattern is broader: a
  fixed-size hand-off where **every field is re-checked at the point of use** and the failure is a
  typed status (`LEO_MEMORY_STATUS_BAD_HANDOFF` / `MAP_FAILED` / `PARSE_FAILED` / `OK`), not a
  silent mis-read. That is the answer to this project's **"one value, two definitions"** and
  **"a claim in a comment is not a check"** defect classes.
- **HD2 keeps `memSize` as the boot bank and carries no total** — which is precisely the stance our
  964 correction settled on (`max_mem` alone carries the total; `gMemSize`/`mem_size`/`sane_size`
  stay the boot bank). HD2 reached that stance by construction; we reached it by correcting a
  defect. Same destination.

### Mi4 workstream informed

- The **915-B region port** (rungs 0–5; 976/978 host halves built) — HD2 supplies no algorithm, but
  it supplies the **validation shape**: a fixed-width hand-off with per-field refusal and a typed
  status. Target: `src/regions/xnu_memory_regions.c` and the hand-off validator it feeds. See
  ranked list, item **#1**.
- The **3 GB recognition clause** (958) is not informed by HD2 — HD2 never had the problem. Stated
  honestly: **this is the area where the reference is weakest.**
- The **`memSize<=0x40000000` ceiling** in `leo_video_ok` is an independent confirmation of 956
  (`build_entry.sh` refuses `>=0x40000000`).

---

## Area 3 — Root-mount / residency

### What HD2 does

HD2 **reached iOS 7 userspace** — the one thing our goal's "进入操作系统" clause still does not have.
`docs/experiments/experiment-966-…` §1 measured this from the HD2's own `BOOTLOG.BIN`:
`BSD root: disk1, major 14, minor 2`, `root_device -> / (hfs)`,
`Leo boot: init execve begin path=/sbin/launchd`,
`com.apple.launchd 1 … *** launchd[1] has started up. ***`, then `backboardd pid=11` and
`SpringBoard pid=12` `status=2`. **The same HFSX volume as ours** (`sha256 c9b9080d…`,
939,524,096 B). The approach:

- **The root is a FILE on the SD card, not a partition.** `IOS7LeoHFSFile` wraps the read-only SD
  file as an `IOBlockStorageDevice`.
- **A block-layer COW makes it writable.** `iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/leo_cow.c`
  (82 lines, plain C) is a **page-granular RAM shadow**:
  - `leo_cow_read_sector` (`:40-48`) returns from the RAM arena if the page is shadowed, else reads
    the base;
  - `leo_cow_prepare_write` (`:50-72`) proves capacity **before** any read, copies each original
    4096-byte page into the arena, then marks the page occupied;
  - `leo_cow_write_prepared_sector` (`:74-81`) writes only the shadow and sets `DIRTY`.
  The base is **never written**. `hfs_mountfs` sees a writable device and mounts rw
  (`Leo boot: root mount readonly=0`), and the log records the mechanism working
  (`Leo COW: cap=1405 pages used=2 dirty=2 writes=2 …`).
- **The pacing budget is derived from XNU's own free pages, not a constant.**
  `leo_cow_budget_pages` (`:13-25`) reserves `(managed>>2)+reserved+target` and spends at most
  `min(managed>>3, (free-guard)>>1)` on COW pages.

- **Residency comes from the timer IRQ, not from an idle hook.** HD2's `machine_idle`
  (`osfmk/arm/machine_routines_asm.s:59-77`) is a plain `wfi` with **no hook and no watch-dog pet**.
  HD2's liveness witness is the **QSD8250 GPT at 32768 Hz**:
  `pexpert/arm/leo_qsd8250_timer.c` implements `Leo_interrupt_init` (VIC, `:74-95`),
  `Leo_timebase_init` (free-running GPT, `:97-120+`), and at `:214` calls
  `ios7leo_timer_observe(last_irq_counter, irq_count)` on every timer IRQ. `ios7leo_timer_observe`
  (`pe_qsd8250_leo.c:290-305`) refuses to refresh unless `leo_continuation_active`, which is set by
  `ios7leo_kernel_continuing()` (`:281-287`) — called once at the end of `arm_init`
  (`osfmk/arm/arm_init.c:180-182`). So **HD2's "stays resident" is a real periodic timer IRQ that
  keeps firing and repainting a liveness indicator**, plus `arm_init.c` checkpoints 6/7/8/10
  (`:86,133,165,180`) that bracket `arm_vm_init`, `kernel_early_bootstrap`, `PE_init_platform`.
- **The VFP guard.** `arm_init.c:237-252` panics unless `CPACR` has FP/D32 enabled and `FPSID`
  reports VFPv3 with a 32-register bank — a *"prove the feature before the context routines use it"*
  check, in the same family as our fault-path checks.

### How our port diverges

- **We have the COW.** `src/platform/stage90_root_media_cow.c` is our port of `leo_cow.c`, with the
  page geometry parameterised and no allocator (arena handed in). The differences are named in that
  file's header. So this half is already transferred — see `mi4-968-cow-writable-root-arm`.
- **We do NOT have userspace.** Our wall is `mi4-969`: launchd links a **301 MiB** dyld shared
  cache against our bounded entry window; HD2's log shows launchd *started*, so HD2 crossed a wall
  we have not. The HD2 tree does **not** contain an obvious "301 MiB shared cache" handler I could
  locate (see NOT FOUND, below) — the difference is likely that HD2 is a QSD8250 with a different
  memory geometry and its root is served as a file, not that it solved the specific mapping problem.
- **Our residency seam is different in kind.** Ours is `__wrap_machine_idle` (951/977/979) — a
  wrapper on the *idle* path. HD2's is (a) `arm_init.c` checkpoints and (b) the **live timer IRQ**.
  **This is the divergence that explains a real Mi4 hazard:** 977's theory was that a resident image
  with no idle-path pet arms the SoC watchdog (~28 s). HD2's `machine_idle` has **no pet either** —
  so a resident HD2 image would face the same class, except HD2 keeps the GPT timer IRQ **live and
  periodic**, which is itself the periodic wakeup. Our 979 press falsified 977's specific pet theory
  for our case, but the HD2 reading says the durable answer to "stay resident" is **a live periodic
  timer interrupt**, not a manual pet inside `machine_idle`. If our D13 image's timer IRQ path is
  not delivering periodically, no amount of idle-hook petting fixes residency.

### Mi4 workstream informed

- The **root-mount / COW** workstream (966/968) — already adopted; HD2 is the source, our port is
  the adaptation.
- The **residency clause** (`保持在xnu里`, 977/979) — HD2 informs it by *counter-example*: its
  liveness is the timer IRQ (`leo_qsd8250_timer.c:214`), so our next residency check should
  confirm the D13 timer IRQ is live and periodic (our `src/timebase.c` / `entry_timebase.c`), rather
  than only instrumenting `machine_idle`. See ranked list, item **#3 (residency variant)**.
- The **dyld shared-cache wall** (969) — **HD2 does not inform this.** Stated honestly.

---

## Ranked: the top 3 HD2 approaches the Mi4 port should ADOPT next

1. **Adopt HD2's validated fixed-width hand-off contract for the region list.**
   *Reason:* the goal's memory clause (3 GB) is gated on a `/memory/reg` list that our own data can
   violate (a low-bank-first list reads as the boot region). 978 already refuses this in
   `src/regions/xnu_memory_regions.c`, but as an ad-hoc assert. HD2 walks the whole struct field by
   field and returns a **typed refusal** (`leo_handoff.h` + `leo_memory_context_header_ok`,
   `pe_qsd8250_leo.c:382-398`). Wrapping our region walk in that shape converts the project's
   recurring "one value, two definitions" / "a claim in a comment is not a check" failures into a
   refused boot.
   *Target:* `src/regions/xnu_memory_regions.c`, `src/pe_state.c`, `src/pexpert.c`.

2. **Adopt the SD-card out-of-band log (`leo_log_locator.c` + `IOS7LeoSDLogger.cpp`).**
   *Reason:* our debug channel dies with the hang, and that has cost us repeatedly (911, 975
   "log LOST", 977). HD2's SD ring is how its launchd/SpringBoard result was ever read back. We
   already have both halves it needs — `src/entry/entry_storage.c` for the block read and a COW/ring
   arena pattern — so this is an assembly job, not a new driver. It directly serves the 「可以通过usb
   进行调试」 clause by making the log **persistent** across the reset the USB ladder cannot survive.
   *Target:* a new storage-log rung in `src/entry/`, borrowing `leo_log_locator.c`'s bounded FAT32
   locator (it validates the MBR/BPB exactly the way our storage paths validate CSD/GPT).

3. **Adopt HD2's two residency disciplines: (a) the console hand-off guard, (b) timer-IRQ-as-pet.**
   *Reason (a):* `pe_qsd8250_leo.c:199-202` switches to the real console only when the sink is still
   the RAM ring, so the first fatal report is never overwritten. That is a *"silence is a reading
   only if success is silent"* guard we should mirror in `pe_msm8974.c` / `entry_trace.c`.
   *Reason (b):* HD2's `machine_idle` has no pet and HD2 still stays up — because its GPT timer IRQ
   is live (`leo_qsd8250_timer.c:214`). Our 977/979 investigation has been entirely inside the idle
   wrapper; HD2 says the durable residency signal is a **periodic timer IRQ**. The next residency
   check should witness `src/timebase.c` firing on D13.
   *Target:* `src/platform/darwin13/pe_msm8974.c` (console guard), `src/timebase.c` +
   `src/entry/entry_timebase.c` (timer-IRQ liveness), `src/entry/entry_trace.c` (the check).

---

## What I could NOT find in the HD2 tree (honest gaps)

- **Any device-side USB channel.** No QSD8250 USB controller driver, no
  `iokit/Drivers/**/*usb*`. HD2's "USB debug" question is answered with **storage**, not USB — so
  our 959–962 USB ladder has **no HD2 prior art**; it stands on its own.
- **Any msm8974 board code.** `BOARD_CONFIG_MSM8974` does not appear in this checkout. The msm8974
  Platform Expert named in `mi4-reference-xnu-repo.md` is in the **remote**
  `HTC-Leo-Revival-Project/xnu`, not here.
- **Any writer of `pmap_mem_regions[]`.** `pmap.h:296-305` declares the region list; a whole-tree
  grep finds no definition, initialiser, or consumer. The region-list *shape* is declared but the
  *implementation* is absent — so 915-B is genuinely our design, not a port of a working HD2 path.
- **Any `hw.memsize` read or `/defaults` handling.** HD2 carries no total-RAM reporting (958 is
  wholly our divergence).
- **Any explicit dyld shared-cache / commpage / userspace-VM mapping tied to the launchd exec.** The
  log proves launchd started, but I could not locate the source that maps the 301 MiB shared cache
  (the wall of `mi4-969`). This is the single most valuable thing that is *not* in the tree, and the
  reason Area 3 informs residency but **not** the shared-cache question.
- **The `pbxproj`/build wiring that compiles `pe_qsd8250_leo.c`.** `machine_configuration/` is absent;
  the target config that selects `BOARD_CONFIG_QSD8250_LEO` is not present in this source-only export
  (the README and `SOURCE-AUDIT.md` both state the export is source-only and not a standalone build).

---

## Method / reproduction

All line numbers are from the files on disk under
`/mnt/data/mi4-ios6/external/xnu-hd2-darwin13/xnu/` **except** `osfmk/arm/arm_vm_init.c` and
`osfmk/arm/locore.s`, whose on-disk copies carry our uncommitted Mi4 patches — those were read from
the baseline `git show 8137aa2:<path>`. The board files (`pe_qsd8250_leo.c`, `leo_*.c/.h`,
`IOS7Leo*.cpp`, `leo_handoff.h`) were confirmed untouched by us
(`git log -- xnu/pexpert/arm/pe_qsd8250_leo.c` → one commit, `8137aa2`, a touch-header alignment).
No device was contacted; no source, script, or git state was modified by this study.