# Experiment 52 — Stage49 Controlled TTBR0 Round-Trip

Date: 2026-06-06

Goal: close the next accelerated pre-XNU handoff gap by proving that the Mi 4 can run a controlled **no-XNU** live TTBR0 switch using only a Stage49-owned recovery L1 table, then restore the original live MMU state. Stage49 intentionally writes TTBR0 and invalidates TLBs for this bounded round-trip, but it does not install the proposed XNU table, does not use the proposed XNU TTE workspace, does not execute the generated Mach-O fixture, and does not jump into XNU.

Stage49 still does **not** run XNU or iOS. It does not branch to parsed Mach-O entry metadata, does not jump to XNU `_start` / `arm_init`, does not execute any public-XNU object code, does not write proposed XNU physical load or TTE workspace addresses, does not use the proposed XNU TTE workspace as a live TTBR table, does not write persistent storage, and does not enable or change caches.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- No parsed Mach-O entrypoint execution.
- No generated Mach-O fixture execution.
- No real XNU `_start` / `arm_init` handoff.
- No public-XNU object execution.
- No writes to proposed XNU physical load addresses.
- No writes to proposed XNU TTE workspace physical addresses.
- No use of the proposed XNU TTE workspace as a live TTBR table.
- Controlled Stage49-owned TTBR0 writes are allowed only for the no-XNU round-trip.
- TLB invalidation is allowed only around that Stage49-owned TTBR0 round-trip.
- No normal-path writes to TTBCR, DACR, or SCTLR.
- Original TTBR0/TTBCR/DACR/SCTLR state must be restored after the selftest.
- Caches remain disabled/unchanged.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are run before the TTBR0 round-trip.
- Mach-O materialization remains local BSS-only.
- TTE workspace modeling still uses the local Stage-owned dry-run arena.
- Stage48's safe-table concept is preserved, but Stage49's live switch uses a separate Stage49-owned recovery L1 table.

The Stage49 completion message is intentionally explicit:

```text
Stage49 XNU handoff disabled: controlled TTBR0 round-trip used only Stage49-owned recovery table; original TTBR/control state restored; no Mach-O entry executed, no XNU jump, no proposed physical/workspace writes, no persistent writes, caches unchanged
```

## What Stage49 adds over Stage48

Stage48 proved Stage-owned local table bytes could represent identity/recovery and high-VA ARMv7 section mappings, while still not installing them. Stage49 adds the first bounded live table-control experiment:

- a new standalone `stage49/` payload copied from Stage48,
- Stage49 status/log prefixes (`0x49000001`, `MI4IOS6_STAGE49`),
- command-line markers for:
  - `ttbr0-roundtrip`,
  - `recovery-table`,
  - `stage-owned-tables`,
  - `cache-bits-preserved`,
  - `no-xnu-jump`, `no-macho-exec`, `no-proposed-phys-write`, `no-proposed-tte-write`, `no-persist-write`,
- a dedicated Stage49 TTBR round-trip ABI/result block,
- a dedicated Stage49-owned 16 KiB L1 recovery table,
- live TTBR0 switch to that Stage49-owned L1 table,
- TLB invalidate after switching and after restore,
- immediate restore of the original live TTBR0,
- readback of TTBR0/TTBCR/DACR/SCTLR before/during/after,
- proof that SCTLR cache bits remain unchanged before/during/after,
- proof that no XNU/Mach-O/proposed physical/proposed workspace/persistent writes occurred.

The inherited Stage48 local-only loader/TTE flow remains in place. Stage49 still validates the generated non-proprietary Mach-O fixture, local copy/zero-fill materialization, local ARM XNU TTE dry-run, PA-base-aware high-VA descriptor model, and Stage-owned safe-table materialization before declaring the loader preflight successful.

## Recovery L1 table policy

Stage49 uses the same conservative ARMv7 short-descriptor section word policy as Stage48:

```text
l1[va >> 20] = (pa & 0xfff00000) | 0x00010c02
```

The Stage49 round-trip table is separate from the inherited loader/TTE arenas:

```text
stage49_ttbr0_roundtrip_l1[4096] aligned to 16 KiB
```

It is not the proposed XNU physical TTE workspace. It lives in local Stage49 BSS and maps only the recovery sections needed to keep execution, logging, and reboot paths alive during the no-XNU switch.

Required recovery mappings:

