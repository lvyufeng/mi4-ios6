# Experiment 46 — Stage43 Mach-O/XNU Loader Probe

Date: 2026-06-06

Goal: pivot from purely synthetic XNU-adjacent descriptors toward a real loader-facing XNU preflight by parsing a bounded Mach-O-shaped artifact and recording the boot ABI, Apple-DT, timer/interrupt, and page-table-workspace facts needed before any future XNU handoff.

Stage43 still does **not** run XNU or iOS. It does not branch to parsed Mach-O entry metadata, does not load a proprietary Apple kernelcache, does not program XNU TTBRs, and does not enable caches. It preserves the Stage42 safety gates and adds a small freestanding Mach-O parser plus a loader-preflight record after the inherited high-root/MMU and IRQ retests.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- No parsed Mach-O entrypoint execution.
- No real XNU `_start` / `arm_init` handoff.
- No TTBR switch to the proposed XNU tuple.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun before the Stage43 loader preflight.

## What Stage43 adds over Stage42

Stage42 proved the kernel collection dependency-resolution descriptor and preserved the MMU/GIC/timer safety checks.

Stage43 adds:

- a new standalone `stage43/` payload copied from Stage42,
- Stage43 status/magic/log prefixes (`0x43000001`, `MI4IOS6_STAGE43`),
- a `device-tree` Apple-DT semantic node for public ARM XNU pexpert-style `DTFindEntry("name", "device-tree")` readiness,
- a tiny embedded 32-bit little-endian ARM Mach-O-shaped artifact,
- a freestanding bounded Mach-O parser for:
  - `MH_MAGIC`,
  - `CPU_TYPE_ARM`,
  - `CPU_SUBTYPE_ARM_V7`,
  - `MH_PRELOAD`,
  - `LC_SEGMENT`,
  - `LC_SYMTAB`,
  - `LC_UNIXTHREAD`,
  - required segments `__TEXT`, `__DATA`, and `__LINKEDIT`,
- a loader-preflight record with:
  - selected public XNU baseline metadata,
  - observed Mach-O command and segment masks,
  - actual safe Stage43 boot-args fields,
  - proposed future-XNU `virtBase`, `physBase`, `memSize`, and `topOfKernelData`,
  - ARM XNU-style 10-page early page-table workspace span,
  - Apple-DT semantic readiness mask,
  - known remaining platform-gap mask,
  - timer/interrupt readiness mask,
  - explicit safety mask proving no execution/no TTBR switch/caches unchanged/no persistent write.

## Embedded Mach-O-shaped artifact

The embedded artifact is deliberately synthetic and inert. It is useful because it exercises real public Mach-O header/load-command contracts without including or executing any Apple binary.

Observed probe facts:

```text
macho_artifact_base=0x0001be30
macho_artifact_size=0x00000120
macho_magic=0xfeedface
macho_cputype=0x0000000c
macho_cpusubtype=0x00000009
macho_filetype=0x00000005
macho_ncmds=0x00000005
macho_sizeofcmds=0x000000d0
macho_command_mask=0x00000007
macho_segment_mask=0x00000007
macho_entry_kind_mask=0x80000001
macho_entry_not_executed=0x00000001
macho_min_vmaddr=0x80008000
macho_max_vmaddr=0x8000b000
macho_max_file_extent=0x00000120
macho_validation_mask=0x000003ff
macho_failure_mask=0x00000000
```

Interpretation:

- command mask `0x00000007` means `LC_SEGMENT`, `LC_SYMTAB`, and `LC_UNIXTHREAD` were observed,
- segment mask `0x00000007` means `__TEXT`, `__DATA`, and `__LINKEDIT` were observed,
- entry mask `0x80000001` means a kernel-style entry metadata command was observed and explicitly marked not executed,
- validation mask `0x000003ff` means the required parser checks were complete,
- failure mask `0x00000000` means the parser accepted the bounded artifact.

## Loader-preflight facts

Selected public source baselines recorded by the preflight:

```text
loader_xnu_baseline_tag=0x20502213       # xnu-2050.22.13
loader_xnu_baseline_commit=0xcc8a9b0c    # cc8a9b0c...
loader_xnu_master_version=0x000c0300     # 12.3.0
loader_xnu_arm_reference=0x45700146      # xnu-4570.1.46 ARM reference
```

Actual safe Stage43 boot-args facts remain skeleton/identity-oriented:

```text
loader_actual_virtBase=0x00000000
loader_actual_physBase=0x00008000
loader_actual_topOfKernelData=0x0003e000
```

