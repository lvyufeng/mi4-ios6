# Experiment 47 — Stage44 Mach-O Fixture Load Plan

Date: 2026-06-06

Goal: move another step toward a real XNU loader/handoff contract by replacing Stage43's hand-written Mach-O byte array with a reproducible, non-proprietary host-generated Mach-O fixture, parsing 32-bit section records, reporting public XNU prelink-related segments/sections, and recording a proposed physical load plan used to refine future-XNU `topOfKernelData` / `avail_start` assumptions.

Stage44 still does **not** run XNU or iOS. It does not branch to parsed Mach-O entry metadata, does not load an Apple kernelcache, does not program XNU TTBRs, and does not enable caches. It preserves the Stage43 safety gates and extends the loader preflight only.

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
- SGI/timer IRQ selftests are rerun before the Stage44 loader preflight.

## What Stage44 adds over Stage43

Stage43 proved a bounded Mach-O/XNU loader preflight over an inert embedded byte-array artifact.

Stage44 adds:

- a new standalone `stage44/` payload copied from Stage43,
- Stage44 status/magic/log prefixes (`0x44000001`, `MI4IOS6_STAGE44`),
- a deterministic host-side fixture generator:
  - `tools/mkmacho_fixture.py`,
- a generated checked-in C fixture:
  - `stage44/macho_fixture.c`,
- a raw ignored generated fixture for hash/host inspection:
  - `out/stage44/stage44_fixture.macho`,
- section-level parsing for 32-bit `LC_SEGMENT` commands,
- public-XNU-relevant section reporting for:
  - `__TEXT,__text`,
  - `__DATA,__const`,
  - `__PRELINK_TEXT,__text`,
  - `__PRELINK_INFO,__info`,
  - `__PRELINK_INFO,__kernel`,
  - `__PRELINK_INFO,__kexts`,
  - `__PRELINK_STATE,__kernel`,
  - `__PRELINK_STATE,__kexts`,
- prelink segment reporting for:
  - `__PRELINK_TEXT`,
  - `__PRELINK_INFO`,
  - `__PRELINK_STATE`,
- a bounded proposed physical load-plan descriptor,
- refined proposed `topOfKernelData` from `load_phys_end` rather than the tiny Stage43 fixed-span fallback,
- continued explicit safety reporting proving no execution/no TTBR switch/caches unchanged/no persistent write.

## Generated Mach-O fixture

The fixture is generated from public constants by:

```bash
/mnt/data/mi4-ios6/tools/mkmacho_fixture.py \
  --c-output /mnt/data/mi4-ios6/stage44/macho_fixture.c \
  --bin-output /mnt/data/mi4-ios6/out/stage44/stage44_fixture.macho \
  --symbol-prefix stage44
```

The fixture contains no Apple binary code and no executable payload that Stage44 branches to. It only models public Mach-O layout facts needed by the loader preflight.

Raw fixture hash:

```text
42ad6e6e54878289d95d8adbe928556cd7840ae6aa029b8e1af13c4825129434  out/stage44/stage44_fixture.macho
```

The generated fixture has:

- `MH_MAGIC`,
- `CPU_TYPE_ARM`,
- `CPU_SUBTYPE_ARM_V7`,
- `MH_PRELOAD`,
- six `LC_SEGMENT` commands,
- one bounded zero-symbol `LC_SYMTAB`,
- one minimal `LC_UNIXTHREAD` entry-metadata marker,
- inert marker bytes as section payload.

## Mach-O parser facts

Recovered Stage44 probe facts:

```text
macho_magic=0xfeedface
macho_cputype=0x0000000c
macho_cpusubtype=0x00000009
macho_filetype=0x00000005
macho_ncmds=0x00000008
macho_sizeofcmds=0x00000398
macho_command_mask=0x00000007
macho_segment_count=0x00000006
macho_segment_mask=0x000000e7
macho_section_count=0x00000008
macho_section_mask=0x000000ff
macho_prelink_segment_mask=0x000000e0
macho_prelink_section_mask=0x000000fc
macho_entry_kind_mask=0x80000001
macho_min_vmaddr=0x80008000
macho_max_vmaddr=0x80011000
macho_max_file_extent=0x000004e0
macho_validation_mask=0x00003fff
macho_failure_mask=0x00000000
```

Interpretation:

- command mask `0x00000007` means `LC_SEGMENT`, `LC_SYMTAB`, and `LC_UNIXTHREAD` were observed,
- segment mask `0x000000e7` means `__TEXT`, `__DATA`, `__LINKEDIT`, `__PRELINK_TEXT`, `__PRELINK_INFO`, and `__PRELINK_STATE` were observed,
- section mask `0x000000ff` means the generated fixture's eight known public-XNU-relevant sections were observed,
- prelink segment mask `0x000000e0` means all three Stage44 prelink segment forms were observed,
- prelink section mask `0x000000fc` means the prelink section/reporting bits were observed,
- entry mask `0x80000001` means kernel-style entry metadata was observed and explicitly marked not executed,
- validation mask `0x00003fff` means Stage44 completed base Mach-O validation plus section, load-plan, and prelink-report checks,
- failure mask `0x00000000` means the parser accepted the bounded generated fixture.

