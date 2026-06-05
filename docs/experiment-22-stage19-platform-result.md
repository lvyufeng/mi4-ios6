# Experiment 22 — Stage19 Versioned High-Root Platform Result

Date: 2026-06-05

Goal: let the high-virtual root build a versioned early platform-result object from high-DT and PE_state facts.

Stage19 still does **not** run XNU or iOS. It extends the high-virtual root path so it creates a more explicit platform-result block: version, size, consistency mask, CPU/memory/GIC/timer facts, status, and checksum. The identity path then verifies that result through both identity and high-alias views.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage19 adds over Stage18

Stage18 proved:

- high-virtual boot_args pointer consumption,
- high-virtual Apple-DT pointer derivation,
- high-DT `/memory` and `/timer` summary,
- root step mask `0x0000001f`,
- status `0x18000001`.

Stage19 adds a versioned platform-result object inside the high root:

- result version: `1`,
- result size: `0x000000b0`,
- consistency mask: `0x0000000f`,
- DT CPU count and PE CPU count,
- memory base/size,
- GIC distributor and CPU-interface bases,
- timer frequency,
- platform-result status `0x19000001`,
- root step mask `0x0000003f`.

Consistency mask bits:

```text
0x00000001 memory DT/PE consistency
0x00000002 CPU DT/PE consistency
0x00000004 GIC platform consistency
0x00000008 timer DT/PE consistency
```

Complete consistency mask: `0x0000000f`.

Root step bits now include:

```text
0x00000001 enter
0x00000002 init complete
0x00000004 result generated
0x00000008 return-ready
0x00000010 high-DT summary complete
0x00000020 platform-result complete
```

Complete root-step mask: `0x0000003f`.

## Built image

```bash
./stage19/build.sh
```

Successful local build:

```text
out/stage19/stage19-qcdt.img
sha256=62d8d0af516be2509246d6e5306039569687ac535b9d0dabf79173620eb30f33
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=34320 (0x8610)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage19 mi4ios6=stage19 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage19_vectors
0000ab68 t stage19_kernel_root
0000bdd0 T mmu_high_bootstrap_selftest
0000c7a8 T kernel_entry
0000cad0 T test_kernel_entry
0000cc68 T stage19_main
00020000 b stage19_l1_table
00026000 B __stage19_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage19/stage19-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2498 KB)                       OKAY [  0.080s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.089s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage19-last_kmsg.txt
```

The recovered log was 16843 bytes and contained:

```text
312 MI4IOS6_STAGE19 markers
286 MI4IOS6_STAGE19_XNU markers
```

## Key recovered high-root platform-result markers

