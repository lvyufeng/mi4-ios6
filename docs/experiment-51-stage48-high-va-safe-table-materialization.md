# Experiment 51 — Stage48 High-VA Safe Table Materialization

Date: 2026-06-06

Goal: move one accelerated step closer to a real XNU handoff by separating high-virtual L1 slot selection from physical section descriptor bases, then materializing verified identity/recovery and high-VA table bytes into a Stage48-owned safe table buffer. Stage48 prepares the next no-XNU TTBR-switch experiment, but still does not install these tables or execute XNU.

Stage48 still does **not** run XNU or iOS. It does not branch to parsed Mach-O entry metadata, does not jump to XNU `_start` / `arm_init`, does not write the proposed XNU physical load or TTE workspace addresses, does not program TTBR0/TTBR1, does not replace the live Stage48 MMU tables, and does not enable or change caches. All new high-VA and safe-table bytes are written only to Stage48-owned local BSS.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- No parsed Mach-O entrypoint execution.
- No generated Mach-O fixture execution.
- No real XNU `_start` / `arm_init` handoff.
- No writes to proposed XNU physical load addresses.
- No writes to proposed XNU TTE workspace physical addresses.
- No TTBR0/TTBR1 writes from the proposed XNU boot tuple.
- No TTBCR/DACR/SCTLR writes for the proposed XNU table.
- No TLB invalidation for installing a proposed XNU table.
- No replacement of the live Stage48 MMU tables.
- Caches remain disabled/unchanged.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun before the Stage48 loader preflight.
- Mach-O materialization remains local BSS-only.
- TTE workspace modeling still uses `stage48_tte_dryrun_arena[]`.
- New safe-table materialization uses only `stage48_safe_table_arena[]`.

## What Stage48 adds over Stage47

Stage47 verified identity-style local L1 section descriptors and walked them through a software-only VTOP checker. Stage48 adds:

- a new standalone `stage48/` payload copied from Stage47,
- Stage48 status/log prefixes (`0x48000001`, `MI4IOS6_STAGE48`),
- command-line markers for:
  - `highva-dryrun`,
  - `safe-table`,
  - `safe-table-materialize`,
  - `stage-owned-tables`,
  - `no-ttbr-write`, `no-cache-change`, `no-xnu-jump`,
- a PA-base-aware descriptor helper:

  ```text
  l1[va >> 20] = (pa & 0xfff00000) | 0x00010c02
  ```

- a Stage48-owned safe-table arena:

  ```text
  stage48_safe_table_arena[0x8000]
  ```

  split into two 16 KiB local L1 tables:
  - identity/recovery L1 table,
  - high-VA L1 table,
- high-VA descriptor readback checks folded into the descriptor mask,
- high-VA software VTOP checks folded into the translation mask,
- explicit safety-proof fields for no proposed physical writes, no TTBR/control-register writes, no live-table replacement, and caches unchanged.

## Stage48 implementation note

The generated fixture still begins at a proposed Mach-O `virtBase` around `0x80008000`, while the proposed physical load base is `0x80000000`. ARMv7 section descriptors are 1 MiB-granular, so Stage48 models the high-VA relationship at section-envelope granularity: it records the proposed `virtBase` separately, then materializes a high-VA L1 section envelope covering `0x80000000` through `0x80011000` with descriptor base `0x80000000`.

This is intentional and conservative. Stage48 proves that the implementation no longer depends on the identity helper `l1[index] = (index << 20) | flags`; the new helper takes an explicit VA slot and an explicit PA section base. It does **not** claim that the final sub-section `virtBase=0x80008000` to `physBase=0x80000000` delta has been solved. Exact sub-section placement will require L2/page-granular descriptors or a linker/load-layout adjustment in a later stage.

The embedded `boot_args.CommandLine` and Apple-DT `/chosen/boot-args` strings were kept under the public 256-byte boot-args command-line field. The boot image command line still carries the longer host-visible Stage48 marker set.

## Inherited load and workspace facts

Stage48 preserves the Stage47 materialized Mach-O and proposed workspace flow:

```text
xnu_proposed_physBase=0x80000000
xnu_proposed_loaded_phys_base=0x80000000
xnu_proposed_loaded_phys_end=0x80009000
xnu_proposed_loaded_file_end=0x000004e0
xnu_proposed_topOfKernelData=0x8000c000
xnu_proposed_ttep_workspace_limit=0x80016000
xnu_proposed_avail_start=0x80016000
```

The inherited local TTE workspace still models:

```text
xnu_tte_workspace_base=0x8000c000
xnu_tte_workspace_size=0x0000a000
xnu_tte_workspace_limit=0x80016000
xnu_tte_l1_table_size=0x00004000
xnu_tte_l2_table_size=0x00001000
xnu_tte_scratch_size=0x00005000
```

Stage48 still does not write the proposed physical workspace at `0x8000c000`. The safe table is local BSS only.

## Descriptor and translation facts

Stage48 keeps the same conservative ARMv7 short-descriptor section attributes:

```text
STAGE48_XNU_TTE_DESC_SECTION_SO = 0x00010c02
```

