# Experiment 23 — Stage20 High-Root Phase Table

Date: 2026-06-05

Goal: make the high-virtual root dispatch ordered early init phases from a small phase table and verify per-phase status/checksum.

Stage20 still does **not** run XNU or iOS. It extends the high-root result path with explicit phase-table state so that the root path looks less like one monolithic selftest and more like an ordered early-kernel initialization dispatcher.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage20 adds over Stage19

Stage19 proved:

- versioned high-root platform-result object,
- platform consistency mask `0x0000000f`,
- root step mask `0x0000003f`,
- root status `0x19000001`.

Stage20 adds a high-root phase table:

- phase count: `4`,
- completed phase mask: `0x0000000f`,
- per-phase statuses all `0x20000001`,
- phase status checksum: `0x0000000b`,
- root step mask extends to `0x0000007f`,
- final root status `0x20000001`.

Phase IDs:

```text
0 validate
1 high-DT summary
2 platform result
3 return-ready
```

Root step bits now include:

```text
0x00000001 enter
0x00000002 init complete
0x00000004 result generated
0x00000008 return-ready
0x00000010 high-DT summary complete
0x00000020 platform-result complete
0x00000040 phase-table complete
```

Complete root-step mask: `0x0000007f`.

## Built image

```bash
./stage20/build.sh
```

Successful local build:

```text
out/stage20/stage20-qcdt.img
sha256=7ba86412f51183830d009d6521a3e56722244fa2fc22db012d461162b261f68a
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=35688 (0x8b68)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage20 mi4ios6=stage20 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage20_vectors
0000ab68 t stage20_kernel_root
0000bf7c T mmu_high_bootstrap_selftest
0000ca84 T kernel_entry
0000cdac T test_kernel_entry
0000cf44 T stage20_main
00020000 b stage20_l1_table
00026000 B __stage20_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage20/stage20-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2500 KB)                       OKAY [  0.080s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.090s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage20-last_kmsg.txt
```

The recovered log was 17898 bytes and contained:

```text
330 MI4IOS6_STAGE20 markers
304 MI4IOS6_STAGE20_XNU markers
```

## Key recovered high-root phase markers

```text
MI4IOS6_STAGE20_XNU high virtual kernel_root entered
MI4IOS6_STAGE20_XNU high root owns init sequence
MI4IOS6_STAGE20_XNU high init sequence begin
MI4IOS6_STAGE20_XNU high init step validate begin
MI4IOS6_STAGE20_XNU high init step validate ok
MI4IOS6_STAGE20_XNU high init step timebase begin
MI4IOS6_STAGE20_XNU high init step timebase ok
MI4IOS6_STAGE20_XNU high init step gic summary begin
MI4IOS6_STAGE20_XNU high init step gic summary ok
MI4IOS6_STAGE20_XNU high root dt summary begin
MI4IOS6_STAGE20_XNU high root dt summary ok
MI4IOS6_STAGE20_XNU high root platform result begin
MI4IOS6_STAGE20_XNU high root platform result ok
MI4IOS6_STAGE20_XNU high root phase table begin
MI4IOS6_STAGE20_XNU high root phase table ok
MI4IOS6_STAGE20_XNU high init sequence complete
MI4IOS6_STAGE20_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE20_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE20_XNU high_bootstrap_init_status=0x20000001
MI4IOS6_STAGE20_XNU high_platform_result_consistency=0x0000000f
MI4IOS6_STAGE20_XNU high_phase_count=0x00000004
MI4IOS6_STAGE20_XNU high_phase_completed_mask=0x0000000f
MI4IOS6_STAGE20_XNU high_phase_status_checksum=0x0000000b
MI4IOS6_STAGE20_XNU high_phase0_status=0x20000001
MI4IOS6_STAGE20_XNU high_phase1_status=0x20000001
MI4IOS6_STAGE20_XNU high_phase2_status=0x20000001
MI4IOS6_STAGE20_XNU high_phase3_status=0x20000001
MI4IOS6_STAGE20_XNU high_bootstrap_status=0x20000001
MI4IOS6_STAGE20_XNU high_bootstrap_checksum=0x275020e5
MI4IOS6_STAGE20_XNU high virtual kernel_root leaving
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_result=0x20000001
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_expected_checksum=0x275020e5
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_magic_id=0x20002000
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_validation_id=0x00000000
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_root_steps_id=0x0000007f
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_root_status_id=0x20000001
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_init_steps_id=0x0000000f
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_init_status_id=0x20000001
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_platform_consistency_id=0x0000000f
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_phase_count_id=0x00000004
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_phase_mask_id=0x0000000f
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_phase_checksum_id=0x0000000b
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_phase0_status_id=0x20000001
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_phase1_status_id=0x20000001
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_phase2_status_id=0x20000001
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_phase3_status_id=0x20000001
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_status_id=0x20000001
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_checksum_id=0x275020e5
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_magic_alias=0x20002000
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_root_steps_alias=0x0000007f
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_root_status_alias=0x20000001
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_phase_mask_alias=0x0000000f
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_phase_checksum_alias=0x0000000b
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_status_alias=0x20000001
MI4IOS6_STAGE20_XNU mmu_high_bootstrap_checksum_alias=0x275020e5
MI4IOS6_STAGE20_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the phase-table root path:

```text
MI4IOS6_STAGE20_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the phase-table root path:

```text
MI4IOS6_STAGE20_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE20_XNU kernel_entry ok
MI4IOS6_STAGE20 kernel_entry returned success
MI4IOS6_STAGE20 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage20 makes high-root initialization explicitly phased. The high root now records which early-init phases ran, their individual statuses, and a checksum over phase state. The identity verifier checks this through identity and high-alias views before SGI/timer retests.

This is another step toward a real early kernel startup flow where multiple platform-init phases can be added without flattening everything into one ad hoc bootstrap function.

## Success criteria — met

1. bootloader accepted `stage20-qcdt.img`: yes
2. high virtual root entry ran: yes
3. high-root phase table ran: yes
4. phase mask was complete (`0x0000000f`): yes
5. phase checksum matched (`0x0000000b`): yes
6. root step mask was complete (`0x0000007f`): yes
7. validation mask was zero: yes
8. high root returned status `0x20000001`: yes
9. checksum matched through identity and alias views: yes
10. SGI and timer IRQ paths still worked after high root: yes
11. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage21 can start moving an actual early service table into high virtual execution:

- keep identity/recovery mappings and caches disabled,
- define high-root service records for logging, timebase, platform, and interrupts,
- validate service availability from high virtual execution,
- keep identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