| Mapping bit | Required mapping | Hardware descriptor readback |
| --- | --- | --- |
| `LOW_STAGE` | low Stage code/data/BSS/vector section | `ttbr_rt_desc_low=0x00010c02` |
| `STACK` | active SVC stack section | `ttbr_rt_desc_stack=0x00010c02` |
| `VECTOR` | current VBAR/vector section | `ttbr_rt_desc_vector=0x00010c02` |
| `TABLE` | Stage49 round-trip L1 table section | `ttbr_rt_desc_table=0x00010c02` |
| `RAM_CONSOLE0` | `0xde500000 -> 0xde500000` | `ttbr_rt_desc_ram_console0=0xde510c02` |
| `RAM_CONSOLE1` | `0xde600000 -> 0xde600000` | `ttbr_rt_desc_ram_console1=0xde610c02` |
| `IMEM` | `0x0fa00000 -> 0x0fa00000` | `ttbr_rt_desc_imem=0x0fa10c02` |
| `GIC_TIMER` | `0xf9000000 -> 0xf9000000` | `ttbr_rt_desc_gic_timer=0xf9010c02` |
| `PSHOLD` | `0xfc400000 -> 0xfc400000` | `ttbr_rt_desc_pshold=0xfc410c02` |

Hardware-proven mapping masks:

```text
ttbr_rt_mapping_required_mask=0x000001ff
ttbr_rt_mapping_present_mask=0x000001ff
ttbr_rt_mapping_failure_mask=0x00000000
```

## Controlled TTBR0 round-trip sequence

The Stage49 selftest runs after the inherited MMU high-root/bootstrap checks and after the SGI/timer IRQ selftests. It deliberately does not trigger IRQs while the experimental recovery table is active; the during-switch probes are read/write or read-only checks over identity mappings.

Normal sequence:

1. Save original `SCTLR`, `TTBR0`, `TTBCR`, `DACR`, `VBAR`, and current stack pointer.
2. Record `SCTLR.C`/`SCTLR.I` cache bits.
3. Build the Stage49-owned recovery L1 table.
4. Verify the table is 16 KiB aligned and every required recovery mapping is present.
5. Write pre-switch probe `0x49aa0001`.
6. Program `TTBR0 = stage49_ttbr0_roundtrip_l1`.
7. Invalidate TLBs.
8. Read back during-switch TTBR/control/cache state.
9. Write/read during-switch probe `0x49aa0002`.
10. Read `ram_console` signature, GICD/GICC control registers, timer frequency, and restart reason while the Stage49 table is active.
11. Restore `TTBR0 = original_ttbr0`.
12. Invalidate TLBs again.
13. Verify original TTBR/control state and cache bits are restored/preserved.
14. Write/read post-restore probe `0x49aa0003`.
15. Publish the TTBR round-trip status block to the loader preflight roll-up.

Stage49 deliberately does not call the inherited `enable_identity_mmu()` helper for the round-trip path because that helper manipulates SCTLR cache bits as part of older bootstrap setup. The Stage49 round-trip normal path has `sctlr_write_count=0`.

## Hardware TTBR/control state

Recovered Stage49 hardware values:

```text
stage49_ttbr_roundtrip_status=0x49000001
stage49_ttbr_roundtrip_satisfied_mask=0x0000ffff
stage49_ttbr_roundtrip_failure_mask=0x00000000
stage49_ttbr_roundtrip_checksum=0x48df4f5c

ttbr_rt_orig_sctlr=0x00c5487b
ttbr_rt_orig_ttbr0=0x0006c000
ttbr_rt_orig_ttbcr=0x00000000
ttbr_rt_orig_dacr=0x00000003
ttbr_rt_orig_vbar=0x000080a0

ttbr_rt_l1_base=0x00068000
ttbr_rt_l1_limit=0x0006c000
ttbr_rt_l1_checksum_before=0x0ad00000
ttbr_rt_l1_checksum_after=0x0ad00000

ttbr_rt_vector_base=0x000080a0
ttbr_rt_vector_section=0x00000000
ttbr_rt_image_end=0x00072000

ttbr_rt_switched_ttbr0=0x00068000
ttbr_rt_switched_ttbcr=0x00000000
ttbr_rt_switched_dacr=0x00000003
ttbr_rt_switched_sctlr=0x00c5487b

ttbr_rt_restored_ttbr0=0x0006c000
ttbr_rt_restored_ttbcr=0x00000000
ttbr_rt_restored_dacr=0x00000003
ttbr_rt_restored_sctlr=0x00c5487b
```

