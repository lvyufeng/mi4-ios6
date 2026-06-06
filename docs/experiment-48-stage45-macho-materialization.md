# Experiment 48 — Stage45 Local Mach-O Materialization

Date: 2026-06-06

Goal: prove the public Mach-O segment materialization rule on-device while keeping the operation safely local. Stage45 actually copies file-backed segment bytes and zero-fills segment tails, but only into a dedicated Stage45-owned BSS arena. The proposed XNU physical load plan remains dry-run metadata and is never written.

Stage45 still does **not** run XNU or iOS. It does not branch to parsed Mach-O entry metadata, does not jump to XNU `_start` / `arm_init`, does not write the proposed physical load addresses, does not program TTBRs from the proposed XNU tuple, and does not enable caches. It preserves the Stage44 safety gates and adds local materialization/reparse checks only.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- No parsed Mach-O entrypoint execution.
- No real XNU `_start` / `arm_init` handoff.
- No writes to proposed XNU physical load addresses.
- No TTBR switch to the proposed XNU tuple.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun before the Stage45 loader preflight.
- The Stage45 materialization destination is only `stage45_macho_staging_arena[]`, a local BSS arena aligned to 4096 bytes.

## What Stage45 adds over Stage44

Stage44 generated and parsed a non-proprietary Mach-O fixture, reported section/prelink readiness, and recorded a proposed physical load plan.

Stage45 adds:

- a new standalone `stage45/` payload copied from Stage44,
- Stage45 status/magic/log prefixes (`0x45000001`, `MI4IOS6_STAGE45`),
- stage-parameterized fixture generation in `tools/mkmacho_fixture.py`,
- a Stage45 fixture layout where the first `__TEXT` segment has `fileoff=0` so materialized memory contains a Mach-O header at arena base,
- a bounded local staging arena:
  - `stage45_macho_staging_arena[0x10000]`,
  - aligned to 4096 bytes,
- a staging descriptor recording copy/zero-fill operations,
- actual copy+zero-fill into the local arena only,
- reparse of the materialized arena image,
- public-XNU-style `getlastaddr`-style max-loaded-address recording,
- marker checks for:
  - `ST45-TEXT`,
  - `ST45-DATA`,
  - `ST45-PRELINK-TEXT`,
- zero-fill tail scans for every segment with `vmsize > filesize`,
- loader safety bits proving local-arena-only and no-physical-write behavior.

## Generated Mach-O fixture

The fixture is generated from public constants by:

```bash
/mnt/data/mi4-ios6/tools/mkmacho_fixture.py \
  --c-output /mnt/data/mi4-ios6/stage45/macho_fixture.c \
  --bin-output /mnt/data/mi4-ios6/out/stage45/stage45_fixture.macho \
  --symbol-prefix stage45
```

The fixture contains no Apple binary code and no executable payload that Stage45 branches to. It only models public Mach-O layout facts needed by the loader preflight and materialization checks.

Raw fixture hash:

```text
1cd864f6386a1631303ab0b1b7534d48b6f0a33e8bddab97427573067fd615b3  out/stage45/stage45_fixture.macho
```

The generated fixture has:

- `MH_MAGIC`,
- `CPU_TYPE_ARM`,
- `CPU_SUBTYPE_ARM_V7`,
- `MH_PRELOAD`,
- six `LC_SEGMENT` commands,
- one bounded zero-symbol `LC_SYMTAB`,
- one minimal `LC_UNIXTHREAD` entry-metadata marker,
- inert marker bytes as section payload,
- a Stage45+ layout where `__TEXT.fileoff=0`, allowing the local arena image to be reparsed from its base.

## Mach-O parser facts

Recovered original-artifact probe facts:

```text
macho_artifact_size=0x000004e0
macho_segment_mask=0x000000e7
macho_section_mask=0x000000ff
macho_prelink_segment_mask=0x000000e0
macho_prelink_section_mask=0x000000fc
macho_load_phys_base=0x80000000
macho_load_phys_end=0x80009000
macho_load_phys_size=0x00009000
```

The materialized-arena reparse intentionally reports a larger input span because it reparses the local arena image instead of the compact raw fixture:

```text
macho_artifact_size=0x00009000
macho_segment_mask=0x000000e7
macho_section_mask=0x000000ff
macho_prelink_segment_mask=0x000000e0
macho_prelink_section_mask=0x000000fc
macho_load_phys_base=0x80000000
macho_load_phys_end=0x80009000
macho_load_phys_size=0x00009000
```

Interpretation:

- segment mask `0x000000e7` means `__TEXT`, `__DATA`, `__LINKEDIT`, `__PRELINK_TEXT`, `__PRELINK_INFO`, and `__PRELINK_STATE` were observed,
- section mask `0x000000ff` means the generated fixture's eight known public-XNU-relevant sections were observed,
- prelink segment mask `0x000000e0` means all three Stage45 prelink segment forms were observed,
- prelink section mask `0x000000fc` means the prelink section/reporting bits were observed,
- proposed physical load range remains `0x80000000`-`0x80009000`, but Stage45 does not write that range.