The proposed future-XNU tuple is recorded separately and is **not** used to switch MMU state:

```text
xnu_proposed_virtBase=0x80008000
xnu_proposed_physBase=0x80000000
xnu_proposed_memSize=0x5e500000
xnu_proposed_topOfKernelData=0x80010000
xnu_proposed_ttep_workspace_limit=0x8001a000
xnu_proposed_avail_start=0x8001a000
xnu_workspace_range_ok=0x00000001
```

This follows the public ARM XNU shape where `topOfKernelData` is used as early page-table workspace. Stage43 records a 10-page workspace window and first available page, but does not program those tables as XNU tables.

Readiness and blocker masks:

```text
apple_dt_semantic_mask=0x00000fff
platform_gap_mask=0x0000001f
loader_interrupt_ready_mask=0x0000000f
loader_safety_mask=0x0000001f
loader_satisfied_mask=0x000000ff
loader_checksum=0x944ba8c1
loader_status=0x43000001
```

Interpretation:

- Apple-DT semantic readiness includes `/chosen`, `/memory`, `/cpus`, interrupt-controller, timer, `device-tree`, `target-type`, `model`, boot-args, ram-console, and CPU clock/timebase properties.
- `platform_gap_mask=0x0000001f` is intentional: Stage43 records that real XNU remains blocked by pmap bootstrap, MSM8974 pexpert implementation, GIC hook, timer hook, and IOKit stack work.
- `loader_safety_mask=0x0000001f` confirms the preflight remained non-executing and non-persistent.

## Built image

```bash
/mnt/data/mi4-ios6/stage43/build.sh
```

Successful local build:

```text
out/stage43/stage43-qcdt.img
sha256=9500627807e790f8693ec364803b67236716705c87281d102d051d9a6c370323
```

Build hashes:

```text
778c880e7b1334253e28400ace2b4e517f0c245dee71db4591a56d45329df265  out/stage43/stage43.elf
5db07247681f19f8c9f6ca654e7a8631275a9546aece38857ace9977e336aa81  out/stage43/stage43.bin
a51fd16aaf4ae7512b731db3b0150355747bcce2119519864bdff91b34d19094  out/stage43/stage43.img
9500627807e790f8693ec364803b67236716705c87281d102d051d9a6c370323  out/stage43/stage43-qcdt.img
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=140784 (0x225f0)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage43 mi4ios6=stage43 c-runtime apple-dt macho-loader-probe xnu-preflight
part=kernel offset=0x800 size=140784 sha256=5db07247681f19f8c9f6ca654e7a8631275a9546aece38857ace9977e336aa81
part=dt.img offset=0x23000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Important symbols:

```text
000080a0 T stage43_vectors
0000bbbc t stage43_kernel_root
00013ac8 t stage43_startup_entry
00014c18 T mmu_high_bootstrap_selftest
0001a0d8 T kernel_entry
0001a420 T test_kernel_entry
0001a5b8 T stage43_main
0001be2c T stage43_embedded_macho_size
0001be30 T stage43_embedded_macho
000310b0 b stage43_loader_preflight_block
00034458 b stage43_bootstrap_state_block
00038000 b stage43_l1_table
0003e000 B __stage43_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage43/stage43-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2602 KB)                       OKAY [  0.083s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.094s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage43-last_kmsg.txt
```

The recovered log was 97734 bytes and contained:

```text
1455 MI4IOS6_STAGE43 markers
1429 MI4IOS6_STAGE43_XNU markers
```

## Key recovered markers

Inherited high-root/MMU success remains intact:

```text
MI4IOS6_STAGE43_XNU high_root_status=0x43000001
MI4IOS6_STAGE43_XNU mmu high bootstrap selftest ok
```

Post-root IRQ retests still pass:

```text
MI4IOS6_STAGE43_XNU gic SGI selftest ok
MI4IOS6_STAGE43_XNU gic timer selftest ok
```

Mach-O/XNU loader-preflight markers:

```text
MI4IOS6_STAGE43_XNU Stage43 Mach-O/XNU loader preflight begin
MI4IOS6_STAGE43_XNU Stage43 Mach-O probe begin
MI4IOS6_STAGE43_XNU macho_magic=0xfeedface
MI4IOS6_STAGE43_XNU macho_cputype=0x0000000c
MI4IOS6_STAGE43_XNU macho_cpusubtype=0x00000009
MI4IOS6_STAGE43_XNU macho_filetype=0x00000005
MI4IOS6_STAGE43_XNU macho_command_mask=0x00000007
MI4IOS6_STAGE43_XNU macho_segment_mask=0x00000007
MI4IOS6_STAGE43_XNU macho_entry_kind_mask=0x80000001
MI4IOS6_STAGE43_XNU macho_validation_mask=0x000003ff
MI4IOS6_STAGE43_XNU macho_failure_mask=0x00000000
MI4IOS6_STAGE43_XNU Stage43 Mach-O probe ok; entry metadata observed but not executed
MI4IOS6_STAGE43_XNU xnu_proposed_topOfKernelData=0x80010000
MI4IOS6_STAGE43_XNU apple_dt_semantic_mask=0x00000fff
MI4IOS6_STAGE43_XNU platform_gap_mask=0x0000001f
MI4IOS6_STAGE43_XNU loader_satisfied_mask=0x000000ff
MI4IOS6_STAGE43_XNU loader_status=0x43000001
MI4IOS6_STAGE43_XNU Stage43 XNU handoff disabled: no Mach-O entry executed, no TTBR switch, caches unchanged
MI4IOS6_STAGE43_XNU Stage43 Mach-O/XNU loader preflight ok
```

Final success markers:

```text
MI4IOS6_STAGE43_XNU kernel_entry ok
MI4IOS6_STAGE43 kernel_entry returned success
MI4IOS6_STAGE43 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage43 is the first stage that validates a real loader-facing file format contract instead of only extending the synthetic descriptor chain. The embedded fixture is intentionally not a real kernel, but the parser and preflight now exercise the concrete Mach-O/XNU concepts needed for future work: ARM CPU type/subtype, kernel-style filetype, segment commands, required segment names, entry metadata that must not yet be executed, a proposed `virtBase`/`physBase`/`topOfKernelData` tuple, and XNU-style page-table workspace reservation.

The result is a safer foundation for future XNU bring-up: we now have hardware-proven logs showing that the Stage42 MMU/GIC/timer gates still work and that the Stage43 loader preflight can accept and reason about a bounded XNU-shaped Mach-O artifact.

This is still not XNU-runnable. The explicit blockers remain pmap bootstrap, MSM8974 pexpert implementation, interrupt-controller hook, timer/deadline hook, IOKit/platform driver stack, real kernelcache/loading details, and later code-signing/userspace questions.

## Success criteria — met

1. Stage43 directory and build outputs created: yes
2. Stage43 command line identifies `macho-loader-probe` and `xnu-preflight`: yes
3. bootloader accepted `stage43-qcdt.img`: yes
4. Stage42-derived Apple-DT/PE_state/GIC/timebase/MMU checks still passed: yes
5. SGI IRQ retest passed after high-root/MMU path: yes
6. timer IRQ retest passed after high-root/MMU path: yes
7. Mach-O parser accepted `MH_MAGIC`: yes
8. Mach-O parser accepted ARMv7 CPU type/subtype: yes
9. Mach-O parser accepted `MH_PRELOAD`: yes
10. Mach-O parser observed `LC_SEGMENT`, `LC_SYMTAB`, and `LC_UNIXTHREAD`: yes
11. Mach-O parser observed required segments `__TEXT`, `__DATA`, and `__LINKEDIT`: yes
12. Mach-O parser recorded entry metadata without execution: yes
13. Proposed XNU `virtBase`/`physBase`/`memSize`/`topOfKernelData` was logged: yes
14. 10-page early page-table workspace and `avail_start` were logged: yes
15. Apple-DT semantic readiness mask was complete: yes
16. Remaining XNU platform gaps were explicitly recorded: yes
17. Loader safety mask was complete: yes
18. Loader preflight returned status `0x43000001`: yes
19. `kernel_entry` returned success: yes
20. payload reset through PS_HOLD: yes

## Next stage

Stage44 should keep moving toward a real XNU loader without jumping into XNU yet. Practical next options:

- replace the embedded byte-array fixture with a host-generated, checked-in, non-proprietary Mach-O fixture and matching parser tests,
- add a physical load-plan descriptor that maps Mach-O VM segments to proposed physical load ranges,
- expand section parsing enough to report `__DATA,__const` and prelink-related segments,
- refine `topOfKernelData` placement using real loaded segment/file spans rather than the tiny synthetic fixture,
- keep all Stage43 safety gates, non-persistent booting, ram_console logging, and SGI/timer retests.