## Proposed physical load plan

Stage44 does not actually copy or execute the fixture. It records a proposed future loader map:

```text
macho_load_plan_count=0x00000006
macho_load_vm_base=0x80008000
macho_load_vm_end=0x80011000
macho_load_file_base=0x000003c0
macho_load_file_end=0x000004e0
macho_load_phys_base=0x80000000
macho_load_phys_end=0x80009000
macho_load_phys_size=0x00009000
macho_load_plan_status=0x44000001
```

The mapping rule is:

```text
load_vm_base = minimum segment vmaddr
entry.physaddr = RAM_PHYS_BASE + (segment.vmaddr - load_vm_base)
entry.physsize = align_up(segment.vmsize, 4096)
load_phys_end = max(entry.physaddr + entry.physsize)
```

This gives a future-XNU proposal:

```text
xnu_proposed_loaded_phys_base=0x80000000
xnu_proposed_loaded_phys_end=0x80009000
xnu_proposed_loaded_file_end=0x000004e0
xnu_proposed_topOfKernelData=0x8000c000
xnu_proposed_ttep_workspace_limit=0x80016000
xnu_proposed_avail_start=0x80016000
```

The 10-page TTE/early workspace rule is unchanged from the public ARM XNU interpretation recorded in Stage43, but Stage44 now derives its base from the proposed physical load-plan end.

## Loader-preflight facts

Selected public source baselines remain:

```text
loader_xnu_baseline_tag=0x20502213       # xnu-2050.22.13
loader_xnu_baseline_commit=0xcc8a9b0c    # cc8a9b0c...
loader_xnu_master_version=0x000c0300     # 12.3.0
loader_xnu_arm_reference=0x45700146      # xnu-4570.1.46 ARM reference
```

Readiness and safety facts:

```text
loader_satisfied_mask=0x000000ff
loader_checksum=0x934a9875
loader_status=0x44000001
loader_safety_mask=0x0000001f
```

Stage44 still records the same intentional remaining platform gap mask (`0x0000001f`) for real XNU bring-up blockers: pmap bootstrap, MSM8974 pexpert, GIC hook, timer hook, and IOKit/platform stack.

## Built image

```bash
/mnt/data/mi4-ios6/stage44/build.sh
```

Successful local build:

```text
out/stage44/stage44-qcdt.img
sha256=b04f0da87c37acd1ca9b9acf6111b4fb408cb1d3efce24baf52ef199428f4c87
```

Build hashes:

```text
42ad6e6e54878289d95d8adbe928556cd7840ae6aa029b8e1af13c4825129434  out/stage44/stage44_fixture.macho
a176e8c56cd672d77eb8a806a1be8f6f444f05517805f207e64965a57dfcfd07  out/stage44/stage44.elf
ccd662ca4ded7e3f4469db420687ee14a0ade79de0123d16b46eec471e320d2d  out/stage44/stage44.bin
1bafdf4e3318001a60aefef681fd9a5242a8eb3574a137b96f09192ed7027fac  out/stage44/stage44.img
b04f0da87c37acd1ca9b9acf6111b4fb408cb1d3efce24baf52ef199428f4c87  out/stage44/stage44-qcdt.img
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=144340 (0x233d4)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage44 mi4ios6=stage44 c-runtime apple-dt macho-fixture load-plan xnu-preflight
part=kernel offset=0x800 size=144340 sha256=ccd662ca4ded7e3f4469db420687ee14a0ade79de0123d16b46eec471e320d2d
part=dt.img offset=0x24000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Important symbols:

```text
000080a0 T stage44_vectors
0000bf44 T stage44_loader_preflight_run
0001a800 T kernel_entry
0001ab48 T test_kernel_entry
0001ace0 T stage44_main
0001c558 T stage44_embedded_macho_size
0001c55c T stage44_embedded_macho
000310b0 b stage44_loader_preflight_block
0003e000 B __stage44_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage44/stage44-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2606 KB)                       OKAY [  0.083s]
Booting                                            OKAY [  0.006s]
Finished. Total time: 0.094s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage44-last_kmsg.txt
```

The recovered log was 99264 bytes and contained:

```text
1484 MI4IOS6_STAGE44 markers
1458 MI4IOS6_STAGE44_XNU markers
```

## Key recovered markers

Inherited high-root/MMU success remains intact:

```text
MI4IOS6_STAGE44_XNU high_root_status=0x44000001
MI4IOS6_STAGE44_XNU mmu high bootstrap selftest ok
```

Post-root IRQ retests still pass:

```text
MI4IOS6_STAGE44_XNU gic SGI selftest ok
MI4IOS6_STAGE44_XNU gic timer selftest ok
```

Mach-O fixture/load-plan markers:

```text
MI4IOS6_STAGE44_XNU Stage44 Mach-O/XNU loader preflight begin
MI4IOS6_STAGE44_XNU Stage44 Mach-O probe begin
MI4IOS6_STAGE44_XNU macho_magic=0xfeedface
MI4IOS6_STAGE44_XNU macho_segment_mask=0x000000e7
MI4IOS6_STAGE44_XNU macho_section_count=0x00000008
MI4IOS6_STAGE44_XNU macho_section_mask=0x000000ff
MI4IOS6_STAGE44_XNU macho_prelink_segment_mask=0x000000e0
MI4IOS6_STAGE44_XNU macho_prelink_section_mask=0x000000fc
MI4IOS6_STAGE44_XNU macho_load_plan_count=0x00000006
MI4IOS6_STAGE44_XNU macho_load_phys_base=0x80000000
MI4IOS6_STAGE44_XNU macho_load_phys_end=0x80009000
MI4IOS6_STAGE44_XNU macho_validation_mask=0x00003fff
MI4IOS6_STAGE44_XNU macho_failure_mask=0x00000000
MI4IOS6_STAGE44_XNU xnu_proposed_topOfKernelData=0x8000c000
MI4IOS6_STAGE44_XNU xnu_proposed_avail_start=0x80016000
MI4IOS6_STAGE44_XNU loader_status=0x44000001
MI4IOS6_STAGE44_XNU Stage44 XNU handoff disabled: no Mach-O entry executed, no TTBR switch, caches unchanged
MI4IOS6_STAGE44_XNU Stage44 Mach-O/XNU loader preflight ok
```

Final success markers:

```text
MI4IOS6_STAGE44_XNU kernel_entry ok
MI4IOS6_STAGE44 kernel_entry returned success
MI4IOS6_STAGE44 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage44 is still not a real XNU boot, but it improves the loader side materially. The input artifact is no longer a hand-maintained byte array: it is generated by a host tool from public Mach-O layout constants and checked in as reproducible C. The target-side parser now walks real 32-bit section records, validates their bounds, records prelink-facing names seen in public XNU sources, and calculates a proposed physical placement plan before deriving `topOfKernelData`.

This makes the future handoff contract more concrete: the project now has hardware-proven logs showing a generated Mach-O fixture can drive segment/section/prelink reporting plus proposed physical load ranges while the inherited MMU/GIC/timer safety gates remain intact.

The explicit blockers remain real pmap bootstrap, a real MSM8974 pexpert implementation, XNU interrupt-controller and timer hooks, IOKit/platform drivers, real kernelcache/loading details, and later code-signing/userspace questions.

## Success criteria — met

1. Stage44 directory and build outputs created: yes
2. Host fixture generator added: yes
3. Generated checked-in C fixture added: yes
4. Raw generated Mach-O fixture produced under ignored `out/stage44`: yes
5. Bootloader accepted `stage44-qcdt.img`: yes
6. Stage43-derived Apple-DT/PE_state/GIC/timebase/MMU checks still passed: yes
7. SGI IRQ retest passed after high-root/MMU path: yes
8. Timer IRQ retest passed after high-root/MMU path: yes
9. Mach-O parser accepted `MH_MAGIC`: yes
10. Mach-O parser accepted ARMv7 CPU type/subtype: yes
11. Mach-O parser accepted `MH_PRELOAD`: yes
12. Mach-O parser observed six segment commands: yes
13. Mach-O parser observed required segments `__TEXT`, `__DATA`, and `__LINKEDIT`: yes
14. Mach-O parser parsed section records: yes
15. Mach-O parser reported `__DATA,__const`: yes
16. Mach-O parser reported prelink segment/section readiness: yes
17. Physical load-plan descriptor returned status `0x44000001`: yes
18. Proposed `topOfKernelData` was derived from `load_phys_end`: yes
19. Loader safety mask was complete: yes
20. Loader preflight returned status `0x44000001`: yes
21. `kernel_entry` returned success: yes
22. payload reset through PS_HOLD: yes

## Next stage

Stage45 should keep moving toward a real XNU handoff without executing XNU yet. Practical next options:

- add a bounded dry-run segment copy/zero-fill staging descriptor that proves how the load plan would materialize bytes in RAM without branching to them,
- model the early ARM XNU `_start` page-table/TTE layout more concretely from the Stage44 `topOfKernelData` workspace,
- add a dry-run translation-table descriptor for the proposed `virtBase`/`physBase` kernel mapping while still not programming TTBRs,
- preserve non-persistent booting, ram_console diagnostics, PS_HOLD reset, identity/recovery mappings, and SGI/timer retests.