## Local materialization facts

Stage45 applies the public Mach-O loader rule in a local arena:

```text
for each LC_SEGMENT:
  copy filesize bytes from artifact[fileoff:fileoff+filesize] to loaded vmaddr
  zero-fill vmsize - filesize bytes after the copied part
```

The destination mapping is local-only:

```text
vm_base      = min(segment.vmaddr)
arena_offset = segment.vmaddr - vm_base
copy_dst     = stage45_macho_staging_arena + arena_offset
zero_dst     = copy_dst + segment.filesize
```

Recovered staging facts:

```text
macho_staging_arena_base=0x00037000
macho_staging_arena_end=0x00047000
macho_staging_vm_base=0x80008000
macho_staging_vm_end=0x80011000
macho_staging_vm_span=0x00009000
macho_staging_entry_count=0x00000006
macho_staging_file_bytes=0x000004e0
macho_staging_zero_bytes=0x00008b20
macho_staging_marker_mask=0x00000007
macho_staging_zero_mask=0x0000003f
macho_staging_reparsed_vm_base=0x80008000
macho_staging_reparsed_vm_end=0x80011000
macho_staging_getlastaddr=0x80011000
macho_staging_validation_mask=0x0003c000
macho_staging_failure_mask=0x00000000
macho_staging_status=0x45000001
```

Interpretation:

- six segment entries were materialized,
- `0x4e0` file-backed bytes were copied,
- `0x8b20` bytes were zero-filled,
- marker mask `0x7` means required `ST45-TEXT`, `ST45-DATA`, and `ST45-PRELINK-TEXT` marker prefixes were observed at their materialized section addresses,
- zero mask `0x3f` means all six segment zero-fill tails passed scanning,
- the arena reparse matched the original Mach-O structure and computed `getlastaddr=0x80011000`,
- staging status returned `0x45000001`.

## Loader-preflight facts

Selected public source baselines remain:

```text
loader_xnu_baseline_tag=0x20502213       # xnu-2050.22.13
loader_xnu_baseline_commit=0xcc8a9b0c    # cc8a9b0c...
loader_xnu_master_version=0x000c0300     # 12.3.0
loader_xnu_arm_reference=0x45700146      # xnu-4570.1.46 ARM reference
```

Materialization and readiness facts:

```text
loader_materialized_status=0x45000001
loader_materialized_file_bytes=0x000004e0
loader_materialized_zero_bytes=0x00008b20
xnu_proposed_topOfKernelData=0x8000c000
xnu_proposed_ttep_workspace_limit=0x80016000
xnu_proposed_avail_start=0x80016000
loader_safety_mask=0x0000007f
loader_satisfied_mask=0x000001ff
loader_checksum=0x12499cd8
loader_status=0x45000001
```

The safety mask now includes the Stage44 safety bits plus:

- local-arena-only materialization,
- no writes to proposed physical load addresses.

The loader satisfaction mask now includes the Stage45 materialization requirement, so the complete mask is `0x000001ff`.

## Built image

```bash
/mnt/data/mi4-ios6/stage45/build.sh
```

Successful local build:

```text
out/stage45/stage45-qcdt.img
sha256=bbc90109cd6c8cbcb035bc99f4b8e9f39b9f4740ba58a549205aae3e9716572a
```

Build hashes:

```text
1cd864f6386a1631303ab0b1b7534d48b6f0a33e8bddab97427573067fd615b3  out/stage45/stage45_fixture.macho
b023cb1b08078c53ffd639befd679921c0123ae2e7286689aa5af87cfcc3f273  out/stage45/stage45.elf
09c9885d787c3a3544086c29fc000815538fa5d63569a203376aa1ac71f0510a  out/stage45/stage45.bin
d3e29fb1ea443d39cde27383c8efd476d43a956ab7a63f036640a56f2537113a  out/stage45/stage45.img
bbc90109cd6c8cbcb035bc99f4b8e9f39b9f4740ba58a549205aae3e9716572a  out/stage45/stage45-qcdt.img
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=149564 (0x2483c)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage45 mi4ios6=stage45 c-runtime apple-dt macho-fixture load-plan materialize xnu-preflight
part=kernel offset=0x800 size=149564 sha256=09c9885d787c3a3544086c29fc000815538fa5d63569a203376aa1ac71f0510a
part=dt.img offset=0x25800 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Important symbols:

```text
000080a0 T stage45_vectors
0000bff0 T stage45_loader_preflight_run
0001b7cc T kernel_entry
0001bb14 T test_kernel_entry
0001bcac T stage45_main
0001d530 T stage45_embedded_macho_size
0001d534 T stage45_embedded_macho
00036000 b stage45_loader_preflight_block
00037000 b stage45_macho_staging_arena
00052000 B __stage45_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage45/stage45-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2612 KB)                       OKAY [  0.083s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.094s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage45-last_kmsg.txt
```

The recovered log was 103763 bytes and contained:

```text
1566 MI4IOS6_STAGE45 markers
1540 MI4IOS6_STAGE45_XNU markers
```

## Key recovered markers

Inherited high-root/MMU success remains intact:

```text
MI4IOS6_STAGE45_XNU high_root_status=0x45000001
MI4IOS6_STAGE45_XNU mmu high bootstrap selftest ok
```

Post-root IRQ retests still pass:

```text
MI4IOS6_STAGE45_XNU gic SGI selftest ok
MI4IOS6_STAGE45_XNU gic timer selftest ok
```

Mach-O materialization markers:

```text
MI4IOS6_STAGE45_XNU Stage45 Mach-O/XNU loader preflight begin
MI4IOS6_STAGE45_XNU Stage45 proposed physical load plan is dry-run only; no physical writes performed
MI4IOS6_STAGE45_XNU Stage45 Mach-O materialization begin
MI4IOS6_STAGE45_XNU Stage45 materialized Mach-O reparse ok
MI4IOS6_STAGE45_XNU macho_staging_entry_count=0x00000006
MI4IOS6_STAGE45_XNU macho_staging_file_bytes=0x000004e0
MI4IOS6_STAGE45_XNU macho_staging_zero_bytes=0x00008b20
MI4IOS6_STAGE45_XNU macho_staging_marker_mask=0x00000007
MI4IOS6_STAGE45_XNU macho_staging_zero_mask=0x0000003f
MI4IOS6_STAGE45_XNU macho_staging_status=0x45000001
MI4IOS6_STAGE45_XNU loader_materialized_status=0x45000001
MI4IOS6_STAGE45_XNU loader_safety_mask=0x0000007f
MI4IOS6_STAGE45_XNU loader_satisfied_mask=0x000001ff
MI4IOS6_STAGE45_XNU loader_status=0x45000001
MI4IOS6_STAGE45_XNU Stage45 XNU handoff disabled: materialized only into local BSS arena; no Mach-O entry executed, no physical writes, no TTBR switch, caches unchanged
MI4IOS6_STAGE45_XNU Stage45 Mach-O/XNU loader preflight ok
```

Final success markers:

```text
MI4IOS6_STAGE45_XNU kernel_entry ok
MI4IOS6_STAGE45 kernel_entry returned success
MI4IOS6_STAGE45 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage45 is still not a real XNU boot, but it is a concrete loader step beyond Stage44. Stage44 only computed the proposed segment load plan; Stage45 executes the copy+zero-fill rule on the owned device and validates the result by reparsing the materialized image and checking marker/zero-fill bytes. The materialization target is deliberately a local BSS arena, not the proposed physical XNU range, so Stage45 proves loader mechanics while preserving recovery and non-execution safety.

This makes the future handoff contract more concrete: the project now has hardware-proven logs showing that a generated Mach-O fixture can be copied into a loaded-memory-shaped image, zero-filled, reparsed, and reconciled with a public-XNU-style max loaded address while inherited MMU/GIC/timer safety gates remain intact.

The explicit blockers remain real pmap bootstrap, a real MSM8974 pexpert implementation, XNU interrupt-controller and timer hooks, IOKit/platform drivers, real kernelcache/loading details, relocation/linking/prelink details, and later code-signing/userspace questions.

## Success criteria — met

1. Stage45 directory and build outputs created: yes
2. Fixture generator parameterized for Stage45: yes
3. Stage45 fixture layout reparses from local arena base: yes
4. Local staging arena added and aligned: yes
5. Materialization destination is Stage45-owned BSS only: yes
6. Proposed physical load addresses remain dry-run metadata only: yes
7. Bootloader accepted `stage45-qcdt.img`: yes
8. Stage44-derived Apple-DT/PE_state/GIC/timebase/MMU checks still passed: yes
9. SGI IRQ retest passed after high-root/MMU path: yes
10. Timer IRQ retest passed after high-root/MMU path: yes
11. Mach-O parser accepted the generated fixture before materialization: yes
12. Six segment entries were staged: yes
13. File-backed bytes were copied into the local arena: yes
14. Zero-fill tails were cleared and scanned: yes
15. Materialized arena image reparsed successfully: yes
16. `getlastaddr`-style value was recorded as `0x80011000`: yes
17. Required marker prefixes were verified: yes
18. Materialization status returned `0x45000001`: yes
19. Loader safety mask included local-arena-only/no-physical-write bits: yes
20. Loader preflight returned status `0x45000001`: yes
21. `kernel_entry` returned success: yes
22. payload reset through PS_HOLD: yes

## Next stage

Completed by Stage46. Stage46 modeled the early ARM XNU `_start` page-table/TTE workspace from Stage45's materialized image and proposed `topOfKernelData`, recorded a 10-page workspace layout, reported loaded-kernel/RAM/ram_console/GIC section indices, and returned TTE dry-run status `0x46000001` while still not programming TTBRs, enabling caches, executing XNU, or writing proposed physical addresses.

Stage47 should continue toward a real XNU handoff without executing XNU yet by populating and verifying more explicit local simulated section descriptors and adding dry-run translation checks over the local table model.