The original live TTBR0 was `0x0006c000`. Stage49 switched only to the Stage49-owned L1 table at `0x00068000`, then restored the original TTBR0 value.

## Cache-bit proof

Stage49 preserves SCTLR cache bits exactly across the switch:

```text
ttbr_rt_cache_bits_before=0x00000000
ttbr_rt_cache_bits_during=0x00000000
ttbr_rt_cache_bits_after=0x00000000
ttbr_rt_caches_changed=0x00000000
```

No normal-path SCTLR/TTBCR/DACR writes occurred:

```text
ttbr_rt_ttbr0_write_count=0x00000002
ttbr_rt_ttbcr_write_count=0x00000000
ttbr_rt_dacr_write_count=0x00000000
ttbr_rt_sctlr_write_count=0x00000000
ttbr_rt_tlb_invalidate_count=0x00000002
```

Interpretation:

- TTBR0 write #1: switch to Stage49-owned recovery L1 table.
- TTBR0 write #2: restore original TTBR0.
- TLB invalidate #1: after switching.
- TLB invalidate #2: after restoring.
- No TTBCR/DACR/SCTLR changes were needed.

## During-switch probes

During-switch hardware probes succeeded under the Stage49-owned recovery table:

```text
ttbr_rt_probe_before=0x49aa0001
ttbr_rt_probe_during=0x49aa0002
ttbr_rt_probe_after=0x49aa0003
ttbr_rt_ram_console_sig_during=0x43474244
ttbr_rt_gicd_ctlr_during=0x00000001
ttbr_rt_gicc_ctlr_during=0x00000001
ttbr_rt_timer_freq_during=0x0124f800
ttbr_rt_restart_reason_during=0x00000000
```

`0x43474244` is the `ram_console` signature expected by the existing logging path. `0x0124f800` is 19.2 MHz, matching the established MSM8974 ARM generic timer frequency.

## Safety proof markers

Recovered Stage49 TTBR round-trip safety-proof values:

```text
ttbr_rt_xnu_entry_executed=0x00000000
ttbr_rt_macho_bytes_executed=0x00000000
ttbr_rt_proposed_phys_load_written=0x00000000
ttbr_rt_proposed_tte_workspace_written=0x00000000
ttbr_rt_persistent_write_attempted=0x00000000
ttbr_rt_caches_changed=0x00000000
```

Loader roll-up:

```text
loader_safety_mask=0x00007fff
loader_ttbr_roundtrip_status=0x49000001
loader_ttbr_roundtrip_satisfied_mask=0x0000ffff
loader_ttbr_roundtrip_failure_mask=0x00000000
loader_ttbr_roundtrip_checksum=0x48df4f5c
loader_stage_owned_ttbr_status=0x49000001
loader_ttbr_restored_status=0x49000001
loader_cache_preserved_status=0x49000001
loader_satisfied_mask=0x0007ffff
loader_status=0x49000001
```

Stage49 intentionally removes Stage48's global `no-ttbr-write` claim. The replacement proof is narrower and stronger for this stage: the only TTBR writes are the Stage49-owned switch and restore, the original state is restored, and caches remain unchanged.

## Built image

```bash
/mnt/data/mi4-ios6/stage49/build.sh
```

Successful local build:

```text
out/stage49/stage49-qcdt.img
sha256=39a340f14ba5aeca3c35b3e03ea4415a6936687cda029c775eb203b385de34c7
```

Build hashes:

```text
f647c171469f0b7b80329089ddd77e5eb94ef5b18de7d1d5de8f1db4d13b6648  out/stage49/stage49_fixture.macho
951234c62c7fcb987c485831410593a141f01ba69079791947116432f278c4c3  out/stage49/stage49.elf
dce051d3f064036989337b081628e8f9ecf42c8ccf388eb4f8f947ccb76d8075  out/stage49/stage49.bin
a1436d4ce53b3db051d69ce8a332397068af1e4b085ce783058692d8b7f6385c  out/stage49/stage49.img
39a340f14ba5aeca3c35b3e03ea4415a6936687cda029c775eb203b385de34c7  out/stage49/stage49-qcdt.img
```

Size:

```text
text=165400 data=0 bss=250176 dec=415576 hex=65758
```

