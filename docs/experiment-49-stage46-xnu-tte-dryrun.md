# Experiment 49 — Stage46 XNU TTE Workspace Dry-Run

Date: 2026-06-06

Goal: move one step closer to a real XNU handoff by modeling the early ARM XNU `_start` translation-table workspace derived from Stage45's materialized Mach-O and proposed `topOfKernelData`, while keeping every table byte local and non-authoritative.

Stage46 still does **not** run XNU or iOS. It does not branch to parsed Mach-O entry metadata, does not jump to XNU `_start` / `arm_init`, does not write the proposed XNU physical load or TTE workspace addresses, does not program TTBR0/TTBR1, does not replace the live Stage46 MMU tables, and does not enable caches. It preserves the Stage45 safety gates and adds a TTE workspace dry-run descriptor only.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- No parsed Mach-O entrypoint execution.
- No real XNU `_start` / `arm_init` handoff.
- No writes to proposed XNU physical load addresses.
- No writes to the proposed XNU TTE workspace physical addresses.
- No TTBR0/TTBR1 writes.
- No replacement of the live Stage46 MMU tables.
- Caches remain disabled/unchanged.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun before the Stage46 loader preflight.
- Mach-O materialization remains local BSS-only.
- TTE workspace modeling writes only to `stage46_tte_dryrun_arena[]`, a Stage46-owned local BSS simulation buffer.

## What Stage46 adds over Stage45

Stage45 proved local Mach-O copy+zero-fill materialization, materialized-image reparse, marker checks, and zero-fill scanning.

Stage46 adds:

- a new standalone `stage46/` payload copied from Stage45,
- Stage46 status/log prefixes (`0x46000001`, `MI4IOS6_STAGE46`),
- a command-line marker:
  - `tte-dryrun`,
- a Stage46 TTE dry-run descriptor:
  - `struct stage46_xnu_tte_dryrun`,
- a local TTE simulation arena:
  - `stage46_tte_dryrun_arena[0x0000a000]`,
  - aligned to 16 KiB,
- ARMv7 short-descriptor workspace accounting:
  - 16 KiB L1 table reservation,
  - 4 KiB page-granular L2/coarse-table reservation,
  - remaining scratch reservation,
- section-index reporting for:
  - loaded kernel/Mach-O physical range,
  - low RAM range,
  - `ram_console`,
  - GIC/timer MMIO range,
- local simulated L1 section descriptors written only to the local dry-run arena,
- explicit safety/readiness bits proving no TTBR writes, no cache changes, and local simulation only.

## Inherited Mach-O materialization facts

Stage46 preserves Stage45's local materialization behavior. The recovered hardware log shows:

```text
macho_staging_arena_base=0x00046000
macho_staging_arena_end=0x00056000
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
macho_staging_status=0x46000001
loader_materialized_status=0x46000001
loader_materialized_file_bytes=0x000004e0
loader_materialized_zero_bytes=0x00008b20
```

Interpretation:

- six segment entries were materialized into local BSS,
- `0x4e0` file-backed bytes were copied,
- `0x8b20` bytes were zero-filled,
- marker mask `0x7` means required `ST46-TEXT`, `ST46-DATA`, and `ST46-PRELINK-TEXT` marker prefixes were observed,
- zero mask `0x3f` means all six zero-fill tails passed scanning,
- the arena reparse matched the original Mach-O structure,
- materialization status returned `0x46000001`.

## Proposed XNU load/workspace facts

Stage46 preserves Stage45's proposed physical load and TTE workspace placement:

```text
xnu_proposed_loaded_phys_base=0x80000000
xnu_proposed_loaded_phys_end=0x80009000
xnu_proposed_loaded_file_end=0x000004e0
xnu_proposed_topOfKernelData=0x8000c000
xnu_proposed_ttep_workspace_limit=0x80016000
xnu_proposed_avail_start=0x80016000
```

This follows the public ARM XNU interpretation used since Stage43:

```text
topOfKernelData = align_up(load_phys_end, 16 KiB)
TTE workspace   = 10 pages = 0x0000a000
avail_start     = topOfKernelData + TTE workspace
```

Stage46 does not write this proposed physical workspace. It models the layout in a local BSS arena only.

## TTE dry-run descriptor facts

Recovered TTE dry-run facts:

```text
xnu_tte_dryrun_status=0x46000001
xnu_tte_workspace_base=0x8000c000
xnu_tte_workspace_size=0x0000a000
xnu_tte_workspace_limit=0x80016000
xnu_tte_local_arena_base=0x0003c000
xnu_tte_l1_table_base=0x8000c000
xnu_tte_l1_table_size=0x00004000
xnu_tte_l2_table_base=0x80010000
xnu_tte_l2_table_size=0x00001000
xnu_tte_scratch_base=0x80011000
xnu_tte_scratch_size=0x00005000
xnu_tte_kernel_l1_first_index=0x00000800
xnu_tte_kernel_l1_last_index=0x00000800
xnu_tte_kernel_l1_section_count=0x00000001
xnu_tte_lowmem_l1_first_index=0x00000800
xnu_tte_lowmem_l1_last_index=0x00000de4
xnu_tte_ram_console_l1_index=0x00000de5
xnu_tte_gic_l1_first_index=0x00000f90
xnu_tte_gic_l1_last_index=0x00000f90
xnu_tte_ttbr0_written=0x00000000
xnu_tte_ttbr1_written=0x00000000
xnu_tte_caches_enabled=0x00000000
xnu_tte_local_sim_zeroed=0x00000001
xnu_tte_satisfied_mask=0x000003ff
xnu_tte_failure_mask=0x00000000
xnu_tte_checksum=0x5e5692d0
```