```text
MI4IOS6_STAGE19_XNU high virtual kernel_root entered
MI4IOS6_STAGE19_XNU high root owns init sequence
MI4IOS6_STAGE19_XNU high init sequence begin
MI4IOS6_STAGE19_XNU high init step validate begin
MI4IOS6_STAGE19_XNU high init step validate ok
MI4IOS6_STAGE19_XNU high init step timebase begin
MI4IOS6_STAGE19_XNU high init step timebase ok
MI4IOS6_STAGE19_XNU high init step gic summary begin
MI4IOS6_STAGE19_XNU high init step gic summary ok
MI4IOS6_STAGE19_XNU high root dt summary begin
MI4IOS6_STAGE19_XNU high root dt summary ok
MI4IOS6_STAGE19_XNU high root platform result begin
MI4IOS6_STAGE19_XNU high root platform result ok
MI4IOS6_STAGE19_XNU high init sequence complete
MI4IOS6_STAGE19_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE19_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE19_XNU high_bootstrap_init_status=0x19000001
MI4IOS6_STAGE19_XNU high_bootstrap_timebase_freq=0x0124f800
MI4IOS6_STAGE19_XNU high_bootstrap_timebase_delta_us=0x000003ea
MI4IOS6_STAGE19_XNU high_bootstrap_gic_irq_count=0x00000120
MI4IOS6_STAGE19_XNU high_bootstrap_gic_cpu_count=0x00000004
MI4IOS6_STAGE19_XNU high_platform_result_version=0x00000001
MI4IOS6_STAGE19_XNU high_platform_result_size=0x000000b0
MI4IOS6_STAGE19_XNU high_platform_result_consistency=0x0000000f
MI4IOS6_STAGE19_XNU high_platform_dt_cpu_count=0x00000004
MI4IOS6_STAGE19_XNU high_platform_pe_cpu_count=0x00000004
MI4IOS6_STAGE19_XNU high_platform_memory_base=0x80000000
MI4IOS6_STAGE19_XNU high_platform_memory_size=0x5e500000
MI4IOS6_STAGE19_XNU high_platform_gic_dist_base=0xf9000000
MI4IOS6_STAGE19_XNU high_platform_gic_cpu_base=0xf9002000
MI4IOS6_STAGE19_XNU high_platform_timer_frequency=0x0124f800
MI4IOS6_STAGE19_XNU high_platform_result_status=0x19000001
MI4IOS6_STAGE19_XNU high_bootstrap_status=0x19000001
MI4IOS6_STAGE19_XNU high_bootstrap_checksum=0x275019c5
MI4IOS6_STAGE19_XNU high virtual kernel_root leaving
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_result=0x19000001
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_expected_checksum=0x275019c5
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_magic_id=0x19001900
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_validation_id=0x00000000
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_root_steps_id=0x0000003f
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_root_status_id=0x19000001
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_init_steps_id=0x0000000f
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_init_status_id=0x19000001
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_root_dt_summary_status_id=0x19000001
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_platform_version_id=0x00000001
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_platform_size_id=0x000000b0
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_platform_consistency_id=0x0000000f
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_platform_dt_cpu_count_id=0x00000004
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_platform_pe_cpu_count_id=0x00000004
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_platform_memory_base_id=0x80000000
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_platform_memory_size_id=0x5e500000
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_platform_gic_dist_id=0xf9000000
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_platform_gic_cpu_id=0xf9002000
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_platform_timer_freq_id=0x0124f800
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_platform_status_id=0x19000001
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_status_id=0x19000001
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_checksum_id=0x275019c5
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_magic_alias=0x19001900
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_root_steps_alias=0x0000003f
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_root_status_alias=0x19000001
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_platform_consistency_alias=0x0000000f
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_platform_status_alias=0x19000001
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_init_steps_alias=0x0000000f
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_status_alias=0x19000001
MI4IOS6_STAGE19_XNU mmu_high_bootstrap_checksum_alias=0x275019c5
MI4IOS6_STAGE19_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the platform-result root path:

```text
MI4IOS6_STAGE19_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the platform-result root path:

```text
MI4IOS6_STAGE19_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE19_XNU kernel_entry ok
MI4IOS6_STAGE19 kernel_entry returned success
MI4IOS6_STAGE19 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage19 gives the high root a more durable kernel-result shape: versioned, sized, and internally consistency-checked. The result fuses high-DT facts with PE_state facts and is verified through both identity and high-alias views.

This is a useful stepping stone toward a real early platform initialization object that later XNU-adjacent code can consume.

## Success criteria — met

1. bootloader accepted `stage19-qcdt.img`: yes
2. high virtual root entry ran: yes
3. high-DT summary ran: yes
4. platform-result object was built: yes
5. platform consistency mask was complete (`0x0000000f`): yes
6. root step mask was complete (`0x0000003f`): yes
7. validation mask was zero: yes
8. high root returned status `0x19000001`: yes
9. checksum matched through identity and alias views: yes
10. SGI and timer IRQ paths still worked after high root: yes
11. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage20 can start making the high root dispatch multiple early init phases from a small phase table:

- keep identity/recovery mappings and caches disabled,
- define ordered high-root phase records,
- record per-phase status and checksum,
- keep identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