Boot image parse highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=165400 (0x28618)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage49 mi4ios6=stage49 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved no-xnu-jump no-macho-exec no-proposed-phys-write no-proposed-tte-write no-persist-write
part=kernel offset=0x800 size=165400 sha256=dce051d3f064036989337b081628e8f9ecf42c8ccf388eb4f8f947ccb76d8075
part=dt.img offset=0x29000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Important symbols:

```text
000080a0 T stage49_vectors
0000c11c T stage49_loader_preflight_run
0001e120 T kernel_entry
0001e484 T test_kernel_entry
0001e61c T stage49_main
0003c000 b stage49_loader_preflight_block
00040000 b stage49_safe_table_arena
00048000 b stage49_tte_dryrun_arena
00052000 b stage49_macho_staging_arena
00064440 b stage49_ttbr0_roundtrip_block
00068000 b stage49_ttbr0_roundtrip_l1
00072000 B __stage49_image_end
```

The Stage49-owned round-trip L1 table starts at `0x00068000`, which is 16 KiB aligned and separate from the loader preflight block, safe-table arena, TTE dry-run arena, and Mach-O staging arena.

## Hardware run result

Hardware run succeeded using the required non-persistent path.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage49/stage49-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2626 KB)                       OKAY [  0.084s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.095s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage49-last_kmsg.txt
```

The recovered log was 113889 bytes and contained:

```text
1738 MI4IOS6_STAGE49 markers
1712 MI4IOS6_STAGE49_XNU markers
```

## Key recovered markers

Inherited high-root/MMU success remains intact:

```text
MI4IOS6_STAGE49_XNU high_root_status=0x49000001
MI4IOS6_STAGE49_XNU mmu high bootstrap selftest ok
```

Post-root IRQ retests still pass before the TTBR0 round-trip:

```text
MI4IOS6_STAGE49_XNU gic SGI selftest ok
MI4IOS6_STAGE49_XNU gic timer selftest ok
```

Inherited loader/TTE/safe-table gates still pass:

```text
MI4IOS6_STAGE49_XNU macho_staging_status=0x49000001
MI4IOS6_STAGE49_XNU xnu_tte_dryrun_status=0x49000001
MI4IOS6_STAGE49_XNU xnu_highva_dryrun_status=0x49000001
MI4IOS6_STAGE49_XNU xnu_safe_table_status=0x49000001
```

Stage49 TTBR0 round-trip gates pass:

```text
MI4IOS6_STAGE49_XNU stage49_ttbr_roundtrip_status=0x49000001
MI4IOS6_STAGE49_XNU stage49_ttbr_roundtrip_satisfied_mask=0x0000ffff
MI4IOS6_STAGE49_XNU stage49_ttbr_roundtrip_failure_mask=0x00000000
MI4IOS6_STAGE49_XNU ttbr_rt_switched_ttbr0=0x00068000
MI4IOS6_STAGE49_XNU ttbr_rt_restored_ttbr0=0x0006c000
MI4IOS6_STAGE49_XNU ttbr_rt_cache_bits_before=0x00000000
MI4IOS6_STAGE49_XNU ttbr_rt_cache_bits_during=0x00000000
MI4IOS6_STAGE49_XNU ttbr_rt_cache_bits_after=0x00000000
MI4IOS6_STAGE49_XNU ttbr_rt_ram_console_sig_during=0x43474244
MI4IOS6_STAGE49_XNU ttbr_rt_xnu_entry_executed=0x00000000
MI4IOS6_STAGE49_XNU ttbr_rt_macho_bytes_executed=0x00000000
MI4IOS6_STAGE49_XNU ttbr_rt_proposed_phys_load_written=0x00000000
MI4IOS6_STAGE49_XNU ttbr_rt_proposed_tte_workspace_written=0x00000000
MI4IOS6_STAGE49_XNU ttbr_rt_persistent_write_attempted=0x00000000
MI4IOS6_STAGE49_XNU ttbr_rt_caches_changed=0x00000000
```

Loader/final success markers:

```text
MI4IOS6_STAGE49_XNU loader_ttbr_roundtrip_status=0x49000001
MI4IOS6_STAGE49_XNU loader_stage_owned_ttbr_status=0x49000001
MI4IOS6_STAGE49_XNU loader_ttbr_restored_status=0x49000001
MI4IOS6_STAGE49_XNU loader_cache_preserved_status=0x49000001
MI4IOS6_STAGE49_XNU loader_satisfied_mask=0x0007ffff
MI4IOS6_STAGE49_XNU loader_status=0x49000001
MI4IOS6_STAGE49_XNU Stage49 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE49_XNU kernel_entry ok
MI4IOS6_STAGE49 kernel_entry returned success
MI4IOS6_STAGE49 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage49 is still not a real XNU boot, but it removes a critical live-MMU uncertainty before starting the public-XNU build migration. Stage48 proved local safe-table bytes could be constructed and verified. Stage49 proves the device can temporarily run under a Stage-owned recovery L1 table, keep logging/MMIO/timebase paths alive, and restore the original live TTBR/control state with caches unchanged.

The important boundary is that this was a **Stage-owned recovery-table** round-trip, not a proposed-XNU table install. The proposed physical load range around `0x80000000` and the proposed TTE workspace around `0x8000c000` remain metadata/dry-run targets only. No Mach-O fixture bytes or XNU code were executed.

The remaining blockers are now aligned with the accelerated route: public XNU build workspace and cancro target scaffold, minimal public-XNU object subset compilation, linkable minimal public-XNU Mach-O construction, real sub-section/page-granular XNU mapping policy, pmap/bootstrap table population, exact XNU cache policy, MSM8974 pexpert support, XNU interrupt/timer hooks, IOKit/platform drivers, real kernelcache/loading details, relocation/linking/prelink details, and later code-signing/userspace policy work.

## Success criteria — met

1. Stage49 directory and build outputs created: yes
2. Stage49 command line reflects controlled TTBR0 round-trip semantics: yes
3. Obsolete global `no-ttbr-write` semantics removed from Stage49 source/command line: yes
4. Stage48 loader/TTE/high-VA/safe-table behavior preserved: yes
5. Dedicated TTBR round-trip result ABI added: yes
6. Dedicated Stage49-owned 16 KiB L1 recovery table added: yes
7. Recovery L1 table base is 16 KiB aligned: yes (`0x00068000`)
8. Recovery mappings cover low stage, stack, vectors, table, ram_console, IMEM, GIC/timer, and PS_HOLD: yes
9. Recovery mapping mask reached `0x000001ff`: yes
10. Descriptor readback matched the section descriptor policy: yes
11. Original SCTLR/TTBR0/TTBCR/DACR/VBAR saved: yes
12. TTBR0 switched to Stage49-owned table: yes (`0x00068000`)
13. During-switch identity/log/MMIO/timebase probes passed: yes
14. Original TTBR0 restored: yes (`0x0006c000`)
15. TTBCR/DACR/SCTLR read back unchanged: yes
16. Cache bits before/during/after stayed `0x00000000`: yes
17. Normal path avoided TTBCR/DACR/SCTLR writes: yes
18. TTBR0 write count was exactly two: yes
19. TLB invalidate count was exactly two: yes
20. Proposed physical load writes remained zero: yes
21. Proposed physical workspace writes remained zero: yes
22. Generated Mach-O fixture execution remained zero: yes
23. XNU entry execution remained zero: yes
24. Persistent write attempt remained zero: yes
25. Bootloader accepted `stage49-qcdt.img`: yes
26. Apple-DT/PE_state/GIC/timebase/MMU checks still passed: yes
27. SGI IRQ retest passed before TTBR0 round-trip: yes
28. Timer IRQ retest passed before TTBR0 round-trip: yes
29. TTBR round-trip status returned `0x49000001`: yes
30. Loader TTBR roll-up returned `0x49000001`: yes
31. Loader satisfied mask reached `0x0007ffff`: yes
32. Loader preflight returned status `0x49000001`: yes
33. `kernel_entry` returned success: yes
34. payload reset through PS_HOLD: yes

## Next stage

Stage50 should begin the public-XNU compile migration requested by the accelerated roadmap. The next safe step is a source/build workspace and cancro target scaffold around public Apple OSS only: keep `external/xnu-upstream` detached at `xnu-2050.22.13` for Darwin 12/iOS 6-era context, keep `external/xnu-4570.1.46` as the public ARM implementation reference, and create a minimal, ignored build/output path that can start compiling selected public-XNU-adjacent objects without executing them on hardware yet.

Stage50 should still preserve the Stage49 safety boundary: non-persistent validation only, no flash/erase/write, no XNU jump, no generated Mach-O execution on device, no proposed physical/workspace writes, no cache-policy change, and no persistent hardware configuration.