Interpretation:

- The proposed workspace begins at `0x8000c000` and ends at `0x80016000`.
- The first 16 KiB is reserved as an ARMv7 short-descriptor L1 table.
- One 4 KiB page is reserved as the first L2/coarse-table page-granular area.
- The remaining 20 KiB is scratch/reserved early workspace.
- The loaded kernel/Mach-O proposed physical range occupies L1 section index `0x800`.
- Low RAM coverage runs from section index `0x800` through `0xde4`.
- `ram_console` begins at section index `0xde5`.
- GIC/timer MMIO coverage includes section index `0xf90`.
- TTBR0/TTBR1 were not written.
- Caches were not enabled.
- Local simulation-only accounting passed.
- TTE dry-run status returned `0x46000001`.

## Loader-preflight facts

Selected public source baselines remain:

```text
loader_xnu_baseline_tag=0x20502213       # xnu-2050.22.13
loader_xnu_baseline_commit=0xcc8a9b0c    # cc8a9b0c...
loader_xnu_master_version=0x000c0300     # 12.3.0
loader_xnu_arm_reference=0x45700146      # xnu-4570.1.46 ARM reference
```

Stage46 loader readiness facts:

```text
loader_tte_dryrun_status=0x46000001
loader_tte_dryrun_satisfied_mask=0x000003ff
loader_tte_dryrun_failure_mask=0x00000000
loader_tte_dryrun_checksum=0x5e5692d0
loader_safety_mask=0x000003ff
loader_satisfied_mask=0x000003ff
loader_checksum=0x4f1a06cb
loader_status=0x46000001
```

The safety mask now includes Stage45's safety bits plus explicit TTE-dry-run-only, no-TTBR-write, and no-cache-change bits.

## Built image

```bash
/mnt/data/mi4-ios6/stage46/build.sh
```

Successful local build:

```text
out/stage46/stage46-qcdt.img
sha256=2ffcab54399a33dbde62cb07b7216484e7144db98e8ddfbde3c1543ab35ee3c0
```

Build hashes:

```text
df4fdcb16ae079bf6129654cc4b183637dbaa8d91db459150f69bf4ba509b202  out/stage46/stage46_fixture.macho
b2102490a7f7b11df72cdb2a28eaa66e291f10f9583d5bb99cf9d3fe33a847fd  out/stage46/stage46.elf
5528d52be572d236f3e99ab7073c1040aded6cdeea6725c6ae9c83a8f4df16b3  out/stage46/stage46.bin
b86aca30a4fba86c5491d9d11784b64626465a2133067759b7212b5a04f37495  out/stage46/stage46.img
2ffcab54399a33dbde62cb07b7216484e7144db98e8ddfbde3c1543ab35ee3c0  out/stage46/stage46-qcdt.img
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=152392 (0x25348)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage46 mi4ios6=stage46 c-runtime apple-dt macho-fixture load-plan materialize tte-dryrun xnu-preflight
part=kernel offset=0x800 size=152392 sha256=5528d52be572d236f3e99ab7073c1040aded6cdeea6725c6ae9c83a8f4df16b3
part=dt.img offset=0x26000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Important symbols:

```text
000080a0 T stage46_vectors
0000bff0 T stage46_loader_preflight_run
0001bf40 T kernel_entry
0001c288 T test_kernel_entry
0001c420 T stage46_main
0001dcb0 T stage46_embedded_macho_size
0001dcb4 T stage46_embedded_macho
00038000 b stage46_loader_preflight_block
0003c000 b stage46_tte_dryrun_arena
00046000 b stage46_macho_staging_arena
00062000 B __stage46_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage46/stage46-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2614 KB)                       OKAY [  0.083s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.093s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage46-last_kmsg.txt
```

The recovered log was 105540 bytes and contained:

```text
1597 MI4IOS6_STAGE46 markers
1571 MI4IOS6_STAGE46_XNU markers
```

## Key recovered markers

Inherited high-root/MMU success remains intact:

```text
MI4IOS6_STAGE46_XNU high_root_status=0x46000001
MI4IOS6_STAGE46_XNU mmu high bootstrap selftest ok
```

Post-root IRQ retests still pass:

```text
MI4IOS6_STAGE46_XNU gic SGI selftest ok
MI4IOS6_STAGE46_XNU gic timer selftest ok
```

Stage46 loader/TTE markers:

```text
MI4IOS6_STAGE46_XNU Stage46 Mach-O/XNU loader preflight begin
MI4IOS6_STAGE46_XNU Stage46 Mach-O materialization begin
MI4IOS6_STAGE46_XNU Stage46 materialized Mach-O reparse ok
MI4IOS6_STAGE46_XNU macho_staging_status=0x46000001
MI4IOS6_STAGE46_XNU Stage46 XNU TTE dry-run ok
MI4IOS6_STAGE46_XNU xnu_tte_dryrun_status=0x46000001
MI4IOS6_STAGE46_XNU xnu_tte_workspace_base=0x8000c000
MI4IOS6_STAGE46_XNU xnu_tte_workspace_limit=0x80016000
MI4IOS6_STAGE46_XNU xnu_tte_l1_table_base=0x8000c000
MI4IOS6_STAGE46_XNU xnu_tte_l2_table_base=0x80010000
MI4IOS6_STAGE46_XNU xnu_tte_satisfied_mask=0x000003ff
MI4IOS6_STAGE46_XNU xnu_tte_failure_mask=0x00000000
MI4IOS6_STAGE46_XNU loader_safety_mask=0x000003ff
MI4IOS6_STAGE46_XNU loader_satisfied_mask=0x000003ff
MI4IOS6_STAGE46_XNU loader_status=0x46000001
MI4IOS6_STAGE46_XNU Stage46 XNU handoff disabled: materialized only into local BSS arena; TTE workspace modeled only in local BSS; no Mach-O entry executed, no physical writes, no TTBR writes, caches unchanged
MI4IOS6_STAGE46_XNU Stage46 Mach-O/XNU loader preflight ok
```

Final success markers:

```text
MI4IOS6_STAGE46_XNU kernel_entry ok
MI4IOS6_STAGE46 kernel_entry returned success
MI4IOS6_STAGE46 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage46 is still not a real XNU boot, but it models the next critical precondition for one: the early translation-table workspace that public ARM XNU derives from `boot_args.topOfKernelData`. Stage45 proved the loader can produce a materialized image shape. Stage46 proves the derived `topOfKernelData` workspace can be split into a conservative ARMv7 short-descriptor L1/L2/scratch layout and can account for the loaded kernel range, RAM, ram_console, and GIC/timer MMIO coverage without touching live TTBRs.

This makes the future handoff contract more concrete: the project now has hardware-proven logs showing a local materialized Mach-O image, proposed physical load range, derived `topOfKernelData`, and dry-run TTE workspace can all agree while inherited MMU/GIC/timer safety gates remain intact.

The explicit blockers remain real pmap bootstrap, real TTE/L2 descriptor population for the exact XNU mapping policy, MSM8974 pexpert support, XNU interrupt-controller and timer hooks, IOKit/platform drivers, real kernelcache/loading details, relocation/linking/prelink details, and later code-signing/userspace questions.

## Success criteria — met

1. Stage46 directory and build outputs created: yes
2. Stage46 command line includes `tte-dryrun`: yes
3. Stage45 materialization behavior preserved: yes
4. Local BSS-only TTE simulation arena added: yes
5. Proposed physical TTE workspace remained dry-run metadata only: yes
6. Bootloader accepted `stage46-qcdt.img`: yes
7. Stage45-derived Apple-DT/PE_state/GIC/timebase/MMU checks still passed: yes
8. SGI IRQ retest passed after high-root/MMU path: yes
9. Timer IRQ retest passed after high-root/MMU path: yes
10. Mach-O parser/materialization returned status `0x46000001`: yes
11. TTE workspace base was derived as `0x8000c000`: yes
12. TTE workspace limit was derived as `0x80016000`: yes
13. L1 table reservation was recorded as 16 KiB: yes
14. L2/coarse-table page reservation was recorded: yes
15. Scratch reservation was recorded: yes
16. Kernel/RAM/ram_console/GIC section indices were recorded: yes
17. TTBR0/TTBR1 writes remained zero: yes
18. caches-enabled flag remained zero: yes
19. TTE dry-run satisfied mask was complete: yes
20. TTE dry-run returned status `0x46000001`: yes
21. Loader safety mask included TTE dry-run-only/no-TTBR/no-cache-change bits: yes
22. Loader preflight returned status `0x46000001`: yes
23. `kernel_entry` returned success: yes
24. payload reset through PS_HOLD: yes

## Next stage

Completed by Stage47. Stage47 populated and read back local simulated L1 section descriptors for loaded kernel, low RAM, ram_console, and GIC/timer MMIO ranges; verified descriptor type and attribute bits; added a software-only identity L1 section virt-to-phys checker; and recorded public-XNU `boot_ttep`/`avail_start` policy facts. It returned `xnu_tte_dryrun_status=0x47000001`, `xnu_tte_descriptor_verify_mask=0x0000007f`, `xnu_tte_translation_check_mask=0x0000001f`, and loader status `0x47000001` while still not executing XNU, writing proposed physical addresses, programming TTBRs, replacing live MMU tables, or changing caches.

Stage48 should continue toward a real XNU handoff without executing XNU yet by materializing verified table bytes into a Stage-owned safe table buffer and preparing a controlled no-XNU TTBR-switch selftest plan, while preserving non-persistent booting, ram_console diagnostics, PS_HOLD reset, recovery mappings, caches unchanged, and SGI/timer retests.
