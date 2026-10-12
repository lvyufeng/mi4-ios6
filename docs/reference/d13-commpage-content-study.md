# D13 commpage content study — what our kernel writes, what iOS 7 actually reads, what reads zero

Method: read-only recon. Tree `external/xnu-hd2-darwin13/xnu` (OUR build tree, read by value);
artifact `/mnt/data/ios7-payload/v2/ios7/rootfs.hfs` (read-only; dyld extracted to
`/tmp/cp992-data/rootfs/IOS7MIN/usr/lib/dyld`, cache slices to `/tmp/cp992-data/`). No build, no
device, no git; one file written (this one). Every claim below is a machine reading: `python3 -I`
(`struct.unpack`, numpy byte scans), `grep -n`/`grep -c`, `sha256sum`, `arm-none-eabi-objdump -D -b
binary -m arm [-M force-thumb]`. sha256 first 16: `commpage.c` `67aa79528b8edebc`, `cpu_capabilities.h`
`9fd54db9d5ef2693`, `pmap.c` `cdc3903ba94e8dc3`, `traps_lo.s` `b16e7a66d538cbcd`, dyld
`408061f0ec0a2d03`, libvDSP slice `7829cdda2d683c7e`.

Census tool `/tmp/cp992-tools/refined_all.py` (baseline `/tmp/cp992-tools/refined_refs.py`): scans all
**697** cache images (696 + dyld), resolving every instruction-materialised constant in
`[0xffff4000,0xffff4fff]` and checking for a dereference within 44 bytes. Idioms: ARM `ldr/ldrb`
literal both U bits, ARM `ldrd` literal, Thumb `ldr[pc]` T1, Thumb T2 literal
(`F8DF/F89F/F8BF/F99F/F9BF`), Thumb T3 signed literal (`F85F/F81F/F83F/F91F/F93F`, P=1 W=0),
ARM+Thumb `movw`/`movt` pairs. Output `/tmp/cp992-data/refined_all.tsv` — 503 rows, 150 inside the
ABI field area.