Inherited Stage47 descriptor/VTOP masks are extended with two high-VA descriptor checks and two high-VA translation checks:

```text
xnu_tte_descriptor_verify_mask=0x000001ff
xnu_tte_descriptor_failure_mask=0x00000000
xnu_tte_translation_check_mask=0x0000007f
xnu_tte_translation_failure_mask=0x00000000
xnu_tte_satisfied_mask=0x001fffff
xnu_tte_failure_mask=0x00000000
```

The high-VA section-envelope facts recovered from hardware are:

```text
xnu_highva_dryrun_status=0x48000001
xnu_highva_virt_base=0x80000000
xnu_highva_virt_end=0x80011000
xnu_highva_phys_base=0x80000000
xnu_highva_phys_end=0x80011000
xnu_highva_virt_phys_delta=0x00000000
xnu_highva_l1_first_index=0x00000800
xnu_highva_l1_last_index=0x00000800
xnu_highva_l1_section_count=0x00000001
xnu_highva_descriptor_first_word=0x80010c02
xnu_highva_descriptor_last_word=0x80010c02
xnu_highva_translation_first_va=0x80000000
xnu_highva_translation_first_pa=0x80000000
xnu_highva_translation_last_va=0x80010fff
xnu_highva_translation_last_pa=0x80010fff
```

## Safe-table materialization facts

Stage48 materializes two local L1 tables into `stage48_safe_table_arena[]`:

```text
xnu_safe_table_status=0x48000001
xnu_safe_table_kind_mask=0x00000003
xnu_safe_table_local_base=0x0003c000
xnu_safe_table_local_limit=0x00044000
xnu_safe_table_written_bytes=0x00008000
xnu_safe_table_materialized_mask=0x00000003
xnu_safe_table_failure_mask=0x00000000
xnu_safe_table_checksum=0x79100000
```

Important symbols show the local arenas are disjoint:

```text
00038000 b stage48_loader_preflight_block
0003c000 b stage48_safe_table_arena
00044000 b stage48_tte_dryrun_arena
0004e000 b stage48_macho_staging_arena
0006a000 B __stage48_image_end
```

Interpretation:

- safe-table local range is `0x0003c000`-`0x00044000`,
- TTE dry-run workspace is `0x00044000`-`0x0004e000`,
- Mach-O staging arena starts at `0x0004e000`,
- the proposed XNU physical load/workspace ranges remain metadata only.

## Safety proof markers

Recovered hardware log safety-proof values:

```text
xnu_tte_proposed_phys_load_written=0x00000000
xnu_tte_proposed_workspace_written=0x00000000
xnu_tte_live_mmu_tables_replaced=0x00000000
xnu_tte_ttbcr_written=0x00000000
xnu_tte_dacr_written=0x00000000
xnu_tte_tlbs_invalidated=0x00000000
xnu_tte_sctlr_written=0x00000000
xnu_tte_stage_owned_tables_only=0x00000001
xnu_tte_ttbr0_written=0x00000000
xnu_tte_ttbr1_written=0x00000000
xnu_tte_caches_enabled=0x00000000
```

Loader roll-up:

```text
loader_highva_dryrun_status=0x48000001
loader_safe_table_status=0x48000001
loader_stage_owned_tables_status=0x48000001
loader_safety_mask=0x00007fff
loader_satisfied_mask=0x00007fff
loader_status=0x48000001
```

## Built image

```bash
/mnt/data/mi4-ios6/stage48/build.sh
```

Successful local build:

```text
out/stage48/stage48-qcdt.img
sha256=5d92ba859c4ee59e3af1362d66c072ee3b0400f45adbcc7b95ac658fb23f289a
```

Build hashes:

```text
cace3ca2ccb45f22553968071e7b3706a302f298bb5e3e5c4030a33cab48dddc  out/stage48/stage48_fixture.macho
abb120d140891ceb21f44e1ba2347539e0b870d38ea84bfae1eb9d87d91c78de  out/stage48/stage48.elf
cef21a166d5cacd8a012ba0b1f777bc2cfbe62773632739172191b8e61047cae  out/stage48/stage48.bin
dad991f33a54b7efb27179988d8495253c8f1aff616b589229abbbf41273bc0e  out/stage48/stage48.img
5d92ba859c4ee59e3af1362d66c072ee3b0400f45adbcc7b95ac658fb23f289a  out/stage48/stage48-qcdt.img
```

Size:

```text
text=158596 data=0 bss=233792 dec=392388 hex=5fcc4
```