**Two earlier scope bugs, corrected here (they invalidate my own first census, not the team's docs).**
The first pass scanned only `/usr/lib/**` (120 of 697 images) and its Thumb `movt` filter matched only
`i=0` (`0xF2CF`), so it missed libdispatch, libvDSP (`/System/Library/Frameworks/...`),
libsystem_platform, and every `movt #0xffff` (`i=1` → `0xF6Cx`); it also lacked ARM *negative*
pc-relative literal loads. Its conclusion "only dyld reads the commpage" was wrong. All reader claims
below come from the corrected 697-image run.

## 1. The layout: two names, one set of offsets

Userspace base (the ABI iOS 7 uses, from the artifact's own instructions): **0xffff4000**. Our tree's
kernel-side base: `_COMM_PAGE_BASE_ADDRESS = _COMM_PAGE32_BASE_ADDRESS = 0x40000000`
(`osfmk/arm/cpu_capabilities.h:39`); 992 aliases the same physical page at 0xffff4000 (pmap.c, gate
census below). Names/offsets are taken from the 4570 header (`external/xnu-4570.1.46/osfmk/arm/
cpu_capabilities.h:172-192`), which documents the iOS 7 arm layout: `_COMM_PAGE_VERSION` +0x01E
("16-bit version#", `_COMM_PAGE_THIS_VERSION` = 3 at :173), `CPU_CAPABILITIES` +0x020, `NCPUS` +0x022,
`CACHE_LINESIZE` +0x026, `ACTIVE_CPUS`/`PHYSICAL_CPUS`/`LOGICAL_CPUS` +0x034/+0x035/+0x036
(hw.activecpu / hw.physicalcpu_max / hw.logicalcpu_max), `MEMORY_SIZE` +0x038 (uint64),
`TIMEBASE_OFFSET` +0x088 (uint64 "for constructing mach_absolute_time()"), `USER_TIMEBASE` +0x090
(uint8 "is userspace mach_absolute_time supported (can read the timebase)").

**The `_COMM_PAGE_VERSION` question is answered:** the D13 tree's `cpu_capabilities.h:52` calls the
same +0x1E slot `_COMM_PAGE_MYSTERY_VALUE` ("This is a uint16_t with value of 3.") and
`commpage.c:92` writes 3 — same offset, same value as 4570's `_COMM_PAGE_VERSION` /
`_COMM_PAGE_THIS_VERSION` (3). Likewise **+0x34..+0x36 are ACTIVE/PHYSICAL/LOGICAL_CPUS**, three u8
CPU counts (not merely "count bytes"): 4570 :183-185 names each; and §3(e) shows libdispatch reading
exactly +0x036.

## 2. Writer census of our tree (`commpage.c`, by value)

Gates: `commpage.c:57` and `:99` are
`#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO || (defined(STAGE90_XNU_COMMPAGE_ALIAS) && STAGE90_XNU_COMMPAGE_ALIAS)`.
Mi4 defines only `-DBOARD_CONFIG_MSM8974_CANCRO`; 992 defines `STAGE90_XNU_COMMPAGE_ALIAS` ⇒ **both
gated blocks compile in** (6 gate lines: `grep -c STAGE90_XNU_COMMPAGE_ALIAS` = 4× pmap.c + 2×
commpage.c). Callers: `commpage_populate` ← `vm_shared_region.c` (`#if defined(__arm__)`);
`commpage_update_active_cpus` ← `commpage_populate:98`. The file's own header comment (`:31`) reads
"Time is still broken though, willfix."

| offset | name (4570 arm) | writer in our tree | gated? | value on our arm (992) |
|---|---|---|---|---|
| +0x00 | SIGNATURE | — none | — | 0 |
| +0x1E | VERSION (THIS_VERSION) | `:92` u16 = 3 | no | 3 |
| +0x20 | CPU_CAPABILITIES | `:93` u32 = `kUP` = 0x8000 (`cpu_capabilities.h:57`) | no | 0x00008000 |
| +0x22 | NCPUS | `:61` u8 = `processor_avail_count`; else-branch `:66` u32 (would also touch +0x24/25) | **yes** | 1 |
| +0x24/+0x25 | USER_PAGE_SHIFT_32/64 | — none | — | 0 / 0 |
| +0x26 | CACHE_LINESIZE | `:106` u16 = 64 (raw offset) | **yes** | 64 |
| +0x28 | SCHED_GEN | — none | — | 0 |
| +0x2C | SPIN_COUNT | — none | — | 0 |
| +0x30 | MEMORY_PRESSURE | — none | — | 0 |
| +0x34/35/36 | ACTIVE/PHYSICAL/LOGICAL_CPUS | `:62-64` u8 = `processor_avail_count` (`assert(processor_avail_count == 1)` `:60`) | **yes** | 1 / 1 / 1 |
| +0x37 | KERNEL_PAGE_SHIFT | — none | — | 0 |
| +0x38 | MEMORY_SIZE | `:94` u64 = `ml_cpu_cache_size(0)` = `machine_info.max_mem` | no | max_mem at populate time (not measured this recon) |
| +0x40 | TIMEOFDAY_DATA.TimeBase | `:129` u64 (`commpage_set_timestamp`) | no | last kernel timestamp, else 0 |
| +0x48 | .TimeStamp_usec | — none | — | 0 |
| +0x4C | .TimeStamp_sec | `:130` (`tofd->TimeStamp_sec = secs`) | no | last timestamp |
| +0x50 | .TimeBaseTicks_per_sec | `:131` forced **0** (the `ticks_per_sec` argument is ignored) | no | 0 |
| +0x58/5C | .TimeBase_magic | — none | — | 0 |
| +0x60 | .TimeBase_add | — none | — | 0 |
| +0x64 | .TimeBase_shift | — none | — | 0 |
| +0x80 | CPUFAMILY | `:90` u32 = `CPUFAMILY_ARM_13` = 0x0cc90e64 (`machine.h:399`); LEO would be UNKNOWN | no | 0x0cc90e64 |
| +0x84 | DEV_FIRM | — none | — | 0 |
| +0x88 | TIMEBASE_OFFSET | — **none anywhere** (the D13 header does not define it; `grep` of commpage.c + cpu_capabilities.h for `0x88`/`USER_TIMEBASE`/`TIMEBASE_OFFSET`: 0 hits) | — | 0 |
| +0x90 | USER_TIMEBASE | — none; deliberate (pmap.c comment: "Offset 0x90 remains zero: Cortex-A8 must take mach_absolute_time's syscall fallback, not attempt an ARM generic-timer userspace register read.") | — | 0 |
| +0x91/0x98 | CONT_HWCLOCK/CONT_TIMEBASE | — none | — | 0 |
| +0xA0 | BOOTTIME_USEC | — none | — | 0 |
| +0xC0 | APPROX_TIME | — none | — | 0 |
| +0x100 | KDEBUG_ENABLE | — none | — | 0 |

Timeofday struct layout (`cpu_capabilities.h:66-74`, `commpage_timeofday_data_t`): `u64 TimeBase;
u32 TimeStamp_usec; u32 TimeStamp_sec; u32 TimeBaseTicks_per_sec; u64 TimeBase_magic; u32
TimeBase_add; u32 TimeBase_shift` → offsets +0x40/48/4C/50/58/60/64 relative to the page. This is
exactly the set of offsets the artifact's userspace reader loads (§3(g)) — an 8-byte-aligned
`TimeBase_magic` at +0x58 is what the artifact's `0x4058`/`0x405c` reads require. (4570 names +0x48
`TimeStamp_sec`; offsets agree, names differ — flag, not a defect for the kernel.)

**Callers requested in the task (memory note was stale).** `gPEClockFrequencyInfo.timebase_frequency_hz`
is assigned at `src/platform/darwin13/pe_msm8974.c:402` and `rtc_configure(...)` is called at `:403`,
unconditionally (`grep -n`: `403: rtc_configure((uint64_t)msm8974_cntfrq);`). The timestamp writers
have live callers too: `commpage_set_timestamp` ← `clock_gettimeofday_set_commpage`
(`osfmk/arm/rtclock.c:341` → `:350 commpage_set_timestamp(abstime - remain, *secs, 0);`) ←
`clock_gettimeofday` (`osfmk/kern/clock.c:349`, adjdelta ≥ 0 path). So `+0x40`/`+0x4C` become nonzero
the first time the kernel services a `gettimeofday` — which is *before* userspace's own later calls.

## 3. Artifact: which fields iOS 7 actually reads (697-image census)

**(a) +0x88 / +0x90 — dyld and libsystem_kernel `_mach_absolute_time`.** dyld (sha
`408061f0ec0a2d03`), VA 0x2be1b2c0 = file offset 0x1b2c0, bytes
`88 c0 04 e3  ff cf 4f e3  08 00 dc e5  00 00 50 e3` =
`movw ip,#0x4088; movt ip,#0xffff; ldrb r0,[ip,#8]; cmp r0,#0` → if the byte at **0xffff4090** is 0:
`mvn ip,#2` (r12 = −3), `svc #0x80`, return. libsystem_kernel has the identical sequence at VA
0x381c5784 (slice offset 0x784). **Value on our arm: +0x90 = 0 ⇒ every userspace
`mach_absolute_time` takes the −3 SWI.** The kernel side of −3 is compiled the *wrong way* on Mi4
(prior study, re-checked): `traps_lo.s:116-119` has the **negated** guard compiled *in*
(`cmn r12,#3; beq swi_trap_tb`) with `swi_trap_tb: movs pc, lr` (returns before any register save —
silently wrong), while the correct arm (`:162-168`, `:264-268 ios7lab_timebase: blx
_mach_absolute_time; b swi_exit64`) is compiled *out*. This is the next frontier after 992 — 991's
231 aborts at 0xffff4090 are precisely this `ldrb`.

**(b) +0x80 CPUFAMILY — libvDSP, MediaToolbox, VideoToolbox, Celestial, libBLAS, libsystem_platform,
IMGSGX535GLDriver** (94 census rows; 51 with an actual dereference). libvDSP (sha
`7829cdda2d683c7e`), VA 0x2c98163c, ARM:
`e51fb1f4 ldr fp,[pc,#-500] → 0x2c981450`  `e51fa1f4 ldr sl,[pc,#-500] → 0x2c981454`
`e59bb000 ldr fp,[fp]` (**the commpage read**) `e15b000a cmp fp,sl` `0a000104 beq 0x2c981a64`, then
`ldr sl,[pc] → 0x2c981458`, `cmp`, same target. Pool at 0x2c981450 = `{0xffff4080, 0x0cc90e64,
0x96077ef1}` = `{CPUFAMILY address, CPUFAMILY_ARM_13, CPUFAMILY_ARM_14}` (`machine.h:399-400`).
**Value on our arm: 0x0cc90e64 = ARM_13 ⇒ the ARM_13 tuning path is taken.** Not zero; risk low.

**(c) +0x20 CPU_CAPABILITIES — vImage @0x2c4d7140 (deref @0x2c4d714e), libGPUSupport @0x31ee78b8,
libGPUSupportMercury @0x31eee2a8.** Value on our arm: `kUP` = 0x8000 — not the real capability bits
(kHasNeon etc. are absent). Readers see "no capability bits" and take generic paths; no crash is
expected but the effect is **not verified** (could not verify what each bit test means).

**(d) +0x22 NCPUS — libsystem_malloc @0x38204206, deref @0x3820420e.** Value 1.

**(e) +0x34/0x35/0x36 — libdispatch, 6 sites** (e.g. `@0x381028ec` deref `@0x381028f8`;
`@0x3810f55c`, `@0x3810f5fa` = +0x36 written at 0x381028ec... the site list in
`/tmp/cp992-data/refined_all.tsv`). **This confirms the comment at `commpage.c:58`** ("Original iOS7
libdispatch sizes its heap from LOGICAL_CPUS at +0x36") — my earlier "libdispatch has no commpage
reference" was the broken pre-fix scan. Values 1/1/1.

**(f) +0x38 / +0x3C MEMORY_SIZE (u64, low/high halves) — libsystem_malloc**
(`@0x3820402c` deref `@0x38204034`; `@0x38204036` deref `@0x38204042`). Value =
`ml_cpu_cache_size(0)` = `machine_info.max_mem`.

**(g) +0x40..+0x64 TIMEOFDAY block — dyld @0x2be1b6fe..0x2be1b764 (9 field addresses) and
libsystem_kernel @0x381d5202..0x381d525a (same routine).** dyld disassembly (Thumb): the routine
materialises `movw/movt` pairs for 0x4040/0x4044 (TimeBase lo/hi), 0x4048, 0x404C, 0x4050, 0x4058,
0x405C, 0x4060, 0x4064, loads each, **re-reads TimeBase and retries the whole block if the two reads
differ** (`eor`/`orrs`/`bne 0x2be1b718` — seqlock), then `blx 0x2be1b2c0` (`_mach_absolute_time`),
computes `(now − TimeBase) × <+0x58> >> <+0x64>` (`umull` / `lsr`), compares the result against
`0x000F4240` (1,000,000) and stores a two-word timeval; returns **0 on success** and **1 when
TimeBase == 0** (`orrs r0, r8, sl; mov r0, #1; beq 0x2be1b806`).
Consequence by value: with `TimeBase == 0` userspace cleanly falls back to the `gettimeofday` syscall
(return 1). But once the kernel has written TimeBase (`clock.c:349` → `rtclock.c:350`) with
`TimeBase_magic/add/shift = 0`, the multiply is by zero and the "fast" time is the **frozen**
TimeStamp — silent-wrong, not a crash (inference from the arithmetic; the exact sec/usec slot
assignment was not decoded). The first kernel-serviced `gettimeofday` therefore *arms* the trap.

**(h) Constants loaded but never dereferenced are data, not commpage reads**: e.g. libicucore
0xffff4000, libGLProgrammability 0xffff4006, libLLVMContainer 0xffff400f, AWD-protobuf 0xffff4017 /
0x405b / 0x407f / 0x40d3, VideoToolbox 0xffff407c ×5, DataDetectorsCore 0xffff4f51, libiconv ×18
(0xffff48f9..0xffff4ff4), libxpc 0xffff4add, plus 353 constants outside the field area
(0xffff41xx..0xffff4fxx). One stray: MediaControlSender 0xffff40ca *with* a deref — offset not in any
known field; **could not verify**, single site.

**(i) No reader found** (whole 697-image census, stated idiom set) for +0x00, +0x1E, +0x24/25,
+0x26, +0x28, +0x2C, +0x30, +0x37, +0x84, +0x91, +0x98, +0xA0, +0xC0, +0x100. This is absence of a
hit, **not proof of absence** (see §6 for detector limits).

## 4. Which fields read ZERO on our arm today

Never written anywhere in our tree (each verified by its writer's guard in §2): +0x00, +0x24, +0x25,
+0x28, +0x2C, +0x30, +0x37, +0x84, **+0x88**, **+0x90**, +0x91, +0x98, +0xA0, +0xC0, +0x100, and the
TOD half-fields +0x48 (usec), +0x58/0x5C (magic), +0x60 (add), +0x64 (shift), +0x50 forced 0.
Of these, the artifact proves live readers for **+0x88/+0x90** (§3a) and the **TOD block** (§3g);
nothing else with a verified reader reads zero. +0x40/+0x4C become nonzero only after the first
kernel `gettimeofday`.

## 5. Proposed next rung (fields are missing where it counts)

1. **`traps_lo.s` −3 timebase SWI — the wall after 992.** With 992's mapping, dyld/libsystem no
   longer fault at 0xffff4090 but fall into the −3 SWI, whose Mi4 path is the silently-wrong
   `swi_trap_tb: movs pc, lr`. The correct implementation already exists verbatim in the tree;
   open the negated guard at `:116` and the `:162`/`:264` gates (patch + checker per the prior
   `docs/reference/d13-launchd-early-abi-study.md` §4/checker sketch — `tools/patch_d13_timebase_swi.py`
   + `tools/check_timebase_swi.py`), with a one-shot marker in `ios7lab_timebase`.
   Falsification pre-registered: userspace `mach_absolute_time` still returns 0/garbage, or
   `swi_trap_tb` still taken.
2. **TOD block safety valve** (same press budget): decide consciously between (a) keeping
   `TimeBase == 0` semantics — userspace's return-1 fallback gives a *correct* `gettimeofday` via
   syscall — and (b) populating `TimeBase_magic/add/shift` from the kernel's timebase conversion.
   Add a marker printf in `clock_gettimeofday_set_commpage` (`rtclock.c:341`) so the press log shows
   when `TimeBase` first becomes nonzero — a nonzero TimeBase with zero magic is the frozen-clock
   trap of §3(g).
3. Defer: +0x20 value fidelity (`kUP` vs real capability bits), +0x26/+0x1E (no reader found).

## 6. Uncertainties / corrections

- My first census's "only dyld reads the commpage" was an artifact of a `/usr/lib`-only walk plus a
  broken Thumb `movt` filter; corrected in the 697-image run quoted throughout (§ method).
- "Reader" evidence = a constant load followed by a same-register memory access within 44 bytes.
  The deref detector misses `ldrd [Rn]`, register-copied bases and `add`-formed bases, so reader
  lists are lower bounds and §3(i) "no reader found" is not proof of absence.
- dyld's TOD routine: offsets verified by disassembly; the sec/usec slot *names* follow the D13
  struct and 4570's naming (which differ); the exact assignment was not decoded.
- Runtime values not measured this recon (no device access): `_COMM_PAGE_MEMORY_SIZE` at populate
  time, the actual TimeBase/TimeStamp contents, and whether any 992-boot log line shows them.
- The `_COMM_PAGE_VERSION`/`MYSTERY_VALUE` identity rests on offset (0x1E in both headers) and value
  (3 in both); the D13 header names it differently.