Boot image parse highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=158596 (0x26b84)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage48 mi4ios6=stage48 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table safe-table-materialize stage-owned-tables tte-dryrun tte-verify vtop-dryrun xnu-preflight no-ttbr-write no-cache-change no-xnu-jump
part=kernel offset=0x800 size=158596 sha256=cef21a166d5cacd8a012ba0b1f777bc2cfbe62773632739172191b8e61047cae
part=dt.img offset=0x27800 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage48/stage48-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2620 KB)                       OKAY [  0.083s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.094s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage48-last_kmsg.txt
```

The recovered log was 110092 bytes and contained:

```text
1669 MI4IOS6_STAGE48 markers
1643 MI4IOS6_STAGE48_XNU markers
```

## Key recovered markers

Inherited high-root/MMU success remains intact:

```text
MI4IOS6_STAGE48_XNU high_root_status=0x48000001
MI4IOS6_STAGE48_XNU mmu high bootstrap selftest ok
```

Post-root IRQ retests still pass:

```text
MI4IOS6_STAGE48_XNU gic SGI selftest ok
MI4IOS6_STAGE48_XNU gic timer selftest ok
```

Stage48 loader/high-VA/safe-table markers:

```text
MI4IOS6_STAGE48_XNU macho_staging_status=0x48000001
MI4IOS6_STAGE48_XNU xnu_tte_dryrun_status=0x48000001
MI4IOS6_STAGE48_XNU xnu_tte_descriptor_verify_mask=0x000001ff
MI4IOS6_STAGE48_XNU xnu_tte_descriptor_failure_mask=0x00000000
MI4IOS6_STAGE48_XNU xnu_tte_translation_check_mask=0x0000007f
MI4IOS6_STAGE48_XNU xnu_tte_translation_failure_mask=0x00000000
MI4IOS6_STAGE48_XNU xnu_highva_dryrun_status=0x48000001
MI4IOS6_STAGE48_XNU xnu_safe_table_status=0x48000001
MI4IOS6_STAGE48_XNU xnu_tte_satisfied_mask=0x001fffff
MI4IOS6_STAGE48_XNU xnu_tte_failure_mask=0x00000000
MI4IOS6_STAGE48_XNU loader_highva_dryrun_status=0x48000001
MI4IOS6_STAGE48_XNU loader_safe_table_status=0x48000001
MI4IOS6_STAGE48_XNU loader_stage_owned_tables_status=0x48000001
MI4IOS6_STAGE48_XNU loader_safety_mask=0x00007fff
MI4IOS6_STAGE48_XNU loader_satisfied_mask=0x00007fff
MI4IOS6_STAGE48_XNU loader_status=0x48000001
MI4IOS6_STAGE48_XNU Stage48 Mach-O/XNU loader preflight ok
```

Final success markers:

```text
MI4IOS6_STAGE48_XNU kernel_entry ok
MI4IOS6_STAGE48 kernel_entry returned success
MI4IOS6_STAGE48 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage48 is still not a real XNU boot, but it removes another pre-handoff uncertainty. Stage47 proved local descriptor bytes could be verified and walked. Stage48 proves those bytes can be copied into a separate Stage-owned safe table buffer, that a high-VA table can be built with explicit VA-slot/PA-base descriptor composition, and that loader preflight can fail closed unless high-VA, safe-table, and local-only safety masks are complete.

The major remaining blockers are still real sub-section/page-granular XNU mapping policy, controlled live table switching, real pmap/bootstrap table population, exact XNU cache policy, MSM8974 pexpert support, XNU interrupt/timer hooks, IOKit/platform drivers, real kernelcache/loading details, relocation/linking/prelink details, and later code-signing/userspace policy work.

## Success criteria — met

1. Stage48 directory and build outputs created: yes
2. Stage48 command line includes high-VA and safe-table markers: yes
3. Stage47 descriptor/VTOP behavior preserved: yes
4. PA-base-aware high-VA descriptor helpers added: yes
5. Stage-owned safe table arena added: yes
6. Identity/recovery L1 bytes materialized into safe table: yes
7. High-VA L1 bytes materialized into safe table: yes
8. High-VA descriptor readback bits passed: yes
9. High-VA software VTOP bits passed: yes
10. Safe-table non-overlap/local-only checks passed: yes
11. Proposed physical load writes remained zero: yes
12. Proposed physical workspace writes remained zero: yes
13. Live MMU table replacement remained zero: yes
14. TTBR0/TTBR1 writes remained zero: yes
15. TTBCR/DACR/SCTLR writes remained zero: yes
16. TLB invalidation for table install remained zero: yes
17. caches-enabled flag remained zero: yes
18. Bootloader accepted `stage48-qcdt.img`: yes
19. Apple-DT/PE_state/GIC/timebase/MMU checks still passed: yes
20. SGI IRQ retest passed after high-root/MMU path: yes
21. Timer IRQ retest passed after high-root/MMU path: yes
22. TTE dry-run status returned `0x48000001`: yes
23. Loader safety/satisfied masks reached `0x00007fff`: yes
24. Loader preflight returned status `0x48000001`: yes
25. `kernel_entry` returned success: yes
26. payload reset through PS_HOLD: yes

## Next stage

Stage49 should attempt a controlled **no-XNU** TTBR-switch selftest using Stage-owned tables only. It should not jump to XNU yet. The table must preserve recovery identity mappings, ram_console, PS_HOLD, GIC, timer, abort logging, and an immediate success/failure reporting path. If Stage49 switches TTBRs, it should use the Stage-owned safe-table concept from Stage48 rather than the proposed XNU physical workspace, and it should still keep caches unchanged unless a separate cache-policy stage explicitly validates otherwise.
